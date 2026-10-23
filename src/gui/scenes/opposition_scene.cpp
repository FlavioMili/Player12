// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/opposition_scene.h"

#include <fmt/printf.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <format>
#include <utility>

#include "global/language_manager.h"
#include "gui/gui_view.h"
#include "gui/scenes/onboarding_overlay.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/role_utils.h"

namespace
{
constexpr float TWO_COLUMN_MIN_WIDTH = 860.0f;
constexpr float INSTRUCTION_WIDTH = 190.0f;

void bullet(const std::string& text, const ImVec4& color)
{
  const float size = ImGui::GetTextLineHeight();
  const ImVec2 start = ImGui::GetCursorScreenPos();
  ImGui::GetWindowDrawList()->AddCircleFilled(
      ImVec2(start.x + size * 0.35f, start.y + size * 0.5f),
      3.0f * Theme::scale(), Theme::toU32(color));
  ImGui::SetCursorScreenPos(ImVec2(start.x + size, start.y));
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextUnformatted(text.c_str());
  ImGui::PopTextWrapPos();
}

void note(const std::string& text)
{
  Theme::ScopedText small(Theme::Text::SMALL);
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(Theme::palette().muted, "%s", text.c_str());
  ImGui::PopTextWrapPos();
}

std::string decimal(float value) { return std::format("{:.2f}", value); }

/**
 * Counter-tactics applied for the coming fixture. Kept for the session, not
 * per screen instance: coming back to the report must not offer to apply
 * the same shift a second time.
 */
struct AppliedCounters
{
  TeamID opponent = 0;
  GameDateValue date;
  std::vector<bool> applied;
};
AppliedCounters applied_counters;
}  // namespace

OppositionScene::OppositionScene(GUIView* parent) : ManagementScene(parent) {}

void OppositionScene::refresh()
{
  GameController& controller = guiView->getController();
  fixture = controller.getNextManagedFixture();
  instructions.clear();
  applied.clear();
  if (!fixture) return;
  const auto team = controller.getTeamById(fixture->opponent);
  opponent_name = team ? team->get().getName() : std::string();
  report = controller.getOppositionReport(fixture->opponent);
  controller.markOppositionReportViewed(fixture->opponent);
  for (const OppositionPlayer& player : report.likely_xi)
    instructions.push_back(
        controller.getOppositionInstruction(fixture->opponent, player.player));
  if (applied_counters.opponent != fixture->opponent ||
      !(applied_counters.date == fixture->date) ||
      applied_counters.applied.size() != report.counters.size())
    applied_counters = {fixture->opponent, fixture->date,
                        std::vector<bool>(report.counters.size(), false)};
  applied = applied_counters.applied;
}

void OppositionScene::renderContent()
{
  if (!fixture)
  {
    UI::pageHeader(LOC("OPPOSITION_TITLE"));
    UI::emptyState(LOC("OPPOSITION_NO_FIXTURE"), nullptr);
    return;
  }
  const std::string subtitle =
      fmt::sprintf(LOC("OPPOSITION_SUBTITLE"), opponent_name.c_str(),
                   Format::date(fixture->date).c_str(),
                   LOC(fixture->home ? "FIXTURE_HOME" : "FIXTURE_AWAY"),
                   static_cast<int>(report.confidence));
  UI::pageHeader(LOC("OPPOSITION_TITLE"), subtitle.c_str());
  renderSummary();

  const float available = ImGui::GetContentRegionAvail().x;
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  if (available >= TWO_COLUMN_MIN_WIDTH * Theme::scale())
  {
    const float left = std::floor((available - gap) * 0.52f);
    const float right = available - gap - left;
    ImGui::BeginGroup();
    renderCounters(left);
    renderLikelyXi(left);
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    renderProfile(right);
    renderKeyPlayers(right);
    ImGui::EndGroup();
  }
  else
  {
    renderCounters(available);
    renderProfile(available);
    renderKeyPlayers(available);
    renderLikelyXi(available);
  }
}

