// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/club_scene.h"

#include <fmt/printf.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <format>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/language_manager.h"
#include "gui/gui_view.h"
#include "gui/view_models/competition_view.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/board.h"
#include "model/competition.h"
#include "model/game.h"
#include "model/season_history.h"
#include "model/supporters.h"

namespace
{
// Season history columns (unscaled widths); priority 0 never hides.
const std::array<UI::Column, 6>& historyColumns()
{
  static const std::array<UI::Column, 6> columns = {{
      {"CLUB_COL_SEASON", 70.0f, 0},
      {"CLUB_COL_COMPETITION", 170.0f, 1},
      {"CLUB_COL_CHAMPION", 0.0f, 0},
      {"CLUB_COL_RUNNER_UP", 170.0f, 2},
      {"CLUB_COL_TOP_SCORER", 200.0f, 3},
      {"CLUB_COL_MOVEMENTS", 280.0f, 4},
  }};
  return columns;
}

constexpr float TWO_COLUMN_MIN_WIDTH = 860.0f;
constexpr float KEY_WIDTH = 170.0f;
constexpr int MAX_TICKET_PRICE = 1000;
/** Reasons behind the supporters' mood shown on the card. */
constexpr std::size_t SUPPORTER_REASONS_SHOWN = 3;

std::string teamName(const GameController& controller, TeamID id)
{
  if (id == 0) return "–";
  const auto team = controller.getTeamById(id);
  return team ? team->get().getName() : std::string("–");
}

std::string joinTeams(const GameController& controller,
                      const std::vector<TeamID>& teams)
{
  std::string joined;
  for (const TeamID id : teams)
  {
    if (!joined.empty()) joined += ", ";
    joined += teamName(controller, id);
  }
  return joined;
}
}  // namespace

ClubScene::ClubScene(GUIView* parent) : ManagementScene(parent) {}

void ClubScene::update(float /*deltaTime*/)
{
  // Club data changes through controller actions; refresh() rebuilds the
  // historical and confidence caches when the scene is entered or resumed.
}

