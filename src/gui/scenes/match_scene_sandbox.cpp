// Football Management Project — sandbox-only match presentation.
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <string_view>

#include "controller/game_controller.h"
#include "global/language_manager.h"
#include "global/paths.h"
#include "gui/gui_view.h"
#include "gui/render/match_renderer_2d.h"
#include "gui/scenes/match_scene.h"
#include "gui/scenes/match_shouts_bar.h"
#include "gui/view_models/match_clock.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/match_rules.h"
#include "model/player.h"
#include "model/role_utils.h"
#include "model/team.h"
#include "tools/match_sandbox_page_ui.h"

namespace
{
namespace Page = SandboxPageUI;
float s() { return Theme::scale(); }

float actionWidth(const char* text, ImFont* font, float size = 14)
{
  ImGui::PushFont(font, size);
  const auto ink =
      SandboxTypography::labelBounds(font, ImGui::GetFontSize(), text, 0);
  ImGui::PopFont();
  return ink.max.x - ink.min.x + 12 * s();
}

bool action(const char* text, ImFont* font, bool selected = false,
            float width = 0, float size = 14)
{
  if (width <= 0) width = actionWidth(text, font, size);
  ImGui::PushID(text);
  ImGui::PushStyleColor(ImGuiCol_Button, selected ? Page::LIME : Page::DARK);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        selected ? Page::SCROLL_HOVER : Page::LIGHT);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                        selected ? Page::LIME : Page::MID);
  const bool clicked = ImGui::Button("##action", ImVec2(width, 32 * s()));
  const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
  SandboxTypography::label(ImGui::GetWindowDrawList(), font, size, text, a, b,
                           ImGui::GetColorU32(ImGui::ColorConvertU32ToFloat4(
                               selected ? Page::INK : Page::CREAM)));
  ImGui::PopStyleColor(3);
  ImGui::PopID();
  return clicked;
}

void next(float width, float right = 0)
{
  if (ImGui::GetItemRectMax().x + 6 * s() + width <=
      (right > 0
           ? right
           : ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x))
    ImGui::SameLine(0, 6 * s());
}

void beginFixed(const char* id, const char* title, ImVec2 size, ImFont* bold,
                bool scrollable = true)
{
  ImGui::PushStyleColor(ImGuiCol_ChildBg, Page::LIGHT);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16 * s(), 12 * s()));
  ImGui::BeginChild(id, size, ImGuiChildFlags_AlwaysUseWindowPadding,
                    scrollable ? ImGuiWindowFlags_None
                               : ImGuiWindowFlags_NoScrollbar |
                                     ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  Page::tracked(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), bold,
                title, 16, s());
  ImGui::Dummy(ImVec2(0, 26 * s()));
}

std::string abbreviation(const std::string& name)
{
  std::string result;
  for (unsigned char letter : name)
    if (std::isalpha(letter) && letter < 128)
    {
      result += static_cast<char>(std::toupper(letter));
      if (result.size() == 3) break;
    }
  return result.empty() ? name.substr(0, 3) : result;
}

std::string roleCode(const char* label)
{
  std::string out;
  bool first = true;
  for (const unsigned char letter : std::string_view(label))
  {
    if (std::isalpha(letter))
    {
      if (first) out += static_cast<char>(std::toupper(letter));
      first = false;
    }
    else
      first = true;
  }
  if (out.size() < 2) return abbreviation(label);
  return out.substr(0, 3);
}

// Centre visible glyph ink, including accented player names, in a table cell.
void cellText(const std::string& value, ImVec2 a, ImVec2 b,
              bool leftAligned = false)
{
  auto* font = ImGui::GetFont();
  const float size = ImGui::GetFontSize();
  auto* baked = font->GetFontBaked(size);
  const float factor = size / baked->Size;
  ImVec2 lo(FLT_MAX, FLT_MAX), hi(-FLT_MAX, -FLT_MAX);
  float x = 0;
  for (const char* c = value.c_str(); *c;)
  {
    unsigned int code;
    const int count = ImTextCharFromUtf8(&code, c, nullptr);
    if (!count) break;
    c += count;
    const auto* glyph = baked->FindGlyph(code);
    if (glyph->Visible)
    {
      lo.x = std::min(lo.x, x + glyph->X0 * factor);
      hi.x = std::max(hi.x, x + glyph->X1 * factor);
      lo.y = std::min(lo.y, glyph->Y0 * factor);
      hi.y = std::max(hi.y, glyph->Y1 * factor);
    }
    x += glyph->AdvanceX * factor;
  }
  if (lo.x == FLT_MAX) return;
  const ImVec2 at(
      std::round(leftAligned ? a.x - lo.x : (a.x + b.x - lo.x - hi.x) * .5f),
      std::round((a.y + b.y - lo.y - hi.y) * .5f));
  auto* draw = ImGui::GetWindowDrawList();
  draw->PushClipRect(a, b, true);
  draw->AddText(font, size, at, Page::CREAM, value.c_str());
  draw->PopClipRect();
}
void shirt(ImVec2 a, ImU32 color)
{
  auto* draw = ImGui::GetWindowDrawList();
  const auto pt = [&](float x, float y)
  { return ImVec2(a.x + x * s(), a.y + y * s()); };
  draw->AddQuadFilled(pt(4, 3), pt(1, 6), pt(3, 10), pt(6, 8), color);
  draw->AddQuadFilled(pt(12, 3), pt(15, 6), pt(13, 10), pt(10, 8), color);
  draw->AddRectFilled(pt(5, 3), pt(11, 16), color);
  draw->AddLine(pt(6, 3), pt(10, 3), Page::DARK, 2 * s());
}
}  // namespace