void OppositionScene::renderSummary() const
{
  const Theme::Palette& palette = Theme::palette();
  const OppositionAverages& recent = report.recent;
  const OppositionAverages& league = report.league;
  int points = 0;
  for (const int result : report.form) points += result > 0 ? 3 : result == 0;
  UI::TileRow row(4);
  row.next();
  const std::string form = report.form.empty()
                               ? std::string("–")
                               : fmt::sprintf(LOC("OPPOSITION_FORM_VALUE"),
                                              points, 3 * report.form.size());
  const std::string formNote =
      fmt::sprintf(LOC("OPPOSITION_FORM_NOTE"), report.form.size());
  UI::statTile("opp_form", LOC("OPPOSITION_FORM"), form.c_str(),
               formNote.c_str(), palette.text, row.width());
  row.next();
  const std::string goals =
      recent.matches > 0 ? std::format("{:.1f} – {:.1f}", recent.goals_for,
                                       recent.goals_against)
                         : std::string("–");
  UI::statTile("opp_goals", LOC("OPPOSITION_GOALS"), goals.c_str(),
               LOC("OPPOSITION_PER_MATCH"), palette.text, row.width());
  row.next();
  const std::string xg = recent.matches > 0 ? decimal(recent.xg_for) + " – " +
                                                  decimal(recent.xg_against)
                                            : std::string("–");
  const std::string xgNote =
      fmt::sprintf(LOC("OPPOSITION_LEAGUE_XG"), decimal(league.xg_for).c_str());
  UI::statTile("opp_xg", LOC("OPPOSITION_XG"), xg.c_str(), xgNote.c_str(),
               palette.text, row.width());
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("%s", LOC("OPPOSITION_XG_HELP"));
  row.next();
  const std::string possession = recent.matches > 0
                                     ? std::format("{:.0f}%", recent.possession)
                                     : std::string("–");
  const std::string passNote = fmt::sprintf(
      LOC("OPPOSITION_PASSING_NOTE"), static_cast<int>(recent.pass_completion),
      static_cast<int>(league.pass_completion));
  UI::statTile("opp_possession", LOC("OPPOSITION_POSSESSION"),
               possession.c_str(), passNote.c_str(), palette.text, row.width());
}

void OppositionScene::renderCounters(float width)
{
  GameController& controller = guiView->getController();
  UI::beginAutoHeightCard("opp_counters", LOC("OPPOSITION_COUNTERS"), width);
  for (std::size_t index = 0; index < report.counters.size(); ++index)
  {
    const CounterTactic& counter = report.counters[index];
    ImGui::PushID(static_cast<int>(index));
    if (index > 0) ImGui::Separator();
    {
      Theme::ScopedText title(Theme::Text::TITLE);
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextUnformatted(GuidanceUI::text(counter.action).c_str());
      ImGui::PopTextWrapPos();
    }
    note(GuidanceUI::text(counter.reason));
    const StrategySliders& shift = counter.shift;
    if (const bool actionable =
            counter.mark != 0 || shift.pressing != 0.0f ||
            shift.riskTaking != 0.0f || shift.offensiveBias != 0.0f ||
            shift.widthUsage != 0.0f || shift.compactness != 0.0f;
        actionable)
    {
      ImGui::BeginDisabled(applied[index]);
      if (UI::secondaryButton(
              LOC(applied[index] ? "OPPOSITION_APPLIED" : "OPPOSITION_APPLY"),
              ImVec2(0.0f, 0.0f), UI::ButtonSize::COMPACT) &&
          controller.applyCounterTactic(counter))
      {
        applied[index] = true;
        applied_counters.applied = applied;
        for (std::size_t row = 0; row < report.likely_xi.size(); ++row)
          instructions[row] = controller.getOppositionInstruction(
              fixture->opponent, report.likely_xi[row].player);
        showToast(LOC("OPPOSITION_APPLIED_TOAST"));
      }
      ImGui::EndDisabled();
    }
    ImGui::PopID();
  }
  if (UI::link(LOC("OPPOSITION_OPEN_TACTICS"), "open_tactics"))
    Navigation::open(guiView, NavSection::TACTICS);
  UI::endCard();
}

void OppositionScene::renderProfile(float width) const
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("opp_profile", LOC("OPPOSITION_PROFILE"), width);
  if (!report.enough_data)
  {
    note(
        fmt::sprintf(LOC("OPPOSITION_PROFILE_NO_DATA"), report.recent.matches));
    UI::endCard();
    return;
  }
  UI::sectionLabel(LOC("OPPOSITION_STRENGTHS"));
  if (report.strengths.empty()) note(LOC("OPPOSITION_NOTHING_STANDS_OUT"));
  for (const AnalysisLine& line : report.strengths)
    bullet(GuidanceUI::text(line), palette.warning);
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
  UI::sectionLabel(LOC("OPPOSITION_WEAKNESSES"));
  if (report.weaknesses.empty()) note(LOC("OPPOSITION_NOTHING_STANDS_OUT"));
  for (const AnalysisLine& line : report.weaknesses)
    bullet(GuidanceUI::text(line), palette.positive);
  note(fmt::sprintf(LOC("OPPOSITION_PROFILE_NOTE"), report.recent.matches,
                    report.league.matches));
  UI::endCard();
}