void ClubScene::refresh()
{
  GameController& controller = guiView->getController();
  const auto managed = controller.getManagedTeam();
  history.clear();
  confidence_trend.clear();
  supporters_index.reset();
  supporter_reasons.clear();
  if (!managed) return;
  const Team& club = managed->get();

  const auto table =
      CompetitionView::buildStandings(controller, club.getLeagueId());
  const auto row =
      std::ranges::find_if(table, [&club](const auto& standing)
                           { return standing.team_id == club.getId(); });
  league_position =
      row != table.end()
          ? static_cast<int>(std::distance(table.begin(), row)) + 1
          : 0;

  // Board deltas are stored newest first; the chart reads left to right.
  const BoardState& board = controller.getBoardState();
  for (size_t index = board.result_count; index > 0; --index)
    confidence_trend.push_back(board.recent_deltas[index - 1]);

  if (const Game* game = controller.getGame();
      game && game->getSupporters().team() == club.getId())
  {
    const SupporterMood& mood = game->getSupporters().mood();
    supporters_index = mood.index;
    for (const SupporterReason& reason : mood.reasons)
    {
      if (supporter_reasons.size() == SUPPORTER_REASONS_SHOWN) break;
      supporter_reasons.push_back(
          {LOC(SupporterModel::reasonKey(reason)), reason.points});
    }
  }

  ticket_price_input = static_cast<int>(club.getProfile().ticket_price);
  fair_ticket_price = controller.getFairTicketPrice(club.getId());
  last_attendance = controller.getLastHomeAttendance(club.getId());

  // Board targets beyond the league finish, with how they are going.
  board_targets = controller.getBoardTargets();

  // Roll of honour: the leagues the club played in (from the archived final
  // tables, so promotion or relegation keeps its past), its national cup
  // and continental finals it reached, newest first.
  const auto data = controller.getGameData();
  const LeagueID cup =
      data ? Competitions::rootLeague(*data, club.getLeagueId()) : 0;
  const TeamID club_id = club.getId();
  const auto& entries = controller.getSeasonHistory();
  for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry)
  {
    std::optional<uint16_t> finish;
    bool relevant = false;
    if (entry->competition_type == MatchType::LEAGUE)
    {
      if (const auto placing =
              controller.getArchivedPlacing(entry->season, club_id))
      {
        relevant = placing->first == entry->competition_id;
        if (relevant) finish = placing->second;
      }
      else if (controller.getSeasonStartYear(entry->season) == 0)
      {
        // Seasons from before the tables were archived.
        relevant = entry->competition_id == club.getLeagueId() ||
                   entry->champion_id == club_id ||
                   std::ranges::contains(entry->promoted, club_id) ||
                   std::ranges::contains(entry->relegated, club_id);
      }
    }
    else if (entry->competition_type == MatchType::CUP)
      relevant = entry->competition_id == cup;
    else
      relevant =
          entry->champion_id == club_id || entry->runner_up_id == club_id;
    if (!relevant) continue;
    HistoryRow line;
    line.season = std::format("{}/{:02}", entry->start_year,
                              (entry->start_year + 1) % 100);
    // Named in the current language; the saved name is the fallback.
    line.competition = entry->competition_name;
    if (entry->competition_type == MatchType::CUP)
      line.competition = controller.getCupName(cup);
    else if (entry->competition_type == MatchType::LEAGUE)
      if (const auto league = controller.getLeagueById(entry->competition_id))
        line.competition = Competitions::leagueName(league->get());
    if (finish.has_value())
      line.competition =
          fmt::sprintf(LOC("CLUB_HISTORY_FINISH"), line.competition, *finish);
    line.champion = teamName(controller, entry->champion_id);
    line.runner_up = teamName(controller, entry->runner_up_id);
    if (entry->top_scorer_id != 0 && data)
    {
      const auto scorer = data->getPlayer(entry->top_scorer_id);
      line.top_scorer = std::format(
          "{} ({})", scorer ? scorer->get().getName() : std::string("–"),
          entry->top_scorer_goals);
    }
    if (!entry->promoted.empty())
      line.movements += fmt::sprintf(LOC("CLUB_PROMOTED"),
                                     joinTeams(controller, entry->promoted));
    if (!entry->relegated.empty())
    {
      if (!line.movements.empty()) line.movements += "  ·  ";
      line.movements += fmt::sprintf(LOC("CLUB_RELEGATED"),
                                     joinTeams(controller, entry->relegated));
    }
    history.emplace_back(std::move(line));
  }
}

void ClubScene::renderContent()
{
  GameController& controller = guiView->getController();
  const auto managed = controller.getManagedTeam();
  if (!managed) return;
  const Team& club = managed->get();
  const Theme::Palette& palette = Theme::palette();
  const BoardState& board = controller.getBoardState();
  const ClubProfile& profile = club.getProfile();

  // Data-pack identity: nickname and founding year lead the subtitle.
  const ClubIdentity* identity = controller.getClubIdentity(club.getId());
  std::string subtitle = LOC("CLUB_SUBTITLE");
  if (identity != nullptr && identity->founded > 0)
    subtitle = fmt::sprintf(LOC("CLUB_FOUNDED_VALUE"), identity->founded) +
               "  ·  " + subtitle;
  if (identity != nullptr && !identity->nickname.empty())
    subtitle = identity->nickname + "  ·  " + subtitle;
  UI::pageHeader(club.getName().c_str(), subtitle.c_str());

  const float gap = ImGui::GetStyle().ItemSpacing.x;
  UI::TileRow tiles(4);
  const float tile = tiles.width();
  const std::string confidence =
      std::format("{:.0f}%", static_cast<double>(board.confidence));
  ImVec4 confidenceColor = palette.negative;
  if (board.confidence >= 60.0f)
    confidenceColor = palette.positive;
  else if (board.confidence >= 35.0f)
    confidenceColor = palette.warning;
  tiles.next();
  UI::statTile("confidence", LOC("CLUB_TILE_CONFIDENCE"), confidence.c_str(),
               LOC(board.dismissed ? "CLUB_DISMISSED" : "CLUB_CONFIDENCE_NOTE"),
               confidenceColor, tile);
  tiles.next();
  const std::string target =
      fmt::sprintf(LOC("CLUB_TARGET_POSITION"), board.target_position);
  UI::statTile("objective", LOC("CLUB_TILE_OBJECTIVE"),
               LOC(BoardModel::objectiveKey(board.objective)), target.c_str(),
               palette.text, tile);
  tiles.next();
  const std::string reputation = std::format("{} / 100", profile.reputation);
  UI::statTile("reputation", LOC("CLUB_TILE_REPUTATION"), reputation.c_str(),
               LOC("CLUB_REPUTATION_NOTE"), palette.text, tile);
  tiles.next();
  const std::string capacity = Format::thousands(profile.stadium_capacity);
  UI::statTile("stadium", LOC("CLUB_TILE_STADIUM"), capacity.c_str(),
               identity != nullptr && !identity->stadium_name.empty()
                   ? identity->stadium_name.c_str()
                   : LOC("CLUB_STADIUM_NOTE"),
               palette.text, tile);

  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
  const float available = ImGui::GetContentRegionAvail().x;
  const bool twoColumns = available >= TWO_COLUMN_MIN_WIDTH * Theme::scale();
  const float half =
      twoColumns ? std::floor((available - gap) * 0.5f) : available;
  // Both cards size to their content: nothing clips at any scale.
  renderBoard(half);
  if (twoColumns) ImGui::SameLine();
  const float right = twoColumns ? available - gap - half : available;
  ImGui::BeginGroup();
  renderStadium(right);
  renderSupporters(right);
  ImGui::EndGroup();
  renderHistory();
}