void MatchScene::loadSandboxFonts()
{
  auto* atlas = ImGui::GetIO().Fonts;
  const auto font = [&](const char* file, float size)
  {
    const auto config = SandboxTypography::manropeConfig();
    for (auto* existing : atlas->Fonts)
      if (std::string_view(existing->GetDebugName()).find(file) !=
              std::string_view::npos &&
          !existing->Sources.empty() &&
          existing->Sources[0]->ExtraSizeScale == config.ExtraSizeScale)
        return existing;
    return atlas->AddFontFromFileTTF(
        AssetPaths::file((std::string("assets/fonts/") + file).c_str()).c_str(),
        size, &config);
  };
  sandbox_regular = font("Manrope-Regular.ttf", 16);
  sandbox_bold = font("Manrope-Bold.ttf", 14);
}

void MatchScene::renderSandboxOverview(float width)
{
  Page::beginCard("##sandbox_overview", "MATCH OVERVIEW", width, Page::LIGHT,
                  s(), sandbox_bold);
  const std::array<const char*, 4> tabs{"Stats", "Events", "Analysis",
                                        "Engine"};
  const float tabWidth = (ImGui::GetContentRegionAvail().x - 12 * s()) / 4;
  for (int tab = 0; tab < 4; ++tab)
  {
    if (tab) ImGui::SameLine(0, 4 * s());
    if (action(tabs[tab], sandbox_bold, sandbox_tab == tab, tabWidth, 12))
    {
      sandbox_tab = tab;
      if (tab == 2 && managed_is_home)
        analysis_panel.openNow(guiView->getController(), *engine, home_team_id,
                               away_team_id);
      if (tab != 2) analysis_panel.close();
      if (tab != 3 && sandbox->set_debugger) sandbox->set_debugger(false);
    }
  }
  if (sandbox->engine_tools_visible)
    sandbox->engine_tools_visible(sandbox_tab == 3);
  ImGui::PushFont(sandbox_regular, 13);
  if (sandbox_tab == 0)
  {
    const auto& stats = engine->getStats();
    if (ImGui::BeginTable("##sandbox_stats", 3,
                          ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_BordersInnerH |
                              ImGuiTableFlags_SizingStretchProp))
    {
      ImGui::TableSetupColumn("Home", ImGuiTableColumnFlags_WidthStretch, .8f);
      ImGui::TableSetupColumn("Statistic", ImGuiTableColumnFlags_WidthStretch,
                              1.8f);
      ImGui::TableSetupColumn("Away", ImGuiTableColumnFlags_WidthStretch, .8f);
      const auto row = [](const std::string& own, const char* label,
                          const std::string& other)
      {
        ImGui::TableNextRow();
        for (const auto& value : std::array<std::string, 3>{own, label, other})
        {
          ImGui::TableNextColumn();
          const auto at = ImGui::GetCursorScreenPos();
          const ImVec2 size(ImGui::GetContentRegionAvail().x,
                            ImGui::GetTextLineHeight());
          ImGui::Dummy(size);
          cellText(value, at, ImVec2(at.x + size.x, at.y + size.y));
        }
      };
      row(abbreviation(home_name), "", abbreviation(away_name));
      row(std::format("{:.0f}%", stats.homePossession), "Possession",
          std::format("{:.0f}%", stats.awayPossession));
      row(std::to_string(stats.homeShots), "Shots",
          std::to_string(stats.awayShots));
      row(std::to_string(stats.homeOnTarget), "On target",
          std::to_string(stats.awayOnTarget));
      row(std::format("{:.2f}", stats.homeShotXG), "xG",
          std::format("{:.2f}", stats.awayShotXG));
      row(std::format("{}/{}", stats.homePassesCompleted,
                      stats.homePassesAttempted),
          "Passes",
          std::format("{}/{}", stats.awayPassesCompleted,
                      stats.awayPassesAttempted));
      row(std::to_string(stats.homeCorners), "Corners",
          std::to_string(stats.awayCorners));
      row(std::to_string(stats.homeFouls), "Fouls",
          std::to_string(stats.awayFouls));
      row(std::to_string(stats.homeOffsides), "Offsides",
          std::to_string(stats.awayOffsides));
      row(std::to_string(stats.homeSaves), "Saves",
          std::to_string(stats.awaySaves));
      row(std::format("{} / {}", stats.homeYellowCards, stats.homeRedCards),
          "Yellow / red",
          std::format("{} / {}", stats.awayYellowCards, stats.awayRedCards));
      ImGui::EndTable();
    }
  }
  else if (sandbox_tab == 1)
  {
    ImGui::Checkbox("Show all events", &show_all_events);
    refreshVisibleEvents();
    const auto& events = engine->getEvents();
    // Fixed-height, clipped previews keep the complete event feed inexpensive.
    // The full description remains available on hover.
    const float rowHeight = ImGui::GetTextLineHeight() * 3 + 12 * s();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(visible_events.size()), rowHeight);
    while (clipper.Step())
      for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
      {
        const auto& event =
            events[visible_events[visible_events.size() - 1 -
                                  static_cast<std::size_t>(row)]];
        const auto text =
            MatchClock::minuteLabel(event.timeMinute, event.period,
                                    event.addedMinute > 0) +
            "  " + event.description;
        const auto at = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        ImGui::Dummy(
            ImVec2(width, rowHeight - ImGui::GetStyle().ItemSpacing.y));
        auto* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(
            at, ImVec2(at.x + width, at.y + rowHeight - 12 * s()), true);
        draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), at, Page::CREAM,
                      text.c_str(), nullptr, width);
        draw->PopClipRect();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text.c_str());
        draw->AddLine(ImVec2(at.x, at.y + rowHeight - 6 * s()),
                      ImVec2(at.x + width, at.y + rowHeight - 6 * s()),
                      Page::RULE);
      }
  }
  else if (sandbox_tab == 2)
  {
    if (managed_is_home)
      analysis_panel.renderEmbedded();
    else
      ImGui::TextWrapped("Analysis is available for a managed side.");
  }
  ImGui::PopFont();
  if (sandbox_tab == 3 && sandbox->on_side_panel)
  {
    ImGui::PushTextWrapPos(0);
    sandbox->on_side_panel(*engine, is_paused);
    ImGui::PopTextWrapPos();
  }
  Page::endCard(s());
}

