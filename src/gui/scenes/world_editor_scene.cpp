// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/world_editor_scene.h"

#include <imgui.h>

#include <cctype>
#include <cfloat>
#include <set>
#include <utility>

#include "database/world/world_io.h"
#include "database/world/world_runtime.h"
#include "global/language_manager.h"
#include "gui/scenes/career_start.h"
#include "gui/scenes/world_editor_state.h"
#include "gui/widgets/theme.h"
#include "model/inbox.h"

using json = nlohmann::json;
using World::Kind;
using State = WorldEditorScene::State;
using Page = State::Page;
using namespace WorldEditorUi;

namespace
{
constexpr std::size_t MAX_HISTORY = 30;
constexpr std::size_t HISTORY_BYTES = 128U * 1024U * 1024U;
constexpr float STATUS_SECONDS = 8.0F;
constexpr float WIDE_WIDTH = 1180.0F;
constexpr float NAV_WIDTH = 190.0F;
constexpr float POPUP_WIDTH = 520.0F;
}  // namespace

namespace WorldEditorUi
{
std::int64_t integer(const json& entity, const char* key)
{
  const auto found = entity.find(key);
  return found != entity.end() && found->is_number_integer()
             ? found->get<std::int64_t>()
             : 0;
}
std::string format(const char* key, std::vector<std::string> args)
{
  return formatLocalized(key, args);
}
const World::FieldSpec* specOf(Kind kind, std::string_view path)
{
  const auto& all = World::fields(kind);
  const auto found = std::ranges::find(all, path, &World::FieldSpec::path);
  return found == all.end() ? nullptr : &*found;
}
std::string fieldLabel(std::optional<Kind> kind, const std::string& path)
{
  const World::FieldSpec* spec = kind ? specOf(*kind, path) : nullptr;
  return spec != nullptr ? LOC(std::string(spec->label).c_str()) : path;
}
std::string kindName(Kind kind)
{
  return LOC(std::string(World::kindLabel(kind)).c_str());
}

std::string stem(const std::filesystem::path& path)
{
  return World::pathToUtf8(path.filename());
}
float scaled(float value) { return value * Theme::scale(); }
void muted(const std::string& text)
{
  ImGui::PushStyleColor(ImGuiCol_Text, Theme::palette().muted);
  ImGui::PushTextWrapPos(0.0F);
  ImGui::TextUnformatted(text.c_str());
  ImGui::PopTextWrapPos();
  ImGui::PopStyleColor();
}
/** Button that starts a new line when it does not fit; false while disabled. */
bool flowButton(const char* label, bool& first, bool enabled, bool primary,
                UI::ButtonSize size)
{
  if (!first)
  {
    UI::sameLineIfFits(UI::buttonWidth(label, size));
  }
  first = false;
  ImGui::BeginDisabled(!enabled);
  const bool pressed = primary ? UI::primaryButton(label, ImVec2(0, 0), size)
                               : UI::secondaryButton(label, ImVec2(0, 0), size);
  ImGui::EndDisabled();
  return pressed && enabled;
}

bool cancelPressed(ImVec2 size)
{
  return UI::secondaryButton(LOC("WORLD_BTN_CANCEL"), size) ||
         ImGui::IsKeyPressed(ImGuiKey_Escape);
}
}  // namespace WorldEditorUi

State::State(GUIView* owner, Options start)
    : view(owner), options(std::move(start))
{
  if (options.world)
  {
    load(*options.world, options.file, false);
  }
  else
  {
    load(*World::defaultWorld(), options.file, false);
  }
}

std::optional<Kind> State::kindOf(Page page)
{
  if (page < Page::Language || page > Page::Player)
  {
    return std::nullopt;
  }
  return static_cast<Kind>(static_cast<int>(page) -
                           static_cast<int>(Page::Language));
}

State::Page State::pageOf(Kind kind)
{
  return static_cast<Page>(static_cast<int>(kind) +
                           static_cast<int>(Page::Language));
}