void ClubScene::renderBoard(float width)
{
  const Theme::Palette& palette = Theme::palette();
  const BoardState& board = guiView->getController().getBoardState();
  UI::beginAutoHeightCard("club_board", LOC("CLUB_BOARD"), width);
  const float keyWidth = KEY_WIDTH * Theme::scale();
  UI::keyValue(LOC("CLUB_OBJECTIVE"),
               LOC(BoardModel::objectiveKey(board.objective)), keyWidth);
  const std::string expected =
      board.expected_position > 0
          ? fmt::sprintf(LOC("CLUB_POSITION_VALUE"), board.expected_position)
          : std::string("–");
  UI::keyValue(LOC("CLUB_EXPECTED_POSITION"), expected.c_str(), keyWidth);
  const std::string target =
      fmt::sprintf(LOC("CLUB_POSITION_VALUE"), board.target_position);
  UI::keyValue(LOC("CLUB_WORST_ACCEPTABLE"), target.c_str(), keyWidth);
  const std::string current =
      league_position > 0
          ? fmt::sprintf(LOC("CLUB_POSITION_VALUE"), league_position)
          : std::string("–");
  const bool onTarget =
      league_position > 0 && league_position <= board.target_position;
  ImVec4 positionColor = palette.negative;
  if (league_position == 0)
    positionColor = palette.text;
  else if (onTarget)
    positionColor = palette.positive;
  ImGui::PushStyleColor(ImGuiCol_Text, positionColor);
  UI::keyValue(LOC("CLUB_CURRENT_POSITION"), current.c_str(), keyWidth);
  ImGui::PopStyleColor();

  ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * Theme::scale()));
  const std::string confidence =
      std::format("{:.0f}%", static_cast<double>(board.confidence));
  ImVec4 confidenceColor = palette.negative;
  if (board.confidence >= 60.0f)
    confidenceColor = palette.positive;
  else if (board.confidence >= 35.0f)
    confidenceColor = palette.warning;
  UI::meter(LOC("CLUB_CONFIDENCE"), board.confidence / 100.0f, keyWidth,
            confidenceColor, confidence.c_str());
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * Theme::scale()));
  UI::sectionLabel(LOC("CLUB_RESULTS_VS_EXPECTATION"));
  if (confidence_trend.empty())
  {
    ImGui::TextColored(palette.faint, "%s", LOC("CLUB_NO_LEAGUE_RESULTS"));
  }
  else
  {
    UI::deltaBars(
        "board_trend", confidence_trend,
        ImVec2(ImGui::GetContentRegionAvail().x, 36.0f * Theme::scale()));
  }
  if (board.low_reviews > 0 && !board.dismissed)
    ImGui::TextColored(palette.warning, "%s", LOC("CLUB_BOARD_CONCERNED"));
  if (board_targets) renderTargets(*board_targets, keyWidth);
  UI::endCard();
}

