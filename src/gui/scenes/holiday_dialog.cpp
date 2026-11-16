// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/holiday_dialog.h"

#include <fmt/printf.h>

#include <algorithm>
#include <array>
#include <utility>

#include "controller/game_controller.h"
#include "global/language_manager.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/calendar.h"
#include "model/world_rng.h"

namespace
{
constexpr const char* POPUP_ID = "###holiday_dialog";
constexpr float DIALOG_WIDTH = 600.0f;
constexpr float VIEWPORT_WIDTH_SHARE = 0.94f;
constexpr float VIEWPORT_HEIGHT_SHARE = 0.9f;
constexpr size_t MAX_RESULT_LINES = 12;
constexpr int MIN_CRISIS = 2;
constexpr int MAX_CRISIS = 12;

float scaled(float value) { return value * Theme::scale(); }

std::string teamName(const GameController& controller, TeamID id)
{
  const auto team = controller.getTeamById(id);
  return team ? team->get().getName() : std::string("–");
}
}  // namespace

void HolidayDialog::open(GameController& controller)
{
  plan = HolidayPlan();
  plan.preferences = controller.getHolidayPreferences();
  plan.until = SeasonCalendar::addDays(controller.getCurrentDate(), 14);
  mode = static_cast<int>(std::to_underlying(plan.mode));
  bid_mode = plan.preferences.big_bid_threshold > 0 ? 1 : 0;
  bid_amount = plan.preferences.big_bid_threshold > 0
                   ? plan.preferences.big_bid_threshold
                   : 20'000'000;
  crisis_count = plan.preferences.injury_crisis_count;
  showing_summary = false;
  refreshTarget(controller);
  open_requested = true;
}

void HolidayDialog::refreshTarget(const GameController& controller)
{
  plan.mode = static_cast<HolidayMode>(mode);
  const GameDateValue today = controller.getCurrentDate();
  if (plan.mode == HolidayMode::NextDecision)
  {
    target_text = LOC("HOLIDAY_TARGET_DECISION");
    can_leave = true;
    return;
  }
  const auto target = controller.getHolidayTarget(plan);
  can_leave = target.has_value();
  if (!target)
  {
    target_text = LOC("HOLIDAY_TARGET_NONE");
    return;
  }
  const int days = dayOrdinal(*target) - dayOrdinal(today);
  target_text = fmt::sprintf(Format::plural("HOLIDAY_TARGET_DAYS", days),
                             Format::date(*target).c_str(), days);
}

void HolidayDialog::showSummary(const GameController& controller,
                                const HolidaySummary& summary)
{
  const Theme::Palette& palette = Theme::palette();
  showing_summary = true;
  open_requested = true;
  summary_title = fmt::sprintf(
      Format::plural("HOLIDAY_SUMMARY_DAYS", summary.days), summary.days,
      Format::date(summary.start).c_str(), Format::date(summary.end).c_str());
  const std::string amount =
      summary.stop_amount > 0 ? Format::money(summary.stop_amount) : "";
  summary_reason = fmt::sprintf(LOC(Holiday::stopKey(summary.reason)),
                                summary.stop_detail.c_str(), amount.c_str());
  summary_reason_color = summary.reason == HolidayStop::Completed
                             ? palette.muted
                             : palette.warning;

  results.clear();
  const size_t first = summary.results.size() > MAX_RESULT_LINES
                           ? summary.results.size() - MAX_RESULT_LINES
                           : 0;
  for (size_t index = first; index < summary.results.size(); ++index)
  {
    const HolidayResult& result = summary.results[index];
    ImVec4 color = palette.muted;
    if (result.goals_for > result.goals_against)
      color = palette.positive;
    else if (result.goals_for < result.goals_against)
      color = palette.negative;
    results.emplace_back(
        fmt::sprintf(
            LOC(result.home ? "HOLIDAY_RESULT_HOME" : "HOLIDAY_RESULT_AWAY"),
            Format::dayMonth(result.date).c_str(),
            teamName(controller, result.opponent_id).c_str(), result.goals_for,
            result.goals_against),
        color);
  }
  table_line.clear();
  if (summary.position_after > 0 && summary.played_before > 0)
    table_line = fmt::sprintf(LOC("HOLIDAY_TABLE_LINE"), summary.position_after,
                              summary.position_before, summary.points_after,
                              summary.points_before);
  else if (summary.position_after > 0)
    table_line = fmt::sprintf(LOC("HOLIDAY_TABLE_START"),
                              summary.position_after, summary.points_after);
  money_rows = {{LOC("HOLIDAY_INCOME"), Format::money(summary.income)},
                {LOC("HOLIDAY_EXPENSES"), Format::money(-summary.expenses)},
                {LOC("HOLIDAY_BALANCE"),
                 fmt::sprintf(LOC("HOLIDAY_BALANCE_VALUE"),
                              Format::money(summary.balance_after).c_str(),
                              Format::money(summary.balance_before).c_str())}};
  moves.clear();
  for (const HolidayMove& move : summary.moves)
    moves.emplace_back(
        fmt::sprintf(
            LOC(move.incoming ? "HOLIDAY_MOVE_IN" : "HOLIDAY_MOVE_OUT"),
            move.name.c_str(), teamName(controller, move.other_team_id).c_str(),
            Format::money(move.fee).c_str()),
        move.incoming ? palette.text : palette.muted);
  injuries.clear();
  for (const HolidayInjury& injury : summary.injuries)
    injuries.emplace_back(
        fmt::sprintf(Format::plural("HOLIDAY_INJURY_LINE", injury.days),
                     injury.name.c_str(), LOC(injury.injury_key.c_str()),
                     injury.days),
        injury.key_player ? palette.warning : palette.text);
  filed_line = summary.messages_filed > 0
                   ? fmt::sprintf(Format::plural("HOLIDAY_FILED",
                                                 summary.messages_filed),
                                  summary.messages_filed)
                   : std::string();
}