json* State::entity(Kind kind, std::int64_t key)
{
  if (key < 0)
  {
    return nullptr;
  }
  if (kind != Kind::Travel)
  {
    return doc.find(kind, key);
  }
  json& travel = doc.section(kind);
  return static_cast<std::size_t>(key) < travel.size()
             ? &travel.at(static_cast<std::size_t>(key))
             : nullptr;
}

std::string State::selectedName() const
{
  const auto k = kind();
  if (!k || selected < 0)
  {
    return {};
  }
  const json* found = const_cast<State*>(this)->entity(*k, selected);
  return found != nullptr ? World::entityName(*k, *found) : std::string();
}

void State::load(World::Document document, std::filesystem::path path,
                 bool changed_)
{
  doc = std::move(document);
  file = std::move(path);
  dirty = changed_;
  undo.clear();
  redo.clear();
  last_edit.clear();
  conflicts.clear();
  duplicates.clear();
  combined.reset();
  setPage(Page::Details);
  issues_stale = true;
  since_edit = 1.0F;
}

void State::notify(std::string message, bool error)
{
  status = std::move(message);
  status_error = error;
  status_seconds = STATUS_SECONDS;
}

bool State::openFile(const std::filesystem::path& path)
{
  World::LoadResult loaded = World::readFile(path);
  if (!loaded.document)
  {
    notify(LOC("WORLD_ERR_READ"), true);
    return false;
  }
  load(std::move(*loaded.document), path, false);
  if (const auto errors = World::countOf(loaded.issues, World::Severity::Error);
      errors != 0)
  {
    setPage(Page::Problems);
    notify(format("WORLD_OPENED_WITH_ERRORS", {std::to_string(errors)}), true);
  }
  return true;
}

bool State::save()
{
  if (World::hasErrors(problems(true)))
  {
    notify(LOC("WORLD_ERR_ERRORS"), true);
    return false;
  }
  const auto path = World::Library::save(doc, file);
  if (path.empty())
  {
    notify(LOC("WORLD_ERR_WRITE"), true);
    return false;
  }
  file = path;
  dirty = false;
  notify(format("WORLD_SAVED", {stem(path)}));
  return true;
}

bool State::exportTo(const std::filesystem::path& chosen)
{
  if (World::hasErrors(problems(true)))
  {
    notify(LOC("WORLD_ERR_ERRORS"), true);
    return false;
  }
  std::filesystem::path path = chosen;
  if (path.extension().empty())
  {
    path += std::string(World::FILE_EXTENSION);
  }
  if (!World::writeFile(doc, path))
  {
    notify(LOC("WORLD_ERR_WRITE"), true);
    return false;
  }
  notify(format("WORLD_EXPORTED", {stem(path)}));
  return true;
}

void State::request(Action action, std::filesystem::path path)
{
  if (dirty)
  {
    pending = action;
    pending_path = std::move(path);
    open_popup = Popup::Discard;
    return;
  }
  perform(action, path);
}

void State::perform(Action action, const std::filesystem::path& path)
{
  switch (action)
  {
    case Action::NewTemplate:
      load(World::templateDocument(), {}, false);
      break;
    case Action::NewEmpty:
      load(World::Document::empty(LOC("WORLD_NEW_NAME")), {}, false);
      break;
    case Action::Open:
      if (path.empty())
      {
        load(*World::defaultWorld(), {}, false);
      }
      else
      {
        openFile(path);
      }
      break;
    case Action::Close:
      next_scene = CareerStart::mainMenu(view);
      break;
    case Action::None:
      break;
  }
}

bool State::startCareer()
{
  const bool blocked = World::hasErrors(problems(true)) ||
                       World::countOf(issues, World::Severity::Blocking) != 0;
  if (blocked)
  {
    setPage(Page::Problems);
    notify(LOC("WORLD_ERR_CANT_START"), true);
    return false;
  }
  auto world = std::make_shared<const World::Document>(doc);
  next_scene = options.career_slot
                   ? CareerStart::startCareer(view, *options.career_slot, world)
                   : CareerStart::chooseSlot(view, world);
  return true;
}

State::Snapshot State::capture() const
{
  return {doc.root().dump(), conflicts, duplicates};
}

