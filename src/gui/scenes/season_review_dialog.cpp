// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/season_review_dialog.h"

#include <fmt/printf.h>

#include <algorithm>
#include <cmath>
#include <format>

#include "controller/game_controller.h"
#include "global/language_manager.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/competition.h"
#include "model/continental.h"
#include "model/season_review.h"

namespace
{
constexpr const char* POPUP_ID = "###season_review_dialog";
constexpr float DIALOG_WIDTH = 640.0f;
constexpr float VIEWPORT_WIDTH_SHARE = 0.94f;
constexpr float VIEWPORT_HEIGHT_SHARE = 0.9f;
constexpr float KEY_WIDTH = 150.0f;

float scaled(float value) { return value * Theme::scale(); }

std::string leagueName(const GameController& controller, LeagueID league_id)
{
  const auto league = controller.getLeagueById(league_id);
  return league ? Competitions::leagueName(league->get()) : std::string("–");
}

ImVec4 gradeColor(ObjectiveGrade grade)
{
  using enum ObjectiveGrade;
  const Theme::Palette& palette = Theme::palette();
  switch (grade)
  {
    case Exceeded:
    case Met:
      return palette.positive;
    case Missed:
      return palette.warning;
    case Failed:
      break;
  }
  return palette.negative;
}

std::string youthTarget(std::uint8_t target)
{
  return LOC(BoardModel::youthTargetKey(target));
}
}  // namespace

void SeasonReviewDialog::open(const GameController& controller,
                              const SeasonReview& review)
{
  const Theme::Palette& palette = Theme::palette();
  season = review.season;
  const SeasonVerdict outcome = review.result.verdict;
  const char* headline_key = "SEASON_REVIEW_HEADLINE_SATISFIED";
  headline_color = palette.text;
  if (review.champion)
    headline_key = "SEASON_REVIEW_HEADLINE_CHAMPIONS";
  else if (review.promoted)
    headline_key = "SEASON_REVIEW_HEADLINE_PROMOTED";
  else if (review.relegated)
    headline_key = "SEASON_REVIEW_HEADLINE_RELEGATED";
  else if (outcome == SeasonVerdict::Sacked)
    headline_key = "SEASON_REVIEW_HEADLINE_SACKED";
  else if (outcome == SeasonVerdict::Warned)
    headline_key = "SEASON_REVIEW_HEADLINE_WARNED";
  else if (outcome == SeasonVerdict::Delighted)
    headline_key = "SEASON_REVIEW_HEADLINE_DELIGHTED";
  else if (outcome == SeasonVerdict::Pleased)
    headline_key = "SEASON_REVIEW_HEADLINE_PLEASED";
  headline = LOC(headline_key);
  // The celebration (or the gloom) follows the outcome, not the accent.
  if (review.champion || review.promoted ||
      outcome == SeasonVerdict::Delighted || outcome == SeasonVerdict::Pleased)
    headline_color = palette.positive;
  else if (review.relegated || outcome == SeasonVerdict::Sacked ||
           outcome == SeasonVerdict::Warned)
    headline_color = palette.negative;

  const auto club = controller.getTeamById(review.team_id);
  subtitle = fmt::sprintf(
      LOC("SEASON_REVIEW_SUBTITLE"),
      std::format("{}/{:02}", review.start_year, (review.start_year + 1) % 100),
      club ? club->get().getName() : std::string("–"),
      leagueName(controller, review.league_id));

  tiles.clear();
  tiles.emplace_back(LOC("SEASON_REVIEW_POSITION"),
                     fmt::sprintf(LOC("SEASON_REVIEW_POSITION_VALUE"),
                                  review.position, review.league_size));
  tiles.emplace_back(LOC("SEASON_REVIEW_POINTS"),
                     std::to_string(review.points));
  tiles.emplace_back(LOC("SEASON_REVIEW_RECORD"),
                     fmt::sprintf(LOC("SEASON_REVIEW_RECORD_VALUE"), review.won,
                                  review.drawn, review.lost));
  tiles.emplace_back(LOC("SEASON_REVIEW_GOALS"),
                     fmt::sprintf(LOC("SEASON_REVIEW_GOALS_VALUE"),
                                  review.goals_for, review.goals_against));

  highlights.clear();
  highlights.push_back(review.top_scorer.empty()
                           ? std::string(LOC("SEASON_REVIEW_NO_SCORER"))
                           : fmt::sprintf(LOC("SEASON_REVIEW_TOP_SCORER"),
                                          review.top_scorer.c_str(),
                                          review.top_scorer_goals));
  if (review.promoted || review.relegated)
    highlights.push_back(
        fmt::sprintf(LOC(review.promoted ? "SEASON_REVIEW_PROMOTED_TO"
                                         : "SEASON_REVIEW_RELEGATED_TO"),
                     leagueName(controller, review.next_league_id)));
  if (review.cup_won) highlights.emplace_back(LOC("SEASON_REVIEW_CUP_WON"));
  if (const Continental::CompetitionRules* rules =
          review.continental_id != 0 ? Continental::rules(review.continental_id)
                                     : nullptr)
    highlights.push_back(
        fmt::sprintf(LOC("SEASON_REVIEW_CONTINENTAL"), LOC(rules->name_key)));

  verdict = LOC(SeasonReviewModel::verdictKey(outcome));
  verdict_color = palette.positive;
  if (outcome == SeasonVerdict::Sacked || outcome == SeasonVerdict::Warned)
    verdict_color = palette.negative;
  else if (outcome == SeasonVerdict::Satisfied)
    verdict_color = palette.text;
  confidence =
      fmt::sprintf(LOC("SEASON_REVIEW_CONFIDENCE"),
                   static_cast<int>(std::lround(review.result.confidence)),
                   static_cast<int>(std::lround(review.confidence_before)));

  const auto graded = [](const std::string& target, ObjectiveGrade grade)
  {
    return fmt::sprintf(LOC("SEASON_REVIEW_GRADED"), target,
                        LOC(SeasonReviewModel::gradeKey(grade)));
  };
  grades.clear();
  grades.emplace_back(LOC("SEASON_REVIEW_OBJ_LEAGUE"),
                      graded(LOC(BoardModel::objectiveKey(review.objective)),
                             review.result.league),
                      gradeColor(review.result.league));
  grades.emplace_back(
      LOC("SEASON_REVIEW_OBJ_CUP"),
      graded(LOC(BoardModel::cupObjectiveKey(review.cup_objective)),
             review.result.cup),
      gradeColor(review.result.cup));
  grades.emplace_back(
      LOC("SEASON_REVIEW_OBJ_FINANCES"),
      graded(LOC(BoardModel::financeObjectiveKey(review.finance_objective)),
             review.result.finances),
      gradeColor(review.result.finances));
  grades.emplace_back(LOC("SEASON_REVIEW_OBJ_YOUTH"),
                      graded(fmt::sprintf(LOC("SEASON_REVIEW_YOUTH_VALUE"),
                                          youthTarget(review.youth_target),
                                          review.young_regulars),
                             review.result.youth),
                      gradeColor(review.result.youth));

  money.clear();
  money.emplace_back(LOC("SEASON_REVIEW_PRIZE"),
                     Format::money(review.prize_money));
  money.emplace_back(LOC("SEASON_REVIEW_BALANCE"),
                     Format::money(review.balance));

  next_objectives.clear();
  farewell.clear();
  if (review.next_objective)
  {
    next_objectives.emplace_back(
        LOC("SEASON_REVIEW_OBJ_LEAGUE"),
        fmt::sprintf(LOC("SEASON_REVIEW_NEXT_LEAGUE"),
                     LOC(BoardModel::objectiveKey(*review.next_objective)),
                     review.next_target));
    next_objectives.emplace_back(
        LOC("SEASON_REVIEW_OBJ_CUP"),
        LOC(BoardModel::cupObjectiveKey(review.next_cup)));
    next_objectives.emplace_back(
        LOC("SEASON_REVIEW_OBJ_FINANCES"),
        LOC(BoardModel::financeObjectiveKey(review.next_finances)));
    next_objectives.emplace_back(LOC("SEASON_REVIEW_OBJ_YOUTH"),
                                 youthTarget(review.next_youth));
  }
  else
  {
    farewell = LOC("SEASON_REVIEW_FAREWELL");
  }
  open_requested = true;
}