void MatchScene::renderSandboxRoster()
{
  Page::tracked(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(),
                sandbox_bold, "ON-PITCH XI", 14, s());
  ImGui::Dummy(ImVec2(0, 24 * s()));
  ImGui::PushFont(sandbox_regular, 12);
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(s(), 4 * s()));
  if (ImGui::BeginTable("##sandbox_xi", 5,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                            ImGuiTableFlags_SizingStretchProp))
  {
    const char* headers[]{"PLAYER", "POS", "ROL", "RAT", "CON"};
    for (int col = 0; col < 5; ++col)
      ImGui::TableSetupColumn(headers[col], ImGuiTableColumnFlags_WidthStretch,
                              col == 0 ? 40.f : 15.f);
    const auto cell =
        [&](const std::string& text, const char* tooltip = nullptr,
            std::optional<ImU32> kit = std::nullopt, bool leftAligned = false)
    {
      ImGui::TableNextColumn();
      auto at = ImGui::GetCursorScreenPos();
      const float width = ImGui::GetContentRegionAvail().x, height = 20 * s();
      ImGui::Dummy(ImVec2(width, height));
      if (tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
      if (kit)
      {
        auto label = text;
        const float room = std::max(s(), width - 21 * s());
        if (ImGui::CalcTextSize(label.c_str()).x > room)
        {
          while (!label.empty() &&
                 ImGui::CalcTextSize((label + "...").c_str()).x > room)
          {
            std::size_t end = label.size() - 1;
            while (end > 0 &&
                   (static_cast<unsigned char>(label[end]) & 0xC0) == 0x80)
              --end;
            label.resize(end);
          }
          label += "...";
        }
        const float textWidth =
            std::min(ImGui::CalcTextSize(label.c_str()).x, room);
        const float groupWidth = 19 * s() + textWidth;
        const float left = at.x;
        shirt(ImVec2(left, at.y + (height - 18 * s()) * .5f), *kit);
        cellText(label, ImVec2(left + 19 * s(), at.y),
                 ImVec2(left + groupWidth, at.y + height), true);
      }
      else
        cellText(text, at, ImVec2(at.x + width, at.y + height), leftAligned);
    };
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers, 28 * s());
    ImGui::PushFont(sandbox_bold, 11);
    for (int column = 0; column < 5; ++column)
      cell(headers[column], nullptr, std::nullopt, column == 0);
    ImGui::PopFont();
    const bool home = managed_is_home.value_or(true);
    for (const auto& player : engine->getPlayers())
    {
      if (!player.player || !player.onPitch || player.isHomeTeam != home)
        continue;
      ImGui::TableNextRow(ImGuiTableRowFlags_None, 28 * s());
      cell(player.player->getLastName(), player.player->getName().c_str(),
           player.isGoalkeeper ? Page::LIME : teamColor(home));
      cell(RoleUtils::shortName(player.player->getRole()));
      const auto role =
          player.formationSlot >= 0
              ? engine->getSlotRole(
                    home, static_cast<std::size_t>(player.formationSlot))
              : TacticalRole::Standard;
      const char* roleName = LOC(Tactics::roleKey(role));
      cell(roleCode(roleName), roleName);
      const auto* rating = engine->findPlayerStats(player.player->getId());
      cell(rating ? std::format("{:.1f}", rating->rating) : "-");
      cell(std::format("{:.0f}%", player.stamina * 100));
    }
    ImGui::EndTable();
  }
  ImGui::PopStyleVar();
  ImGui::PopFont();
}