void State::snapshot(const std::string& key)
{
  if (!key.empty() && key == last_edit && !undo.empty())
  {
    return;
  }
  last_edit = key;
  redo.clear();
  undo.push_back(capture());
  std::size_t bytes = 0;
  for (const Snapshot& entry : undo)
  {
    bytes += entry.text.size();
  }
  while (undo.size() > MAX_HISTORY ||
         (bytes > HISTORY_BYTES && undo.size() > 1))
  {
    bytes -= undo.front().text.size();
    undo.pop_front();
  }
}

void State::restore(const Snapshot& snapshot_)
{
  json root = json::parse(snapshot_.text, nullptr, false);
  if (root.is_discarded())
  {
    return;
  }
  doc = World::Document(std::move(root));
  conflicts = snapshot_.conflicts;
  duplicates = snapshot_.duplicates;
  last_edit.clear();
  form_stale = true;
  if (const auto k = kind(); k && entity(*k, selected) == nullptr)
  {
    selected = -1;
  }
  settleConflicts();
}

void State::undoStep()
{
  if (undo.empty())
  {
    return;
  }
  redo.push_back(capture());
  const Snapshot previous = std::move(undo.back());
  undo.pop_back();
  restore(previous);
}

void State::redoStep()
{
  if (redo.empty())
  {
    return;
  }
  undo.push_back(capture());
  const Snapshot next = std::move(redo.back());
  redo.pop_back();
  restore(next);
}

void State::changed()
{
  dirty = true;
  issues_stale = true;
  since_edit = 0.0F;
  rows_dirty = true;
}

void State::setPage(Page next)
{
  page = next;
  selected = -1;
  cup = -1;
  search.clear();
  parent_filter = -1;
  form_stale = true;
  // Rebuilt now, not at the next frame: the list may draw in this frame.
  rebuildRows();
}

void State::select(Kind target, std::int64_t key)
{
  if (page != pageOf(target))
  {
    setPage(pageOf(target));
  }
  selected = key;
  cup = -1;
  form_stale = true;
  scroll_to_selected = true;
}

void State::update(float delta)
{
  since_edit += delta;
  status_seconds = std::max(0.0F, status_seconds - delta);
  if (const auto path = dialog.take())
  {
    switch (purpose)
    {
      case Purpose::Open:
        request(Action::Open, *path);
        break;
      case Purpose::Combine:
        combineFile(*path);
        break;
      case Purpose::Export:
        exportTo(*path);
        break;
    }
  }
}

void State::handleDrop(const std::filesystem::path& path)
{
  if (pristine())
  {
    openFile(path);
    return;
  }
  pending_path = path;
  open_popup = Popup::Dropped;
}

namespace
{
std::vector<std::pair<Page, std::string>> navigation(const State& state)
{
  std::vector<std::pair<Page, std::string>> items;
  items.emplace_back(Page::Details, LOC("WORLD_NAV_DETAILS"));
  for (const Kind kind : World::KINDS)
  {
    items.emplace_back(
        State::pageOf(kind),
        kindName(kind) + " (" + std::to_string(state.doc.count(kind)) + ")");
  }
  items.emplace_back(Page::Problems,
                     std::string(LOC("WORLD_NAV_PROBLEMS")) + " (" +
                         std::to_string(state.issues.size()) + ")");
  if (state.openConflicts() != 0)
  {
    items.emplace_back(Page::Conflicts,
                       std::string(LOC("WORLD_NAV_CONFLICTS")) + " (" +
                           std::to_string(state.openConflicts()) + ")");
  }
  return items;
}
}  // namespace