std::optional<std::uint16_t> SeasonReviewDialog::render()
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
  const Theme::Palette& palette = Theme::palette();
  UI::sectionLabel(LOC("SEASON_REVIEW_TITLE"));
  {
    Theme::ScopedText title(Theme::Text::TITLE);
    ImGui::TextColored(headline_color, "%s", headline.c_str());
  }
  ImGui::TextColored(palette.muted, "%s", subtitle.c_str());
  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::S)));

  UI::TileRow row(static_cast<int>(tiles.size()), 120.0f);
  for (std::size_t index = 0; index < tiles.size(); ++index)
  {
    row.next();
    UI::statTile(std::format("season_tile_{}", index).c_str(),
                 tiles[index].first.c_str(), tiles[index].second.c_str(),
                 nullptr, palette.text, row.width());
  }
  ImGui::PushTextWrapPos(0.0f);
  for (const std::string& line : highlights)
    ImGui::TextUnformatted(line.c_str());
  ImGui::PopTextWrapPos();

  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::XS)));
  UI::sectionLabel(LOC("SEASON_REVIEW_BOARD"));
  ImGui::TextColored(verdict_color, "%s", verdict.c_str());
  ImGui::TextColored(palette.muted, "%s", confidence.c_str());
  const float key_width = scaled(KEY_WIDTH);
  for (const Line& line : grades)
  {
    ImGui::PushStyleColor(ImGuiCol_Text, line.color);
    UI::keyValue(line.label.c_str(), line.value.c_str(), key_width);
    ImGui::PopStyleColor();
  }
  for (const auto& [label, value] : money)
    UI::summaryRow(label.c_str(), value.c_str());

  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::XS)));
  UI::sectionLabel(LOC("SEASON_REVIEW_NEXT"));
  if (!farewell.empty())
  {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(palette.muted, "%s", farewell.c_str());
    ImGui::PopTextWrapPos();
  }
  for (const auto& [label, value] : next_objectives)
    UI::keyValue(label.c_str(), value.c_str(), key_width);

  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::S)));
  std::optional<std::uint16_t> closed;
  if (UI::primaryButton(LOC("SEASON_REVIEW_CONTINUE")) ||
      ImGui::IsKeyPressed(ImGuiKey_Escape, false))
  {
    ImGui::CloseCurrentPopup();
    visible = false;
    closed = season;
  }
  ImGui::EndPopup();
  return closed;
}
