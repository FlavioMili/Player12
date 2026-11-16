// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/help_scene.h"

#include <imgui.h>

#include <algorithm>
#include <utility>

#include "controller/game_controller.h"
#include "global/language_manager.h"
#include "gui/gui_view.h"
#include "gui/input_actions.h"
#include "gui/scenes/main_game_scene.h"
#include "gui/scenes/main_menu_scene.h"
#include "gui/view_models/player_view.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/onboarding.h"

namespace
{
constexpr float CONTENT_MAX_WIDTH = 860.0f;

// clang-format off
constexpr std::array<Glossary::Term, 30> TERMS = {{
    {"GLOSSARY_CONTINUE", "GLOSSARY_CONTINUE_DEF"},
    {"GLOSSARY_OBJECTIVE", "GLOSSARY_OBJECTIVE_DEF"},
    {"GLOSSARY_CONFIDENCE", "GLOSSARY_CONFIDENCE_DEF"},
    {"GLOSSARY_REPUTATION", "GLOSSARY_REPUTATION_DEF"},
    {"GLOSSARY_OVERALL", "GLOSSARY_OVERALL_DEF"},
    {"GLOSSARY_POTENTIAL", "GLOSSARY_POTENTIAL_DEF"},
    {"GLOSSARY_CONDITION", "GLOSSARY_CONDITION_DEF"},
    {"GLOSSARY_SHARPNESS", "GLOSSARY_SHARPNESS_DEF"},
    {"GLOSSARY_MORALE", "GLOSSARY_MORALE_DEF"},
    {"GLOSSARY_FORM", "GLOSSARY_FORM_DEF"},
    {"GLOSSARY_FAMILIARITY", "GLOSSARY_FAMILIARITY_DEF"},
    {"GLOSSARY_KNOWLEDGE", "GLOSSARY_KNOWLEDGE_DEF"},
    {"GLOSSARY_TRANSFER_BUDGET", "GLOSSARY_TRANSFER_BUDGET_DEF"},
    {"GLOSSARY_WAGE_BUDGET", "GLOSSARY_WAGE_BUDGET_DEF"},
    {"GLOSSARY_RELEASE_CLAUSE", "GLOSSARY_RELEASE_CLAUSE_DEF"},
    {"GLOSSARY_SELL_ON", "GLOSSARY_SELL_ON_DEF"},
    {"GLOSSARY_LOAN", "GLOSSARY_LOAN_DEF"},
    {"GLOSSARY_FREE_AGENT", "GLOSSARY_FREE_AGENT_DEF"},
    {"GLOSSARY_TRANSFER_WINDOW", "GLOSSARY_TRANSFER_WINDOW_DEF"},
    {"GLOSSARY_SQUAD_STATUS", "GLOSSARY_SQUAD_STATUS_DEF"},
    {"GLOSSARY_DELEGATION", "GLOSSARY_DELEGATION_DEF"},
    {"GLOSSARY_PRESSING", "GLOSSARY_PRESSING_DEF"},
    {"GLOSSARY_XG", "GLOSSARY_XG_DEF"},
    {"GLOSSARY_SET_PIECES", "GLOSSARY_SET_PIECES_DEF"},
    {"GLOSSARY_SHOUTS", "GLOSSARY_SHOUTS_DEF"},
    {"GLOSSARY_YOUTH_INTAKE", "GLOSSARY_YOUTH_INTAKE_DEF"},
    {"GLOSSARY_INTERNATIONAL_BREAK", "GLOSSARY_INTERNATIONAL_BREAK_DEF"},
    {"GLOSSARY_PROMOTION", "GLOSSARY_PROMOTION_DEF"},
    {"GLOSSARY_GOAL_DIFFERENCE", "GLOSSARY_GOAL_DIFFERENCE_DEF"},
    {"GLOSSARY_EXTRA_TIME", "GLOSSARY_EXTRA_TIME_DEF"},
}};
// clang-format on

constexpr std::array<const char*, 5> START_KEYS = {
    "HELP_START_CONTINUE", "HELP_START_HOME", "HELP_START_DELEGATION",
    "HELP_START_MATCH", "HELP_START_SAVING"};
}  // namespace