void OppositionScene::renderKeyPlayers(float width)
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("opp_key", LOC("OPPOSITION_KEY_PLAYERS"), width);
  if (report.key_players.empty()) note(LOC("OPPOSITION_KEY_NONE"));
  for (std::size_t index = 0; index < report.key_players.size(); ++index)
  {
    const KeyOpponent& key = report.key_players[index];
    ImGui::PushID(static_cast<int>(index));
    if (UI::link(key.player.name.c_str(), "key_player"))
      Navigation::openPlayer(guiView, key.player.player);
    ImGui::SameLine();
    ImGui::TextColored(palette.faint, "%s",
                       RoleUtils::shortName(key.player.role));
    note(GuidanceUI::text(key.reason));
    ImGui::PopID();
  }
  UI::endCard();
}

void OppositionScene::renderLikelyXi(float width)
{
  const Theme::Palette& palette = Theme::palette();
  const std::string title =
      report.formation.empty() ? std::string(LOC("OPPOSITION_LIKELY_XI"))
                               : fmt::sprintf(LOC("OPPOSITION_LIKELY_XI_SHAPE"),
                                              report.formation.c_str());
  UI::beginAutoHeightCard("opp_xi", title.c_str(), width);
  static const std::array<UI::Column, 4> COLUMNS = {{
      {"OPPOSITION_COL_PLAYER", 0.0f, 0},
      {"OPPOSITION_COL_ESTIMATE", 70.0f, 1},
      {"OPPOSITION_COL_FOOT", 56.0f, 2},
      {"OPPOSITION_COL_INSTRUCTION", INSTRUCTION_WIDTH, 0},
  }};
  std::array<UI::Column, 4> columns{};
  for (std::size_t index = 0; index < columns.size(); ++index)
  {
    columns[index] = COLUMNS[index];
    columns[index].label = LOC(COLUMNS[index].label);
  }
  const UI::ColumnMask mask =
      UI::fitColumns(columns, ImGui::GetContentRegionAvail().x, 120.0f);
  std::array<const char*, static_cast<std::size_t>(
                              std::to_underlying(OppositionInstruction::COUNT))>
      labels{};
  for (std::size_t index = 0; index < labels.size(); ++index)
    labels[index] = LOC(
        oppositionInstructionKey(static_cast<OppositionInstruction>(index)));
  if (UI::beginResponsiveTable("opp_xi_table", columns, mask,
                               ImGuiTableFlags_RowBg))
  {
    for (std::size_t index = 0; index < report.likely_xi.size(); ++index)
    {
      const OppositionPlayer& player = report.likely_xi[index];
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::PushID(static_cast<int>(index));
      // Links have no frame: centre them on the instruction combo's row.
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
                           ImGui::GetStyle().FramePadding.y);
      if (UI::link(player.name.c_str(), "player"))
        Navigation::openPlayer(guiView, player.player);
      ImGui::SameLine();
      ImGui::TextColored(palette.faint, "%s",
                         RoleUtils::shortName(player.role));
      if (UI::cell(mask, 1))
      {
        ImGui::AlignTextToFramePadding();
        const std::string estimate =
            player.estimate >= 0.0f ? std::format("{:.0f}", player.estimate)
                                    : std::string("?");
        UI::textRight(estimate.c_str());
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
          ImGui::SetTooltip("%s",
                            fmt::sprintf(LOC("OPPOSITION_ESTIMATE_HELP"),
                                         static_cast<int>(player.knowledge))
                                .c_str());
      }
      if (UI::cell(mask, 2))
      {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(LOC(player.right_footed
                                       ? "OPPOSITION_FOOT_RIGHT"
                                       : "OPPOSITION_FOOT_LEFT"));
      }
      if (UI::cell(mask, 3))
      {
        auto selected =
            static_cast<int>(std::to_underlying(instructions[index]));
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::Combo("##instruction", &selected, labels.data(),
                         static_cast<int>(labels.size())))
        {
          instructions[index] = static_cast<OppositionInstruction>(selected);
          guiView->getController().setOppositionInstruction(
              fixture->opponent, player.player, instructions[index]);
        }
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  note(
      fmt::sprintf(LOC("OPPOSITION_INSTRUCTIONS_NOTE"), opponent_name.c_str()));
  UI::endCard();
}