std::optional<HolidayPlan> HolidayDialog::render(GameController& controller)
{
  if (open_requested)
  {
    open_requested = false;
    ImGui::OpenPopup(POPUP_ID);
  }
  visible = ImGui::IsPopupOpen(POPUP_ID);
  if (!visible) return std::nullopt;

  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const float width = std::min(scaled(DIALOG_WIDTH),
                               viewport->WorkSize.x * VIEWPORT_WIDTH_SHARE);
  ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSizeConstraints(
      ImVec2(width, 0.0f),
      ImVec2(width, viewport->WorkSize.y * VIEWPORT_HEIGHT_SHARE));
  if (!ImGui::BeginPopupModal(POPUP_ID, nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize |
                                  ImGuiWindowFlags_NoTitleBar |
                                  ImGuiWindowFlags_NoSavedSettings))
  {
    visible = false;
    return std::nullopt;
  }
  std::optional<HolidayPlan> chosen;
  if (showing_summary)
    renderSummary();
  else
    chosen = renderPlan(controller);
  if (chosen)
  {
    ImGui::CloseCurrentPopup();
    visible = false;
  }
  ImGui::EndPopup();
  return chosen;
}

std::optional<HolidayPlan> HolidayDialog::renderPlan(GameController& controller)
{
  const Theme::Palette& palette = Theme::palette();
  HolidayPreferences& rules = plan.preferences;
  {
    Theme::ScopedText heading(Theme::Text::TITLE);
    ImGui::TextUnformatted(LOC("HOLIDAY_TITLE"));
  }
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(palette.muted, "%s", LOC("HOLIDAY_EXPLAIN"));
  ImGui::PopTextWrapPos();
  ImGui::Separator();

  UI::sectionLabel(LOC("HOLIDAY_HOW_LONG"));
  if (const std::array<const char*, 4> modes =
          {LOC("HOLIDAY_MODE_UNTIL_DATE"), LOC("HOLIDAY_MODE_NEXT_MATCH"),
           LOC("HOLIDAY_MODE_NEXT_DECISION"), LOC("HOLIDAY_MODE_WINDOW_END")};
      UI::segmented("##holiday_mode", mode, modes,
                    ImGui::GetContentRegionAvail().x))
    refreshTarget(controller);
  if (plan.mode == HolidayMode::UntilDate)
  {
    const GameDateValue today = controller.getCurrentDate();
    const auto step = [&](const char* label, int days)
    {
      ImGui::PushID(days);
      const bool pressed =
          UI::secondaryButton(label, ImVec2(0, 0), UI::ButtonSize::COMPACT);
      ImGui::PopID();
      if (!pressed) return;
      GameDateValue next = SeasonCalendar::addDays(plan.until, days);
      const GameDateValue earliest = SeasonCalendar::addDays(today, 1);
      const GameDateValue latest = SeasonCalendar::addDays(today, 366);
      if (next < earliest) next = earliest;
      if (latest < next) next = latest;
      plan.until = next;
      refreshTarget(controller);
    };
    step("-7", -7);
    ImGui::SameLine();
    step("-1", -1);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    {
      Theme::ScopedText strong(Theme::Text::TITLE);
      ImGui::TextUnformatted(Format::date(plan.until).c_str());
    }
    ImGui::SameLine();
    step("+1", 1);
    ImGui::SameLine();
    step("+7", 7);
    UI::sameLineIfFits(UI::buttonWidth("+30", UI::ButtonSize::COMPACT));
    step("+30", 30);
  }
  ImGui::TextColored(can_leave ? palette.text : palette.warning, "%s",
                     target_text.c_str());

  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::XS)));
  UI::sectionLabel(LOC("HOLIDAY_STOP_EARLY"));
  ImGui::Checkbox(LOC("HOLIDAY_RULE_BID"), &rules.stop_big_bid);
  if (rules.stop_big_bid)
  {
    ImGui::Indent(scaled(Theme::Space::L));
    const std::array<const char*, 2> bids = {LOC("HOLIDAY_BID_KEY_PLAYERS"),
                                             LOC("HOLIDAY_BID_AMOUNT")};
    UI::segmented("##bid_mode", bid_mode, bids,
                  std::min(scaled(360.0f), ImGui::GetContentRegionAvail().x));
    if (bid_mode == 1)
      UI::moneyInput("##bid_amount", bid_amount,
                     {.minimum = 100'000,
                      .width = std::min(scaled(240.0f),
                                        ImGui::GetContentRegionAvail().x)});
    ImGui::Unindent(scaled(Theme::Space::L));
  }
  ImGui::Checkbox(LOC("HOLIDAY_RULE_KEY_INJURY"), &rules.stop_key_injury);
  ImGui::Checkbox(LOC("HOLIDAY_RULE_CRISIS"), &rules.stop_injury_crisis);
  if (rules.stop_injury_crisis)
  {
    ImGui::SameLine();
    ImGui::SetNextItemWidth(scaled(150.0f));
    ImGui::SliderInt("##crisis", &crisis_count, MIN_CRISIS, MAX_CRISIS,
                     LOC("HOLIDAY_CRISIS_COUNT"));
  }
  ImGui::Checkbox(LOC("HOLIDAY_RULE_WARNING"), &rules.stop_sacking_warning);
  ImGui::TextColored(palette.faint, "%s", LOC("HOLIDAY_DISMISSAL_NOTE"));

  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::XS)));
  UI::sectionLabel(LOC("HOLIDAY_ASSISTANT"));
  ImGui::Checkbox(LOC("HOLIDAY_ASSISTANT_LINEUP"), &rules.assistant_lineup);
  ImGui::Checkbox(LOC("HOLIDAY_ASSISTANT_TRAINING"), &rules.assistant_training);
  ImGui::Checkbox(LOC("HOLIDAY_ASSISTANT_INBOX"), &rules.assistant_inbox);

  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::S)));
  ImGui::BeginDisabled(!can_leave);
  const bool leave = UI::primaryButton(LOC("HOLIDAY_GO"));
  ImGui::EndDisabled();
  ImGui::SameLine();
  const bool cancel = UI::secondaryButton(LOC("HOLIDAY_CANCEL")) ||
                      ImGui::IsKeyPressed(ImGuiKey_Escape, false);
  rules.big_bid_threshold = bid_mode == 1 ? bid_amount : 0;
  rules.injury_crisis_count = static_cast<std::uint8_t>(crisis_count);
  if (cancel)
  {
    controller.setHolidayPreferences(rules);
    ImGui::CloseCurrentPopup();
    visible = false;
    return std::nullopt;
  }
  if (!leave) return std::nullopt;
  controller.setHolidayPreferences(rules);
  return plan;
}