std::span<const Glossary::Term> Glossary::terms() { return TERMS; }

HelpScene::HelpScene(GUIView* guiView_ptr, bool inCareer,
                     std::optional<std::size_t> focusTerm)
    : GUIScene(guiView_ptr), in_career(inCareer), focus_term(focusTerm)
{
}

void HelpScene::onEnter()
{
  search_text.clear();
  search_text.reserve(TERMS.size());
  for (const Glossary::Term& term : TERMS)
    search_text.push_back(PlayerView::toLower(std::string(LOC(term.term_key)) +
                                              " " + LOC(term.definition_key)));
  query.fill('\0');
  filterTerms();
  focus_pending = focus_term.has_value() && *focus_term < TERMS.size();
}

void HelpScene::filterTerms()
{
  filtered_query = query.data();
  const std::string needle = PlayerView::toLower(filtered_query);
  matches.clear();
  for (std::size_t index = 0; index < TERMS.size(); ++index)
    if (needle.empty() || search_text[index].contains(needle))
      matches.push_back(index);
}

void HelpScene::leave()
{
  if (in_career)
    guiView->popScene();
  else
    changeScene(std::make_unique<MainMenuScene>(guiView));
}

void HelpScene::render()
{
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const float scale = Theme::scale();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(
      ImGuiStyleVar_WindowPadding,
      ImVec2(Theme::Space::XL * scale, Theme::Space::XL * scale));
  ImGui::Begin("##help", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleVar(3);
  const bool leaveWithEscape =
      !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape, false);

  const float width =
      std::min(ImGui::GetContentRegionAvail().x, CONTENT_MAX_WIDTH * scale);
  const float footerHeight =
      ImGui::GetFrameHeightWithSpacing() + Theme::Space::L * scale;
  ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),
                                (ImGui::GetWindowWidth() - width) * 0.5f));
  ImGui::BeginGroup();
  UI::pageHeader(LOC("HELP_TITLE"), LOC("HELP_SUBTITLE"));
  ImGui::BeginChild(
      "##help_body",
      ImVec2(width, ImGui::GetContentRegionAvail().y - footerHeight));
  renderStart();
  renderShortcuts();
  renderGlossary();
  if (focus_pending && focus_screen_y.has_value())
  {
    ImGui::SetScrollFromPosY(*focus_screen_y - ImGui::GetWindowPos().y, 0.3f);
    focus_pending = false;
    focus_screen_y.reset();
  }
  ImGui::EndChild();
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * scale));
  const bool back =
      UI::secondaryButton(LOC("HELP_BACK"), ImVec2(160.0f * scale, 0.0f));
  ImGui::EndGroup();
  ImGui::End();
  if (back || leaveWithEscape) leave();
}