void State::renderHeader()
{
  const Theme::Palette& palette = Theme::palette();
  UI::sectionLabel(LOC("WORLD_EDITOR_TITLE"));
  const World::Manifest manifest = doc.manifest();
  {
    Theme::ScopedText heading(Theme::Text::HEADING);
    ImGui::TextUnformatted(manifest.name.empty() ? LOC("WORLD_UNNAMED")
                                                 : manifest.name.c_str());
  }
  if (dirty)
  {
    ImGui::SameLine();
    UI::badge(LOC("WORLD_UNSAVED"), palette.warning);
  }
  const std::array<std::size_t, 3> totals = {
      World::countOf(issues, World::Severity::Error),
      World::countOf(issues, World::Severity::Blocking),
      World::countOf(issues, World::Severity::Warning)};
  const std::array<const char*, 3> keys = {
      "WORLD_COUNT_ERRORS", "WORLD_COUNT_BLOCKING", "WORLD_COUNT_WARNINGS"};
  const std::array<ImVec4, 3> colours = {palette.negative, palette.warning,
                                         palette.muted};
  bool any = false;
  for (std::size_t i = 0; i < totals.size(); ++i)
  {
    if (totals[i] == 0)
    {
      continue;
    }
    any = true;
    const std::string label = format(keys[i], {std::to_string(totals[i])}) +
                              "##counter" + std::to_string(i);
    UI::sameLineIfFits(UI::buttonWidth(label.c_str(), UI::ButtonSize::COMPACT));
    ImGui::PushStyleColor(ImGuiCol_Text, colours[i]);
    if (UI::secondaryButton(label.c_str(), ImVec2(0, 0),
                            UI::ButtonSize::COMPACT))
    {
      setPage(Page::Problems);
    }
    ImGui::PopStyleColor();
  }
  if (!any)
  {
    ImGui::SameLine();
    UI::badge(LOC("WORLD_NO_PROBLEMS"), palette.positive);
  }
  bool first = true;
  const bool busy = dialog.busy();
  if (flowButton(LOC("WORLD_BTN_NEW"), first))
  {
    open_popup = Popup::New;
  }
  if (flowButton(LOC("WORLD_BTN_OPEN"), first, !busy))
  {
    library = World::Library::list();
    choosing_combine = false;
    open_popup = Popup::Chooser;
  }
  if (flowButton(LOC("WORLD_BTN_COMBINE"), first, !busy))
  {
    library = World::Library::list();
    choosing_combine = true;
    open_popup = Popup::Chooser;
  }
  if (flowButton(LOC("WORLD_BTN_SAVE"), first, dirty || file.empty()))
  {
    save();
  }
  if (flowButton(LOC("WORLD_BTN_EXPORT"), first, !busy))
  {
    purpose = Purpose::Export;
    dialog.save(view->getWindow(), World::Library::fileStem(manifest.name) +
                                       std::string(World::FILE_EXTENSION));
  }
  if (flowButton(LOC("WORLD_BTN_UNDO"), first, !undo.empty()))
  {
    undoStep();
  }
  if (flowButton(LOC("WORLD_BTN_REDO"), first, !redo.empty()))
  {
    redoStep();
  }
  if (flowButton(LOC("WORLD_BTN_START"), first, true, true))
  {
    startCareer();
  }
  if (flowButton(LOC("WORLD_BTN_CLOSE"), first))
  {
    request(Action::Close);
  }
  if (status_seconds > 0.0F)
  {
    ImGui::PushStyleColor(ImGuiCol_Text,
                          status_error ? palette.negative : palette.positive);
    ImGui::PushTextWrapPos(0.0F);
    ImGui::TextUnformatted(status.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
  }
  ImGui::Dummy(ImVec2(0.0F, scaled(Theme::Space::XS)));
}

void State::renderNavigation(float width)
{
  UI::beginCard("##world_nav", nullptr,
                ImVec2(width, ImGui::GetContentRegionAvail().y));
  int index = 0;
  for (const auto& [target, label] : navigation(*this))
  {
    const std::string id = label + "###nav" + std::to_string(index++);
    if (ImGui::Selectable(id.c_str(), page == target, 0,
                          ImVec2(0, ImGui::GetFrameHeight())) &&
        page != target)
    {
      setPage(target);
    }
  }
  UI::endCard();
}

void State::renderNavigationCombo()
{
  const auto items = navigation(*this);
  const auto current =
      std::ranges::find(items, page, &std::pair<Page, std::string>::first);
  ImGui::SetNextItemWidth(scaled(260.0F));
  if (!ImGui::BeginCombo("##world_section",
                         current == items.end() ? "" : current->second.c_str()))
  {
    return;
  }
  for (const auto& [target, label] : items)
  {
    if (ImGui::Selectable(label.c_str(), page == target) && page != target)
    {
      setPage(target);
    }
  }
  ImGui::EndCombo();
}

namespace
{
const char* popupId(State::Popup popup)
{
  static constexpr std::array<const char*, 7> IDS = {"",
                                                     "##world_new",
                                                     "##world_chooser",
                                                     "##world_discard",
                                                     "##world_delete",
                                                     "##world_summary",
                                                     "##world_dropped"};
  return IDS.at(static_cast<std::size_t>(popup));
}
}  // namespace

bool State::modal(const std::string& title)
{
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                          ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
  ImGui::SetNextWindowSizeConstraints(ImVec2(scaled(POPUP_WIDTH), 0),
                                      ImVec2(scaled(POPUP_WIDTH), FLT_MAX));
  if (!ImGui::BeginPopupModal(popupId(popup), nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize |
                                  ImGuiWindowFlags_NoTitleBar |
                                  ImGuiWindowFlags_NoSavedSettings))
  {
    popup = Popup::None;
    return false;
  }
  Theme::ScopedText heading(Theme::Text::TITLE);
  ImGui::TextUnformatted(title.c_str());
  return true;
}

void State::endModal(bool close)
{
  if (close)
  {
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void State::renderChooser()
{
  if (!modal(
          LOC(choosing_combine ? "WORLD_COMBINE_TITLE" : "WORLD_OPEN_TITLE")))
  {
    return;
  }
  muted(LOC(choosing_combine ? "WORLD_COMBINE_HINT" : "WORLD_OPEN_HINT"));
  ImGui::BeginChild("##world_library", ImVec2(0, scaled(260.0F)),
                    ImGuiChildFlags_Borders);
  // Entry 0 is the built-in world (empty path), then the saved worlds.
  std::vector<std::pair<std::string, std::filesystem::path>> entries = {
      {LOC("WORLD_BUILTIN"), {}}};
  for (const World::Library::Entry& entry : library)
  {
    const auto count = [&](Kind kind)
    { return std::to_string(entry.counts[static_cast<std::size_t>(kind)]); };
    entries.emplace_back(entry.manifest.name + "  -  " +
                             format("WORLD_ENTRY_COUNTS",
                                    {count(Kind::Club), count(Kind::Player)}),
                         entry.path);
  }
  bool close = false;
  for (std::size_t i = 0; i < entries.size() && !close; ++i)
  {
    ImGui::PushID(static_cast<int>(i));
    const auto& [label, path] = entries[i];
    close = ImGui::Selectable(label.c_str());
    ImGui::PopID();
    if (!close)
    {
      continue;
    }
    if (choosing_combine)
    {
      path.empty() ? combineDocument(*World::defaultWorld(), label)
                   : combineFile(path);
    }
    else
    {
      request(Action::Open, path);
    }
  }
  if (library.empty())
  {
    muted(LOC("WORLD_LIBRARY_EMPTY"));
  }
  ImGui::EndChild();
  if (UI::secondaryButton(LOC("WORLD_BTN_BROWSE")))
  {
    purpose = choosing_combine ? Purpose::Combine : Purpose::Open;
    dialog.open(view->getWindow());
    close = true;
  }
  ImGui::SameLine();
  endModal(cancelPressed() || close);
}

void State::renderPopups()
{
  if (open_popup != Popup::None)
  {
    popup = open_popup;
    open_popup = Popup::None;
    ImGui::OpenPopup(popupId(popup));
  }
  switch (popup)
  {
    case Popup::None:
      break;
    case Popup::New:
      if (modal(LOC("WORLD_NEW_TITLE")))
      {
        const ImVec2 wide(-FLT_MIN, 0);
        bool close = false;
        for (const auto& [action, label] :
             {std::pair{Action::NewTemplate, "WORLD_NEW_TEMPLATE"},
              std::pair{Action::NewEmpty, "WORLD_NEW_EMPTY"}})
        {
          const bool templated = action == Action::NewTemplate;
          if (templated ? UI::primaryButton(LOC(label), wide)
                        : UI::secondaryButton(LOC(label), wide))
          {
            request(action);
            close = true;
          }
          muted(LOC(templated ? "WORLD_NEW_TEMPLATE_HINT"
                              : "WORLD_NEW_EMPTY_HINT"));
        }
        endModal(close || cancelPressed(wide));
      }
      break;
    case Popup::Chooser:
      renderChooser();
      break;
    case Popup::Discard:
    case Popup::Delete:
    {
      const bool discard = popup == Popup::Discard;
      const auto result =
          discard ? UI::confirmDialog(
                        popupId(popup), LOC("WORLD_DISCARD_TITLE"),
                        LOC("WORLD_DISCARD_BODY"), LOC("WORLD_DISCARD"),
                        LOC("WORLD_KEEP_EDITING"))
                  : UI::confirmDialog(
                        popupId(popup),
                        format("WORLD_DELETE_TITLE", {selectedName()}).c_str(),
                        popup_text.c_str(), LOC("WORLD_BTN_DELETE"),
                        LOC("WORLD_BTN_CANCEL"));
      if (result == UI::DialogResult::CONFIRM && !discard)
      {
        removeSelected();
      }
      else if (result == UI::DialogResult::CONFIRM)
      {
        dirty = false;
        perform(std::exchange(pending, Action::None), pending_path);
      }
      else if (result == UI::DialogResult::CANCEL)
      {
        pending = Action::None;
      }
      if (result != UI::DialogResult::NONE)
      {
        popup = Popup::None;
      }
      break;
    }
    case Popup::Summary:
      renderSummary();
      break;
    case Popup::Dropped:
      if (modal(LOC("WORLD_DROP_TITLE")))
      {
        muted(format("WORLD_DROP_BODY", {stem(pending_path)}));
        const bool combine = UI::primaryButton(LOC("WORLD_DROP_COMBINE"));
        ImGui::SameLine();
        const bool open = UI::secondaryButton(LOC("WORLD_DROP_OPEN"));
        ImGui::SameLine();
        if (combine)
        {
          combineFile(pending_path);
        }
        else if (open)
        {
          openFile(pending_path);
        }
        endModal(combine || open || cancelPressed());
      }
      break;
  }
}

void State::render()
{
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(scaled(Theme::Space::L), scaled(Theme::Space::M)));
  ImGui::Begin("##world_editor", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleVar(3);
  problems();
  if (rows_dirty)
  {
    rebuildRows();
  }
  if (form_stale)
  {
    buildForm();
  }
  renderHeader();
  const bool wide = ImGui::GetContentRegionAvail().x >= scaled(WIDE_WIDTH);
  if (!wide)
  {
    renderNavigationCombo();
  }
  const float height =
      std::max(scaled(160.0F), ImGui::GetContentRegionAvail().y);
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  if (wide)
  {
    renderNavigation(scaled(NAV_WIDTH));
    ImGui::SameLine();
  }
  const float width = ImGui::GetContentRegionAvail().x;
  if (kind())
  {
    const float list = std::max(scaled(340.0F), width * 0.42F);
    renderList(list, height);
    ImGui::SameLine();
    renderForm(width - list - gap, height);
  }
  else if (page == Page::Details)
  {
    renderDetails(width, height);
  }
  else if (page == Page::Problems)
  {
    renderProblems(width, height);
  }
  else
  {
    renderConflicts(width, height);
  }
  renderPopups();
  ImGui::End();
}

WorldEditorScene::WorldEditorScene(GUIView* view, Options options)
    : GUIScene(view), state(std::make_unique<State>(view, std::move(options)))
{
}

WorldEditorScene::~WorldEditorScene() = default;

void WorldEditorScene::handleEvent(const SDL_Event& event)
{
  GUIScene::handleEvent(event);
  if (const auto path = FileDialog::dropped(event))
  {
    state->handleDrop(*path);
  }
}

void WorldEditorScene::update(float deltaTime)
{
  state->update(deltaTime);
  if (state->next_scene)
  {
    changeScene(std::move(state->next_scene));
  }
}

void WorldEditorScene::render()
{
  state->render();
  if (state->next_scene)
  {
    changeScene(std::move(state->next_scene));
  }
}