void HolidayDialog::renderSummary()
{
  const Theme::Palette& palette = Theme::palette();
  {
    Theme::ScopedText heading(Theme::Text::TITLE);
    ImGui::TextUnformatted(LOC("HOLIDAY_SUMMARY_TITLE"));
  }
  ImGui::TextColored(palette.muted, "%s", summary_title.c_str());
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(summary_reason_color, "%s", summary_reason.c_str());
  ImGui::PopTextWrapPos();
  ImGui::Separator();

  UI::sectionLabel(LOC("HOLIDAY_RESULTS"));
  if (results.empty())
    ImGui::TextColored(palette.faint, "%s", LOC("HOLIDAY_NO_RESULTS"));
  for (const Line& line : results)
    ImGui::TextColored(line.color, "%s", line.text.c_str());
  if (!table_line.empty()) ImGui::TextUnformatted(table_line.c_str());

  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::XS)));
  UI::sectionLabel(LOC("HOLIDAY_FINANCES"));
  for (const auto& [label, value] : money_rows)
    UI::summaryRow(label.c_str(), value.c_str());

  if (!moves.empty())
  {
    ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::XS)));
    UI::sectionLabel(LOC("HOLIDAY_TRANSFERS"));
    for (const Line& line : moves)
      UI::textFitted(line.text, ImGui::GetContentRegionAvail().x, line.color);
  }
  if (!injuries.empty())
  {
    ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::XS)));
    UI::sectionLabel(LOC("HOLIDAY_INJURIES"));
    for (const Line& line : injuries)
      UI::textFitted(line.text, ImGui::GetContentRegionAvail().x, line.color);
  }
  if (!filed_line.empty())
  {
    ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::XS)));
    ImGui::TextColored(palette.faint, "%s", filed_line.c_str());
  }
  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::S)));
  if (UI::primaryButton(LOC("TALK_CLOSE")) ||
      ImGui::IsKeyPressed(ImGuiKey_Escape, false))
  {
    ImGui::CloseCurrentPopup();
    visible = false;
    showing_summary = false;
  }
}