void ClubScene::renderTargets(const GameController::BoardTargets& targets,
                              float keyWidth) const
{
  const Theme::Palette& palette = Theme::palette();
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * Theme::scale()));
  UI::sectionLabel(LOC("CLUB_TARGETS"));
  const auto row =
      [&](const char* label, const std::string& value, ObjectiveGrade grade)
  {
    const char* status = LOC(BoardModel::targetStatusKey(grade));
    ImVec4 color = palette.positive;
    if (grade == ObjectiveGrade::Failed)
      color = palette.negative;
    else if (grade == ObjectiveGrade::Missed)
      color = palette.warning;
    UI::keyValue(label, value.c_str(), keyWidth);
    ImGui::SameLine();
    ImGui::TextColored(color, "%s", status);
  };
  row(LOC("CLUB_TARGET_CUP"), LOC(BoardModel::cupObjectiveKey(targets.cup)),
      targets.cup_grade);
  row(LOC("CLUB_TARGET_FINANCES"),
      LOC(BoardModel::financeObjectiveKey(targets.finances)),
      targets.finance_grade);
  const std::string youth =
      targets.youth_target == 0
          ? std::string(LOC("BOARD_YOUTH_NONE"))
          : fmt::sprintf(LOC("CLUB_TARGET_YOUTH_VALUE"), targets.young_regulars,
                         targets.youth_target);
  row(LOC("CLUB_TARGET_YOUTH"), youth, targets.youth_grade);
}

void ClubScene::renderSupporters(float width)
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("club_supporters", LOC("CLUB_SUPPORTERS"), width);
  if (!supporters_index.has_value())
  {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(palette.faint, "%s", LOC("SUPPORTERS_PENDING"));
    ImGui::PopTextWrapPos();
    UI::endCard();
    return;
  }
  const float index = *supporters_index;
  const float keyWidth = KEY_WIDTH * Theme::scale();
  const std::string value = std::format("{:.0f} / 100", index);
  UI::meter(LOC("SUPPORTERS_INDEX"), index / 100.0f, keyWidth,
            Theme::ratingColor(index), value.c_str());
  UI::keyValue(LOC("SUPPORTERS_FEELING"), LOC(SupporterModel::moodKey(index)),
               keyWidth);
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
  UI::sectionLabel(LOC("SUPPORTERS_REASONS"));
  if (supporter_reasons.empty())
    ImGui::TextColored(palette.faint, "%s", LOC("SUPPORTERS_NO_REASONS"));
  const float pointsWidth =
      ImGui::CalcTextSize("+00").x + Theme::Space::S * Theme::scale();
  for (const SupporterLine& line : supporter_reasons)
  {
    const std::string points = std::format("{:+.0f}", line.points);
    const float start = ImGui::GetCursorPosX();
    ImGui::TextColored(line.points > 0.0f ? palette.positive : palette.negative,
                       "%s", points.c_str());
    ImGui::SameLine(start + pointsWidth);
    UI::textFitted(line.text, ImGui::GetContentRegionAvail().x, palette.text);
  }
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(palette.faint, "%s", LOC("SUPPORTERS_NOTE"));
  ImGui::PopTextWrapPos();
  UI::endCard();
}