void MatchScene::renderSandboxTouchline(float width, float height)
{
  const bool debugger =
      sandbox->debugger_visible && sandbox->debugger_visible();
  // Team talks remain automatic, but use the page rather than a floating modal.
  if (play_confirm || play_menu || show_tactics || show_substitutions ||
      debugger)
  {
    beginFixed("##sandbox_right_detail",
               play_confirm || play_menu ? "PLAYER CONTROL"
               : show_tactics            ? "TACTICS"
               : show_substitutions      ? "SUBSTITUTIONS"
                                         : "DEBUGGER",
               ImVec2(width, height), sandbox_bold, play_confirm || play_menu);
    if (play_confirm)
      renderPlayConfirm();
    else if (play_menu)
      renderPlayMenu();
    else if (show_substitutions)
      renderSubstitutionsModal();
    else if (show_tactics)
      renderTacticsModal();
    else if (sandbox->on_debug_panel)
    {
      ImGui::BeginChild("##sandbox_debugger",
                        ImVec2(std::max(s(), ImGui::GetContentRegionAvail().x),
                               ImGui::GetContentRegionAvail().y),
                        ImGuiChildFlags_None,
                        ImGuiWindowFlags_HorizontalScrollbar);
      ImGui::PushTextWrapPos(0);
      sandbox->on_debug_panel(*engine, is_paused);
      ImGui::PopTextWrapPos();
      ImGui::EndChild();
    }
    Page::endCard(s());
    return;
  }
  Page::beginCard("##sandbox_touchline", "TOUCHLINE", width, Page::LIGHT, s(),
                  sandbox_bold);
  {
    ImGui::BeginDisabled(!managed_is_home || match_finished ||
                         team_talk.isOpen());
    if (action("Tactics", sandbox_bold)) showTactics(true);
    constexpr const char* subs = "Substitutions";
    next(actionWidth(subs, sandbox_bold));
    if (action(subs, sandbox_bold)) showSubstitutions(true);
    ImGui::SetNextItemWidth(-1);
    ImGui::BeginDisabled(!MatchShoutsBar::available(*engine));
    if (Page::beginCombo("##sandbox_shout", "Shouts"))
    {
      for (std::size_t index = 0; index < MatchChanges::SHOUTS.size(); ++index)
        if (ImGui::Selectable(LOC(MatchChanges::SHOUTS[index].labelKey)))
          if (auto context = touchline())
            MatchShoutsBar::shout(*context, index);
      ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::Dummy(ImVec2(0, 6 * s()));
    renderSandboxRoster();
    if (ImGui::CollapsingHeader("Delegate"))
    {
      ImGui::BeginDisabled(!managed_is_home);
      if (ImGui::Checkbox("Assistant handles substitutions",
                          &assistant_substitutions))
      {
        guiView->getController().setDutyOwner(
            Duty::Substitutions, assistant_substitutions ? DutyOwner::Assistant
                                                         : DutyOwner::Manager);
        applySubstitutionPolicy();
      }
      bool fixes = guiView->getController().getAssistantFixesLineup();
      if (ImGui::Checkbox("Assistant fixes invalid lineup", &fixes))
        guiView->getController().setAssistantFixesLineup(fixes);
      ImGui::EndDisabled();
    }
    if (!substitution_status.empty())
      ImGui::TextWrapped("%s", substitution_status.c_str());
  }
  Page::endCard(s());
}

void MatchScene::renderSandboxMatch()
{
  if (!sandbox_regular) sandbox_regular = ImGui::GetFont();
  if (!sandbox_bold) sandbox_bold = sandbox_regular;
  auto colours = Theme::palette();
  const auto colour = [](ImU32 value)
  { return ImGui::ColorConvertU32ToFloat4(value); };
  colours.background = colour(Page::PAGE);
  colours.sidebar = colour(Page::DARK);
  colours.surface = colour(Page::LIGHT);
  colours.raised = colour(Page::DARK);
  colours.border = colour(Page::RULE);
  colours.text = colour(Page::CREAM);
  colours.muted = colour(Page::MUTED);
  colours.faint = colour(Page::MUTED);
  colours.accent = colour(Page::LIME);
  colours.on_accent = colour(Page::INK);
  const Theme::ScopedPalette palette(colours);
  const Page::Style style(sandbox_regular, s());
  const auto* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, Page::PAGE);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6 * s(), 6 * s()));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
  ImGui::Begin("##sandbox_match", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoScrollWithMouse |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();
  if (!engine || quick_result.valid())
  {
    if (!engine && !lineup_problems.empty())
      renderLineupGate();
    else if (quick_result.valid())
      renderQuickResultProgress();
    else
      ImGui::TextWrapped("Could not load this match.");
    ImGui::BeginDisabled(quick_result.valid());
    if (action("Exit", sandbox_bold)) guiView->popScene();
    ImGui::EndDisabled();
    ImGui::End();
    return;
  }
  if (!play_menu && play_menu_next != PlayMenuNext::NONE)
  {
    const auto next = play_menu_next;
    play_menu_next = PlayMenuNext::NONE;
    if (next == PlayMenuNext::TACTICS)
      showTactics(true);
    else
      showSubstitutions(true);
  }
  team_talk.prepareForMatch(guiView->getController(), *engine, home_team_id,
                            away_team_id);
  bool debugger = sandbox->debugger_visible && sandbox->debugger_visible();
  if (!sandbox_ui_ready)
  {
    if (debugger) sandbox_tab = 3;
    sandbox_ui_ready = true;
  }
  const float viewAspect =
      view_mode == MatchViewMode::PITCH_2D && !sandboxTrueView()
          ? stadiumAspectRatio2D()
          : 1100.0f / 730.0f;
  const ImVec2 available = ImGui::GetContentRegionAvail();
  const float gap = 6 * s();
  const bool columns = available.x >= 1000 * s();
  const bool focus = (pitch_focus || side_panels_hidden) &&
                     !team_talk.isOpen() && !play_confirm && !play_menu &&
                     !show_tactics && !show_substitutions;
  const float budget =
      std::max(1.0f, available.y - sandbox_normal_toolbar * s() - 54 * s());
  const float normalStage = std::min(
      std::max(1.0f, available.x - 448 * s() - 2 * gap), budget * viewAspect);
  const float normalSide = (available.x - normalStage - 2 * gap) / 2;
  const bool wideRight = debugger || show_tactics || show_substitutions ||
                         play_confirm || play_menu;
  const float rightWidth =
      wideRight ? std::max(normalSide,
                           std::clamp(available.x * .34f, 480 * s(), 640 * s()))
                : normalSide;
  const float middleWidth =
      focus     ? available.x
      : columns ? available.x - normalSide - rightWidth - 2 * gap
                : available.x;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  const auto left = [&]
  {
    const float columnWidth = columns ? normalSide : (available.x - gap) / 2;
    ImGui::BeginChild(
        "##sandbox_match_left",
        ImVec2(columnWidth, columns ? available.y : available.y * .38f),
        ImGuiChildFlags_AlwaysUseWindowPadding);
    const float width = ImGui::GetContentRegionAvail().x;
    const auto subtitle = home_name + " v " + away_name;
    const auto context =
        std::string("Managing: ") +
        (managed_is_home.value_or(true) ? home_name : away_name);
    Page::header("Match Test", subtitle.c_str(), width, s(), sandbox_bold,
                 sandbox_regular, context.c_str(), true);
    Page::beginCard("##sandbox_match_actions", "", width, Page::DARK, s(),
                    sandbox_bold);
    // The action card deliberately has no heading or empty heading space.
    ImGui::SetCursorPosY(12 * s());
    const char* pause = match_finished ? "Finish"
                        : is_paused    ? "Resume"
                                       : "Pause";
    float fontSize = 13;
    const float fit = ImGui::GetContentRegionAvail().x;
    float total = actionWidth(pause, sandbox_bold, fontSize) +
                  actionWidth("Quick result", sandbox_bold, fontSize) +
                  actionWidth("Exit", sandbox_bold, fontSize) + 8 * s();
    if (total > fit) fontSize = 12;
    const float pauseWidth = actionWidth(pause, sandbox_bold, fontSize);
    const float quickWidth =
        actionWidth("Quick result", sandbox_bold, fontSize);
    const float exitWidth = actionWidth("Exit", sandbox_bold, fontSize);
    const float widthScale =
        std::min(1.f, std::max(s(), fit - 9 * s()) /
                          (pauseWidth + quickWidth + exitWidth));
    if (action(pause, sandbox_bold, true, pauseWidth * widthScale, fontSize))
    {
      if (match_finished)
        finishMatch();
      else
        is_paused = !is_paused;
    }
    ImGui::SameLine(0, 4 * s());
    ImGui::BeginDisabled(play.isActive() || match_finished);
    if (action("Quick result", sandbox_bold, false, quickWidth * widthScale,
               fontSize))
      quickResult();
    ImGui::EndDisabled();
    ImGui::SameLine(0, 4 * s());
    if (action("Exit", sandbox_bold, false, exitWidth * widthScale, fontSize))
      guiView->popScene();
    Page::endCard(s());
    if (!quick_result.valid()) renderSandboxOverview(width);
    ImGui::EndChild();
  };
  if (!focus && columns)
  {
    left();
    if (quick_result.valid())
    {
      ImGui::PopStyleVar();
      ImGui::End();
      return;
    }
    ImGui::SameLine(0, gap);
  }
  const float mainHeight = columns || focus ? available.y : available.y * .60f;
  ImGui::BeginChild(
      "##sandbox_match_stage", ImVec2(middleWidth, mainHeight),
      ImGuiChildFlags_AlwaysUseWindowPadding,
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  const float capacity = std::max(1.0f, middleWidth);
  ImGui::PushFont(sandbox_regular, 14);
  const float cameraWidth =
      ImGui::CalcTextSize("Camera:").x + 430 * s() + 5 * gap;
  ImGui::PopFont();
  const float controlWidth =
      actionWidth(play.isActive() ? "Play menu" : "Control a player",
                  sandbox_regular) +
      (play.isActive() ? actionWidth("Hand back", sandbox_regular) + gap : 0);
  float stageWidth = capacity;
  for (int pass = 0; pass < 4; ++pass)
  {
    const int rows = stageWidth >= controlWidth + gap + cameraWidth ? 1
                     : stageWidth >= cameraWidth                    ? 2
                                                                    : 3;
    stageWidth = std::min(
        capacity,
        std::max(s(), mainHeight - rows * 38 * s() - 48 * s()) * viewAspect);
  }
  const ImVec2 toolbarStart = ImGui::GetCursorScreenPos();
  if (!play.isActive())
  {
    ImGui::BeginDisabled(!canTakeControl() || match_finished ||
                         team_talk.isOpen());
    if (action("Control a player", sandbox_regular)) requestTakeControl();
    ImGui::EndDisabled();
  }
  else
  {
    if (action("Play menu", sandbox_regular)) openPlayMenu();
    next(actionWidth("Hand back", sandbox_regular));
    ImGui::BeginDisabled(!canHandBack());
    if (action("Hand back", sandbox_regular)) handBack();
    ImGui::EndDisabled();
  }
  // One font and frame height across actions and camera dropdowns.
  ImGui::PushFont(sandbox_regular, 14);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                      ImVec2(8 * s(), (32 * s() - ImGui::GetFontSize()) * .5f));
  if (ImGui::GetItemRectMax().x + gap + cameraWidth <=
      toolbarStart.x + stageWidth)
    ImGui::SameLine(std::max(0.0f, stageWidth - cameraWidth));
  else
    ImGui::SetCursorPosX(std::max(0.0f, stageWidth - cameraWidth));
  ImGui::BeginGroup();
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Camera:");
  next(66 * s(), toolbarStart.x + stageWidth);
  ImGui::SetNextItemWidth(66 * s());
  ImGui::BeginDisabled(play.isActive());
  const auto speedText = std::format("{:g}×", match_speed);
  if (Page::beginCombo("##sandbox_speed", speedText.c_str()))
  {
    for (float speed : MatchSceneTuning::Controls::SPEED_STEPS)
      if (ImGui::Selectable(std::format("{:g}×", speed).c_str(),
                            match_speed == speed))
        setPlaybackSpeed(speed);
    ImGui::EndCombo();
  }
  next(112 * s(), toolbarStart.x + stageWidth);
  ImGui::SetNextItemWidth(112 * s());
  int playback = highlights_only ? 1 : 0;
  if (Page::combo("##sandbox_playback", &playback, "Full match\0Highlights\0"))
    setHighlightsOnly(playback == 1);
  ImGui::EndDisabled();
  next(62 * s(), toolbarStart.x + stageWidth);
  ImGui::SetNextItemWidth(62 * s());
  int view = view_mode == MatchViewMode::PITCH_2D && !sandboxTrueView() ? 0 : 1;
  if (Page::combo("##sandbox_view", &view,
                  "2D\0"
                  "3D\0"))
  {
    if (view == 0 && sandbox->set_true_view) sandbox->set_true_view(false);
    setViewMode(view == 0 ? MatchViewMode::PITCH_2D
                          : MatchViewMode::BROADCAST_3D);
  }
  const bool view2D =
      view_mode == MatchViewMode::PITCH_2D && !sandboxTrueView();
  next(118 * s(), toolbarStart.x + stageWidth);
  ImGui::SetNextItemWidth(118 * s());
  constexpr std::array<std::pair<const char*, MatchCameraMode>, 7> cameras{
      {{"Broadcast", MatchCameraMode::BROADCAST},
       {"Tactical", MatchCameraMode::TACTICAL},
       {"End", MatchCameraMode::END},
       {"Player follow", MatchCameraMode::PLAYER_FOLLOW},
       {"Free", MatchCameraMode::FREE},
       {"Director", MatchCameraMode::DIRECTOR},
       {"Play", MatchCameraMode::PLAY}}};
  const char* camera = "Broadcast";
  for (const auto& [label, mode] : cameras)
    if (camera_mode == mode) camera = label;
  ImGui::BeginDisabled(view2D);
  if (Page::beginCombo("##sandbox_camera", camera))
  {
    for (const auto& [label, mode] : cameras)
      if ((mode != MatchCameraMode::PLAY || play.isActive()) &&
          ImGui::Selectable(label, camera_mode == mode))
        setCameraMode(mode);
    ImGui::EndCombo();
  }
  ImGui::EndDisabled();
  next(72 * s(), toolbarStart.x + stageWidth);
  ImGui::SetNextItemWidth(72 * s());
  ImGui::SetNextWindowSizeConstraints(ImVec2(220 * s(), 0),
                                      ImVec2(FLT_MAX, FLT_MAX));
  if (Page::beginCombo("##sandbox_more", "More", ImGuiComboFlags_HeightLargest))
  {
    if (ImGui::Selectable("Player names", show_player_names))
      show_player_names = !show_player_names;
    const bool trueView = sandboxTrueView();
    if (ImGui::Selectable("TrueView", trueView))
    {
      if (sandbox->set_true_view) sandbox->set_true_view(!trueView);
      if (!trueView)
      {
        setViewMode(MatchViewMode::BROADCAST_3D);
        sandbox_tab = 3;
      }
    }
    if (ImGui::Selectable("Pressure map", pressure_overlay))
      pressure_overlay = !pressure_overlay;
    if (ImGui::Selectable("Day look", day_look)) day_look = !day_look;
    if (ImGui::Selectable("Pitch focus", pitch_focus))
      setPitchFocus(!pitch_focus);
    if (ImGui::Selectable("Show side panels", !side_panels_hidden))
      setSidePanelsHidden(!side_panels_hidden);
    if (!view2D && camera_mode == MatchCameraMode::FREE)
      if (ImGui::Selectable("Follow ball", free_follow_ball))
        setFreeFollowBall(!free_follow_ball);
    ImGui::EndCombo();
  }
  ImGui::EndGroup();
  ImGui::PopStyleVar();
  ImGui::PopFont();
  const float toolbarHeight = ImGui::GetCursorScreenPos().y - toolbarStart.y;
  if (!wideRight) sandbox_normal_toolbar = std::max(32.0f, toolbarHeight / s());
  const float pitchHeight =
      std::max(s(), mainHeight - toolbarHeight - 48 * s());
  const float pitchWidth = std::min(stageWidth, pitchHeight * viewAspect);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  renderPitch(ImVec2(pitchWidth, pitchWidth / viewAspect));
  const MatchEngine* shown =
      sandbox->shown_engine ? sandbox->shown_engine() : nullptr;
  if (!shown) shown = engine.get();
  if (team_talk.isOpen())
  {
    ImGui::SetCursorScreenPos(origin);
    beginFixed("##sandbox_pitch_talk", "TEAM TALK",
               ImVec2(pitchWidth, pitchWidth / viewAspect), sandbox_bold);
    team_talk.renderForMatch(guiView->getController(), *engine, home_team_id,
                             away_team_id, true);
    if (!team_talk.isOpen() && !match_finished)
    {
      is_paused = false;
      paused_for_dialog = false;
    }
    Page::endCard(s());
  }
  else
  {
    const auto homeText =
        std::format("{}  {} - {}  ", abbreviation(home_name),
                    shown->getHomeScore(), shown->getAwayScore());
    auto awayText = abbreviation(away_name) + "  |  " +
                    MatchClock::clockLabel(shown->getMatchTimeMinutes(),
                                           shown->getPeriod(), false);
    if (shown->hasShootout())
      awayText += std::format(" ({}-{} pens)", shown->getShootoutScore(true),
                              shown->getShootoutScore(false));
    ImGui::PushFont(sandbox_bold, 14);
    const auto homeSize = ImGui::CalcTextSize(homeText.c_str()),
               awaySize = ImGui::CalcTextSize(awayText.c_str());
    auto* draw = ImGui::GetWindowDrawList();
    const ImVec2 at(origin.x + 12 * s(), origin.y + 12 * s());
    const float swatch = 12 * s(), gap = 6 * s(),
                height = homeSize.y + 12 * s();
    draw->AddRectFilled(
        at,
        ImVec2(at.x + homeSize.x + awaySize.x + 2 * swatch + 3 * gap + 16 * s(),
               at.y + height),
        Page::DARK);
    float x = at.x + 8 * s();
    const float y = at.y + (height - swatch) * .5f;
    const auto kit = [&](bool home)
    {
      draw->AddRectFilled(ImVec2(x, y), ImVec2(x + swatch, y + swatch),
                          teamColor(home));
      draw->AddRect(ImVec2(x, y), ImVec2(x + swatch, y + swatch), Page::CREAM);
      x += swatch + gap;
    };
    kit(true);
    draw->AddText(ImVec2(x, at.y + 6 * s()), Page::CREAM, homeText.c_str());
    x += homeSize.x + gap;
    kit(false);
    draw->AddText(ImVec2(x, at.y + 6 * s()), Page::CREAM, awayText.c_str());
    ImGui::PopFont();
  }
  ImGui::BeginChild("##sandbox_match_timeline", ImVec2(pitchWidth, 14 * s()),
                    ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
  renderTimeline();
  ImGui::EndChild();
  if (!engine->getEvents().empty())
  {
    ImGui::PushFont(sandbox_regular, 12);
    UI::textFitted(engine->getEvents().back().description, pitchWidth,
                   ImGui::ColorConvertU32ToFloat4(Page::MUTED));
    ImGui::PopFont();
  }
  ImGui::EndChild();
  if (!focus)
  {
    if (columns)
      ImGui::SameLine(0, gap);
    else
    {
      left();
      ImGui::SameLine(0, gap);
    }
    const float panel = columns ? rightWidth : (available.x - gap) / 2;
    ImGui::BeginChild("##sandbox_match_right",
                      ImVec2(panel, columns ? available.y : available.y * .38f),
                      ImGuiChildFlags_AlwaysUseWindowPadding,
                      wideRight ? ImGuiWindowFlags_NoScrollbar |
                                      ImGuiWindowFlags_NoScrollWithMouse
                                : ImGuiWindowFlags_None);
    if (!side_panels_hidden || wideRight)
      renderSandboxTouchline(std::max(s(), ImGui::GetContentRegionAvail().x),
                             columns ? available.y : available.y * .38f);
    ImGui::EndChild();
  }
  ImGui::PopStyleVar();
  const bool analysisWasOpen = analysis_panel.isOpen();
  analysis_panel.prepareForMatch(
      guiView->getController(), *engine, home_team_id, away_team_id,
      team_talk.isOpen() || show_tactics || show_substitutions);
  if (!analysisWasOpen && analysis_panel.isOpen()) sandbox_tab = 2;
  ImGui::End();

  renderSandboxTools();
}