void HelpScene::renderStart()
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("help_start", LOC("HELP_SECTION_START"));
  ImGui::PushTextWrapPos(0.0f);
  for (const char* key : START_KEYS)
  {
    ImGui::Bullet();
    ImGui::SameLine();
    ImGui::TextUnformatted(LOC(key));
  }
  ImGui::PopTextWrapPos();
  if (auto* hub = in_career
                      ? dynamic_cast<MainGameScene*>(guiView->getBaseScene())
                      : nullptr;
      hub != nullptr && guiView->getController().getManagedTeam())
  {
    ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
    if (UI::secondaryButton(LOC("HELP_REPLAY_TOUR")))
    {
      hub->showPage(MainGameScene::Page::OVERVIEW);
      hub->offerWelcome();
      guiView->navigateTo(nullptr);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
      ImGui::SetTooltip("%s", LOC("HELP_REPLAY_TOUR_HELP"));
    // A hidden first-week checklist comes back to Home.
    GameController& controller = guiView->getController();
    const OnboardingState& checklist = controller.getOnboarding();
    if (checklist.isDismissed() && !checklist.allDone())
    {
      ImGui::SameLine();
      if (UI::secondaryButton(LOC("SETTINGS_CHECKLIST_SHOW")))
      {
        controller.showOnboarding();
        hub->showPage(MainGameScene::Page::OVERVIEW);
        guiView->navigateTo(nullptr);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("%s", LOC("SETTINGS_CHECKLIST_HELP"));
    }
  }
  else if (!in_career)
  {
    ImGui::TextColored(palette.muted, "%s", LOC("HELP_START_NO_CAREER"));
  }
  UI::endCard();
}

void HelpScene::rebuildShortcuts()
{
  const Input::ActionRegistry& registry = Input::registry();
  shortcuts_revision = registry.revision();
  shortcut_rows.clear();
  for (std::size_t index = 0;
       index <
       static_cast<std::size_t>(std::to_underlying(Input::Category::COUNT));
       ++index)
  {
    const auto category = static_cast<Input::Category>(index);
    bool heading = false;
    registry.forEach(
        category,
        [this, &heading, category, &registry](Input::ActionId id,
                                              const Input::Action& action)
        {
          if (!heading)
          {
            shortcut_rows.emplace_back(Input::categoryKey(category),
                                       std::string{}, true, true);
            heading = true;
          }
          std::string keys = Input::chordLabel(action.chords[0]);
          if (action.chords[1] != ImGuiKey_None)
            keys += "  /  " + Input::chordLabel(action.chords[1]);
          shortcut_rows.emplace_back(action.def.label_key, std::move(keys),
                                     false, registry.isDefault(id));
        });
  }
}

void HelpScene::renderShortcuts()
{
  const Theme::Palette& palette = Theme::palette();
  if (shortcut_rows.empty() ||
      shortcuts_revision != Input::registry().revision())
    rebuildShortcuts();
  UI::beginAutoHeightCard("help_keys", LOC("HELP_SECTION_KEYS"));
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(palette.muted, "%s", LOC("HELP_KEYS_HINT"));
  ImGui::PopTextWrapPos();
  const float keysX = ImGui::GetContentRegionAvail().x * 0.6f;
  for (const ShortcutRow& row : shortcut_rows)
  {
    if (row.heading)
    {
      UI::sectionLabel(LOC(row.label_key.c_str()));
      continue;
    }
    ImGui::TextUnformatted(LOC(row.label_key.c_str()));
    ImGui::SameLine(keysX);
    // Changed bindings in full ink, defaults muted.
    ImGui::TextColored(row.is_default ? palette.muted : palette.text, "%s",
                       row.keys.c_str());
  }
  UI::endCard();
}

void HelpScene::renderGlossary()
{
  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  UI::beginAutoHeightCard("help_glossary", LOC("HELP_SECTION_GLOSSARY"));
  ImGui::SetNextItemWidth(-FLT_MIN);
  ImGui::InputTextWithHint("##glossary_search", LOC("HELP_GLOSSARY_SEARCH"),
                           query.data(), query.size());
  if (filtered_query != query.data()) filterTerms();
  if (matches.empty())
    UI::emptyState(LOC("HELP_GLOSSARY_EMPTY_TITLE"),
                   LOC("HELP_GLOSSARY_EMPTY_BODY"));
  ImGui::PushTextWrapPos(0.0f);
  for (const std::size_t index : matches)
  {
    const bool focused = focus_term.has_value() && *focus_term == index;
    const ImVec2 start = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * scale));
    {
      Theme::ScopedText title(Theme::Text::TITLE);
      ImGui::TextUnformatted(LOC(TERMS[index].term_key));
    }
    ImGui::TextColored(palette.muted, "%s", LOC(TERMS[index].definition_key));
    if (focused)
    {
      // A bar beside the term the palette opened (shape, not colour only).
      ImGui::GetWindowDrawList()->AddRectFilled(
          ImVec2(start.x - Theme::Space::S * scale, start.y),
          ImVec2(start.x - Theme::Space::S * scale + 3.0f * scale,
                 ImGui::GetItemRectMax().y),
          Theme::toU32(palette.info));
      // The page body scrolls, not the card: remember where the term is.
      if (focus_pending) focus_screen_y = start.y;
    }
  }
  ImGui::PopTextWrapPos();
  UI::endCard();
}