void ClubScene::renderStadium(float width)
{
  GameController& controller = guiView->getController();
  const Theme::Palette& palette = Theme::palette();
  const ClubProfile& profile = controller.getManagedTeam()->get().getProfile();
  UI::beginAutoHeightCard("club_stadium", LOC("CLUB_MATCHDAY"), width);
  const float keyWidth = KEY_WIDTH * Theme::scale();
  const std::string price = Format::moneyFull(profile.ticket_price);
  UI::keyValue(LOC("CLUB_TICKET_PRICE"), price.c_str(), keyWidth);
  const std::string fair =
      fair_ticket_price > 0 ? Format::moneyFull(fair_ticket_price) : "–";
  UI::keyValue(LOC("CLUB_FAIR_PRICE"), fair.c_str(), keyWidth);
  const std::string crowd =
      last_attendance > 0
          ? fmt::sprintf(LOC("CLUB_ATTENDANCE_VALUE"),
                         Format::thousands(last_attendance).c_str(),
                         100.0 * static_cast<double>(last_attendance) /
                             static_cast<double>(std::max<uint32_t>(
                                 1, profile.stadium_capacity)))
          : std::string(LOC("CLUB_NO_HOME_MATCH"));
  UI::keyValue(LOC("CLUB_LAST_ATTENDANCE"), crowd.c_str(), keyWidth);

  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(palette.muted, "%s", LOC("CLUB_SET_PRICE"));
  ImGui::SameLine(keyWidth);
  int64_t priceInput = ticket_price_input;
  const std::array<UI::MoneyChip, 1> chips = {
      {{LOC("CLUB_FAIR_PRICE"), static_cast<int64_t>(fair_ticket_price)}}};
  UI::MoneyInputOptions options;
  options.minimum = 1;
  options.maximum = MAX_TICKET_PRICE;
  options.width = 190.0f * Theme::scale();
  if (fair_ticket_price > 0) options.chips = chips;
  if (UI::moneyInput("##ticket_price", priceInput, options))
    ticket_price_input = static_cast<int>(priceInput);
  UI::sameLineIfFits(UI::buttonWidth(LOC("CLUB_APPLY_PRICE")));
  ImGui::BeginDisabled(ticket_price_input ==
                       static_cast<int>(profile.ticket_price));
  if (UI::primaryButton(LOC("CLUB_APPLY_PRICE")) &&
      controller.setTicketPrice(static_cast<uint32_t>(ticket_price_input)))
    showToast(LOC("CLUB_PRICE_TOAST"));
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("%s", LOC("CLUB_PRICE_HELP"));

  ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * Theme::scale()));
  UI::sectionLabel(LOC("CLUB_FACILITIES"));
  UI::attributeBar(LOC("CLUB_TRAINING_FACILITIES"),
                   static_cast<float>(profile.training_facilities), keyWidth);
  UI::attributeBar(LOC("CLUB_YOUTH_FACILITIES"),
                   static_cast<float>(profile.youth_facilities), keyWidth);
  UI::endCard();
}

void ClubScene::renderHistory() const
{
  const Theme::Palette& palette = Theme::palette();
  // Grows with the seasons played; the page scrolls, never the card.
  UI::beginAutoHeightCard("club_history", LOC("CLUB_HISTORY"));
  if (history.empty())
  {
    UI::emptyState(LOC("CLUB_HISTORY_EMPTY_TITLE"),
                   LOC("CLUB_HISTORY_EMPTY_BODY"));
    UI::endCard();
    return;
  }
  std::array<UI::Column, 6> columns = historyColumns();
  for (UI::Column& column : columns) column.label = LOC(column.label);
  if (const UI::ColumnMask mask =
          UI::fitColumns(columns, ImGui::GetContentRegionAvail().x, 150.0f);
      UI::beginResponsiveTable(
          "history", columns, mask,
          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
  {
    for (const HistoryRow& line : history)
    {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextColored(palette.muted, "%s", line.season.c_str());
      if (UI::cell(mask, 1)) ImGui::TextUnformatted(line.competition.c_str());
      ImGui::TableNextColumn();
      ImGui::TextColored(palette.text, "%s", line.champion.c_str());
      if (UI::cell(mask, 3)) ImGui::TextUnformatted(line.runner_up.c_str());
      if (UI::cell(mask, 4)) ImGui::TextUnformatted(line.top_scorer.c_str());
      if (UI::cell(mask, 5))
      {
        UI::textFitted(line.movements, ImGui::GetContentRegionAvail().x,
                       palette.muted);
      }
    }
    ImGui::EndTable();
  }
  UI::endCard();
}
