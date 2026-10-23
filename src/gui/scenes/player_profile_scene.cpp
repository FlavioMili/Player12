// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/player_profile_scene.h"

#include <fmt/printf.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <limits>
#include <map>
#include <utility>

#include "database/gamedata.h"
#include "global/global.h"
#include "global/language_manager.h"
#include "gui/gui_view.h"
#include "gui/scenes/lineup_scene.h"
#include "gui/scenes/scouting_scene.h"
#include "gui/scenes/transfer_market_scene.h"
#include "gui/view_models/competition_view.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/injury.h"
#include "model/scouting.h"
#include "model/transfer_market.h"
#include "model/transfer_tuning.h"
#include "model/world_simulation.h"

namespace
{
constexpr float THREE_COLUMN_MIN_WIDTH = 900.0f;
constexpr float HEADER_HEIGHT = 112.0f;
constexpr float BIO_KEY_WIDTH = 130.0f;
constexpr float ATTRIBUTE_LABEL_WIDTH = 118.0f;
constexpr const char* LIST_CONFIRM_ID = "##confirm_transfer_list";

std::string nationalityName(Language nationality)
{
  const auto name = languageToString.find(nationality);
  if (name == languageToString.end()) return {};
  const std::string key = "NATION_" + name->second;
  const char* localized = LOC(key.c_str());
  // LOC returns the key itself when a translation is missing.
  return key == localized ? name->second : std::string(localized);
}

/// Condition: fresh is good, anything clearly below needs attention.
ImVec4 conditionColor(float value)
{
  const Theme::Palette& palette = Theme::palette();
  if (value >= 85.0f) return palette.positive;
  if (value >= 65.0f) return palette.warning;
  return palette.negative;
}

/// Sharpness and morale start at an ordinary 60: values around it stay
/// neutral and only a meaningful deviation is coloured.
ImVec4 levelColor(float value)
{
  const Theme::Palette& palette = Theme::palette();
  if (value >= 75.0f) return palette.positive;
  if (value >= 45.0f) return palette.info;
  if (value >= 30.0f) return palette.warning;
  return palette.negative;
}

/// Key width of a key/value list: fixed, but never more than 45% of the card
/// so values keep room at small windows and large UI scales.
float keyWidthFor(float baseWidth)
{
  return std::min(baseWidth * Theme::scale(),
                  ImGui::GetContentRegionAvail().x * 0.45f);
}

/// UI::keyValue whose value wraps inside the card instead of being clipped.
void keyValueWrapped(const char* key, const char* value, float keyWidth)
{
  const float startX = ImGui::GetCursorPosX();
  ImGui::TextColored(Theme::palette().muted, "%s", key);
  ImGui::SameLine(std::max(startX + keyWidth,
                           ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x +
                               ImGui::GetStyle().ItemSpacing.x * 2.0f));
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextUnformatted(value);
  ImGui::PopTextWrapPos();
}

/// Attribute row with the scouts' likely range: a faint band from low to
/// high and a marker at the estimate, in the attributeBar style.
void rangeBar(const char* label, float estimate, float low, float high,
              float labelWidth)
{
  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  std::array<char, 24> text{};
  const auto formatted = std::format_to_n(
      text.data(), static_cast<std::ptrdiff_t>(text.size() - 1),
      "{:.0f}  ({:.0f}-{:.0f})", static_cast<double>(estimate),
      static_cast<double>(low), static_cast<double>(high));
  text[std::min(static_cast<std::size_t>(formatted.size), text.size() - 1)] =
      '\0';
  const float lineHeight = ImGui::GetTextLineHeight();
  const ImVec2 start = ImGui::GetCursorScreenPos();
  const float available = ImGui::GetContentRegionAvail().x;
  const float textWidth = ImGui::CalcTextSize(text.data()).x;
  const float barWidth =
      std::max(24.0f * scale,
               available - labelWidth - textWidth - Theme::Space::S * scale);
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  drawList->PushClipRect(
      start, ImVec2(start.x + labelWidth - 4.0f * scale, start.y + lineHeight),
      true);
  drawList->AddText(start, Theme::toU32(palette.muted), label);
  drawList->PopClipRect();

  const float barHeight = 6.0f * scale;
  const ImVec2 barMin(start.x + labelWidth,
                      start.y + (lineHeight - barHeight) * 0.5f);
  const ImVec2 barMax(barMin.x + barWidth, barMin.y + barHeight);
  const auto at = [&](float value)
  { return barMin.x + barWidth * std::clamp(value / 100.0f, 0.0f, 1.0f); };
  const ImVec4 color = Theme::ratingColor(estimate);
  drawList->AddRectFilled(barMin, barMax, Theme::toU32(palette.raised),
                          barHeight * 0.5f);
  drawList->AddRectFilled(ImVec2(at(low), barMin.y), ImVec2(at(high), barMax.y),
                          Theme::toU32(color, 0.35f), barHeight * 0.5f);
  const float marker = std::max(2.0f, 3.0f * scale);
  drawList->AddRectFilled(
      ImVec2(at(estimate) - marker * 0.5f, barMin.y - 2.0f * scale),
      ImVec2(at(estimate) + marker * 0.5f, barMax.y + 2.0f * scale),
      Theme::toU32(color), 1.0f);
  drawList->AddText(ImVec2(start.x + available - textWidth, start.y),
                    Theme::toU32(color), text.data());
  ImGui::Dummy(ImVec2(available, lineHeight));
}

constexpr uint16_t SCOUT_DAYS = 14;
}  // namespace

PlayerProfileScene::PlayerProfileScene(GUIView* parent, PlayerID playerId)
    : ManagementScene(parent), player_id(playerId)
{
}

void PlayerProfileScene::update(float /*deltaTime*/)
{
  /* Player data is refreshed on scene entry; there is no frame-driven state
   * to update while the profile is open. */
}

NavSection PlayerProfileScene::navSection() const
{
  return isOwnPlayer() ? NavSection::SQUAD : NavSection::NONE;
}

const Player* PlayerProfileScene::player() const
{
  const auto data = guiView->getController().getGameData();
  if (!data) return nullptr;
  const auto found = data->getPlayer(player_id);
  return found ? &found->get() : nullptr;
}

bool PlayerProfileScene::isOwnPlayer() const
{
  const auto managed = guiView->getController().getManagedTeam();
  return managed && row.team_id == managed->get().getId() && row.id != 0;
}

void PlayerProfileScene::refresh()
{
  const Player* current = player();
  sections.clear();
  fits.clear();
  season_rows.clear();
  career_rows.clear();
  transfers.clear();
  latest_report.reset();
  if (current == nullptr) return;
  GameController& controller = guiView->getController();
  row = PlayerView::makeRow(controller, *current);
  const auto club = controller.getTeamById(row.team_id);
  club_name = row.team_id == FREE_AGENTS_TEAM_ID || !club
                  ? std::string(LOC("TRANSFER_FREE_AGENT_LABEL"))
                  : club->get().getName();
  nationality = nationalityName(current->getNationality());
  const uint32_t clause = controller.getReleaseClause(player_id);
  clause_text = clause > 0 ? Format::money(clause) : std::string();

  dynamics = current->getDynamics();
  form = current->getForm();
  league_ban = controller.getSuspensionMatches(player_id, MatchType::LEAGUE);
  cup_ban = controller.getSuspensionMatches(player_id, MatchType::CUP);
  scouted = !isOwnPlayer();
  squad_role_key =
      scouted ? nullptr : squadRoleKey(controller.getSquadRole(player_id));
  window_open = controller.getTransferWindow().open;

  // Attribute name -> (estimate, low, high): exact for own players, the
  // scouts' estimates for everyone else.
  std::vector<std::pair<std::string, AttributeLine>> attributes;
  if (scouted)
  {
    const auto view = controller.getScoutedView(player_id);
    knowledge = view ? view->knowledge : 0;
    row.overall = view ? view->overall : 0.0f;
    overall_range = view ? std::format("{:.0f} – {:.0f}",
                                       static_cast<double>(view->overall_low),
                                       static_cast<double>(view->overall_high))
                         : std::string();
    row.market_value = view ? static_cast<uint32_t>(std::clamp<int64_t>(
                                  view->estimated_value, 0, UINT32_MAX))
                            : 0;
    row.value_text = Format::money(row.market_value);
    potential_low = view ? view->potential_low : 0.0f;
    potential_high = view ? view->potential_high : 0.0f;
    if (view)
    {
      for (const ScoutedAttribute& attribute : view->attributes)
        attributes.push_back(
            {attribute.name,
             {PlayerView::statLabel(attribute.name), attribute.estimate,
              attribute.low, attribute.high}});
      if (view->latest_report_id.has_value())
      {
        for (const ScoutReport& report : controller.getScoutReports())
        {
          if (report.id != *view->latest_report_id) continue;
          ReportSummary summary;
          summary.heading = std::format("{}  ·  {}", Format::date(report.date),
                                        report.scout_name);
          summary.grade = static_cast<char>(
              'A' + static_cast<int>(std::to_underlying(report.grade)));
          summary.grade_key = scoutGradeKey(report.grade);
          summary.ability =
              std::format("{:.0f}", static_cast<double>(report.overall));
          summary.potential = std::format(
              "{:.0f} – {:.0f}", static_cast<double>(report.potential_low),
              static_cast<double>(report.potential_high));
          summary.fee = Format::money(report.estimated_fee);
          latest_report = std::move(summary);
        }
      }
    }
    shortlisted = controller.isShortlisted(player_id);
    being_scouted = std::ranges::any_of(
        controller.getScoutAssignments(),
        [this](const ScoutAssignment& assignment)
        {
          return !assignment.finished &&
                 assignment.kind == ScoutTargetKind::Player &&
                 assignment.target_id == player_id;
        });
    scout_cost = controller.getScoutAssignmentCost(ScoutTargetKind::Player,
                                                   player_id, SCOUT_DAYS);
  }
  else
  {
    fits = PlayerView::roleFits(*current, controller.getStatsConfig());
    const auto potential = controller.getPotentialEstimate(player_id);
    potential_low = potential.low;
    potential_high = potential.high;
    for (const auto& [name, value] : current->getStats())
      attributes.push_back(
          {name, {PlayerView::statLabel(name), value, value, value}});
  }

  std::vector<bool> grouped(attributes.size(), false);
  for (const PlayerView::AttributeGroup& group : PlayerView::ATTRIBUTE_GROUPS)
  {
    AttributeSection section{group.title_key, {}};
    for (const std::string_view statName : group.stats)
    {
      for (size_t index = 0; index < attributes.size(); ++index)
      {
        if (attributes[index].first != statName) continue;
        grouped[index] = true;
        section.lines.push_back(attributes[index].second);
      }
    }
    if (!section.lines.empty()) sections.push_back(std::move(section));
  }
  // Attributes added by future systems still show up, in their own group.
  AttributeSection other{"PROFILE_GROUP_OTHER", {}};
  for (size_t index = 0; index < attributes.size(); ++index)
    if (!grouped[index]) other.lines.push_back(attributes[index].second);
  if (!other.lines.empty()) sections.push_back(std::move(other));

  for (const PlayerSeasonStats& season :
       controller.getPlayerSeasonStats(player_id))
    season_rows.emplace_back(
        LOC(CompetitionView::matchTypeKey(season.competition_type)), season);

  // Career: one line per season and club, all competitions combined.
  std::map<std::pair<uint16_t, TeamID>, PlayerSeasonStats> bySeason;
  for (const PlayerSeasonStats& season : controller.getPlayerCareer(player_id))
  {
    PlayerSeasonStats& total = bySeason[{season.season, season.team_id}];
    total.season = season.season;
    total.team_id = season.team_id;
    total.appearances += season.appearances;
    total.starts += season.starts;
    total.minutes += season.minutes;
    total.goals += season.goals;
    total.assists += season.assists;
    total.yellow_cards += season.yellow_cards;
    total.red_cards += season.red_cards;
    total.rating_total += season.rating_total;
    total.rated_matches += season.rated_matches;
  }
  for (auto entry = bySeason.rbegin(); entry != bySeason.rend(); ++entry)
  {
    const auto team = controller.getTeamById(entry->first.second);
    career_rows.emplace_back(
        fmt::sprintf(LOC("PROFILE_CAREER_SEASON"), entry->first.first,
                     team ? team->get().getName() : std::string()),
        entry->second);
  }

  // Transfer history, latest move first.
  if (const Game* game = controller.getGame())
  {
    const auto teamName = [&controller](TeamID team_id)
    {
      const auto team = controller.getTeamById(team_id);
      return team_id == FREE_AGENTS_TEAM_ID || !team
                 ? std::string(LOC("TRANSFER_FREE_AGENT_LABEL"))
                 : team->get().getName();
    };
    std::vector<TransferRecord> history =
        game->getTransfers().historyFor(player_id);
    for (auto record = history.rbegin(); record != history.rend(); ++record)
      transfers.emplace_back(
          Format::date(record->date), teamName(record->from_team),
          teamName(record->to_team), transferKindKey(record->kind),
          record->fee > 0 ? Format::money(record->fee) : std::string("–"));
  }
}

void PlayerProfileScene::renderContent()
{
  const Player* current = player();
  if (current == nullptr)
  {
    UI::emptyState(LOC("PROFILE_MISSING_TITLE"), LOC("PROFILE_MISSING_BODY"));
    return;
  }
  renderHeader();

  const float available = ImGui::GetContentRegionAvail().x;
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  const float spacing = ImGui::GetStyle().ItemSpacing.y;
  const float availableHeight = ImGui::GetContentRegionAvail().y;
  // Both rows fit a 720p window at 100%; larger scales scroll the page.
  const float topHeight =
      std::max(260.0f * Theme::scale(), std::floor(availableHeight * 0.6f));
  const float bottomHeight =
      std::max(160.0f * Theme::scale(), availableHeight - topHeight - spacing);
  if (available >= THREE_COLUMN_MIN_WIDTH * Theme::scale())
  {
    const float side = std::floor((available - 2.0f * gap) * 0.29f);
    const float middle = available - 2.0f * gap - 2.0f * side;
    renderBio(*current, side, topHeight);
    ImGui::SameLine();
    renderAttributes(middle, topHeight);
    ImGui::SameLine();
    renderStatus(side, topHeight);
    renderStatistics(side + gap + middle, bottomHeight);
    ImGui::SameLine();
    if (scouted)
      renderScouting(side, bottomHeight);
    else
      renderSuitability(side, bottomHeight);
  }
  else
  {
    const float half = std::floor((available - gap) * 0.5f);
    renderBio(*current, half, topHeight);
    ImGui::SameLine();
    renderAttributes(available - gap - half, topHeight);
    renderStatus(half, bottomHeight);
    ImGui::SameLine();
    if (scouted)
      renderScouting(available - gap - half, bottomHeight);
    else
      renderSuitability(available - gap - half, bottomHeight);
    renderStatistics(available, bottomHeight);
  }
  renderDialogs();
}

void PlayerProfileScene::renderHeader()
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginCard("profile_header", nullptr,
                ImVec2(0.0f, HEADER_HEIGHT * Theme::scale()));
  const float startX = ImGui::GetCursorPosX();
  const float rightEdge = startX + ImGui::GetContentRegionAvail().x;

  // Overall and value on the right edge.
  const std::string overall = std::format("{:.0f}", row.overall);
  float overallWidth = 0.0f;
  {
    Theme::ScopedText display(Theme::Text::DISPLAY);
    overallWidth = std::max(ImGui::CalcTextSize(overall.c_str()).x,
                            ImGui::CalcTextSize("00").x);
  }
  float valueWidth = 0.0f;
  {
    Theme::ScopedText title(Theme::Text::TITLE);
    valueWidth = ImGui::CalcTextSize(row.value_text.c_str()).x;
  }
  const char* valueLabel =
      LOC(scouted ? "PROFILE_ESTIMATED_VALUE_TITLE" : "PROFILE_MARKET_VALUE");
  const char* overallLabel =
      LOC(scouted ? "PROFILE_OVERALL_ESTIMATE" : "PROFILE_OVERALL");
  {
    Theme::ScopedText caption(Theme::Text::CAPTION);
    valueWidth = std::max(valueWidth, ImGui::CalcTextSize(valueLabel).x);
    overallWidth = std::max(overallWidth, ImGui::CalcTextSize(overallLabel).x);
  }
  const float blockX =
      rightEdge - overallWidth - valueWidth - Theme::Space::XL * Theme::scale();

  ImGui::BeginGroup();
  {
    Theme::ScopedText heading(Theme::Text::HEADING);
    ImGui::TextUnformatted(row.name.c_str());
  }
  UI::badge(row.role.c_str(), palette.accent);
  ImGui::SameLine();
  ImGui::TextColored(palette.muted, "%s", LOC(PlayerView::groupKey(row.group)));
  ImGui::SameLine(0.0f, Theme::Space::L * Theme::scale());
  if (row.team_id != FREE_AGENTS_TEAM_ID && !isOwnPlayer())
  {
    if (UI::link(club_name.c_str(), "profile_club"))
      Navigation::openClub(guiView, row.team_id);
  }
  else
  {
    ImGui::TextUnformatted(club_name.c_str());
  }
  ImGui::SameLine(0.0f, Theme::Space::L * Theme::scale());
  ImGui::TextColored(
      palette.muted, "%s",
      fmt::sprintf(LOC("PROFILE_AGE_NATION"), row.age, nationality.c_str())
          .c_str());
  if (row.listed)
  {
    ImGui::SameLine(0.0f, Theme::Space::L * Theme::scale());
    UI::badge(LOC("ROSTER_BADGE_LISTED"), palette.warning);
  }

  if (scouted)
  {
    ImGui::SameLine(0.0f, Theme::Space::L * Theme::scale());
    UI::badge(fmt::sprintf(LOC("PROFILE_KNOWLEDGE_BADGE"),
                           static_cast<int>(knowledge))
                  .c_str(),
              palette.info);
  }
  renderActions();
  ImGui::EndGroup();

  const float groupRight = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
  ImGui::SetCursorPos(
      ImVec2(std::max(blockX, groupRight + Theme::Space::L * Theme::scale()),
             ImGui::GetStyle().WindowPadding.y));
  ImGui::BeginGroup();
  UI::sectionLabel(overallLabel);
  {
    Theme::ScopedText display(Theme::Text::DISPLAY);
    ImGui::TextColored(Theme::ratingColor(row.overall), "%s", overall.c_str());
  }
  if (scouted && !overall_range.empty())
  {
    Theme::ScopedText small(Theme::Text::SMALL);
    ImGui::TextColored(palette.muted, "%s", overall_range.c_str());
  }
  ImGui::EndGroup();
  ImGui::SameLine(0.0f, Theme::Space::XL * Theme::scale());
  ImGui::BeginGroup();
  UI::sectionLabel(valueLabel);
  {
    Theme::ScopedText title(Theme::Text::TITLE);
    ImGui::TextUnformatted(row.value_text.c_str());
  }
  ImGui::EndGroup();
  UI::endCard();
}

void PlayerProfileScene::renderActions()
{
  GameController& controller = guiView->getController();
  if (!scouted)
  {
    if (row.listed)
    {
      if (ImGui::SmallButton(LOC("TRANSFER_UNLIST")))
      {
        controller.removePlayerFromTransfer(player_id);
        showToast(LOC("PROFILE_UNLISTED_TOAST"));
        refresh();
      }
    }
    else
    {
      // Disabled with the reason when the club cannot list him now.
      const GameController::PlayerActionBlock block =
          controller.getListingBlock(player_id);
      ImGui::BeginDisabled(block != GameController::PlayerActionBlock::None);
      if (ImGui::SmallButton(LOC("PROFILE_TRANSFER_LIST")))
        list_confirm_requested = true;
      ImGui::EndDisabled();
      if (block != GameController::PlayerActionBlock::None &&
          ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s",
                          LOC(GameController::playerActionBlockKey(block)));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(LOC("PROFILE_OPEN_LINEUP")))
      guiView->navigateTo(std::make_unique<LineupScene>(guiView, player_id));
    ImGui::SameLine();
    const GameController::PlayerActionBlock renewal =
        controller.getRenewalBlock(player_id);
    ImGui::BeginDisabled(renewal != GameController::PlayerActionBlock::None);
    if (ImGui::SmallButton(LOC("PROFILE_RENEW"))) renew_requested = true;
    ImGui::EndDisabled();
    if (renewal != GameController::PlayerActionBlock::None &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("%s",
                        LOC(GameController::playerActionBlockKey(renewal)));
    UI::sameLineIfFits(UI::buttonWidth(LOC("TALK_ACTION")));
    if (ImGui::SmallButton(LOC("TALK_ACTION")))
      talk_dialog.open(controller, player_id);
    return;
  }

  // Recruitment: offer (or contract talks for a free agent), scout, track.
  const bool freeAgent = row.team_id == FREE_AGENTS_TEAM_ID;
  const bool canDeal = freeAgent || window_open;
  ImGui::BeginDisabled(!canDeal);
  if (ImGui::SmallButton(
          LOC(freeAgent ? "TRANSFER_ACTION_SIGN" : "TRANSFER_ACTION_OFFER")))
    guiView->navigateTo(
        std::make_unique<TransferMarketScene>(guiView, player_id));
  ImGui::EndDisabled();
  if (!canDeal && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("%s", LOC("TRANSFER_WINDOW_CLOSED_HINT"));

  const std::string scoutLabel =
      being_scouted ? std::string(LOC("PROFILE_BEING_SCOUTED"))
                    : fmt::sprintf(LOC("SCOUTING_SCOUT_PLAYER"),
                                   static_cast<int>(SCOUT_DAYS),
                                   Format::money(scout_cost).c_str());
  UI::sameLineIfFits(UI::buttonWidth(scoutLabel.c_str()));
  ImGui::BeginDisabled(being_scouted);
  if (ImGui::SmallButton(scoutLabel.c_str())) sendScout();
  ImGui::EndDisabled();

  const char* shortlistLabel =
      LOC(shortlisted ? "SCOUTING_REMOVE_SHORTLIST" : "SCOUTING_ADD_SHORTLIST");
  UI::sameLineIfFits(UI::buttonWidth(shortlistLabel));
  if (ImGui::SmallButton(shortlistLabel))
  {
    if (const bool changed = shortlisted
                                 ? controller.removeFromShortlist(player_id)
                                 : controller.addToShortlist(player_id);
        changed)
      showToast(LOC(shortlisted ? "SCOUTING_SHORTLIST_REMOVED"
                                : "SCOUTING_SHORTLIST_ADDED"));
    refresh();
  }

  // Following routes his news (moves, injuries, big matches, honours,
  // contracts) to the inbox.
  const bool following = controller.isFollowingPlayer(player_id);
  const char* followLabel = LOC(following ? "INBOX_UNFOLLOW" : "INBOX_FOLLOW");
  UI::sameLineIfFits(UI::buttonWidth(followLabel));
  if (ImGui::SmallButton(followLabel))
  {
    if (following ? controller.unfollowPlayer(player_id)
                  : controller.followPlayer(player_id))
      showToast(fmt::sprintf(
          LOC(following ? "INBOX_UNFOLLOWED" : "INBOX_FOLLOWING"), row.name));
    else
      showToast(fmt::sprintf(LOC("INBOX_FOLLOW_LIMIT"),
                             static_cast<int>(Stories::MAX_FOLLOWS)),
                true);
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("%s", LOC("INBOX_FOLLOW_HELP"));

  if (!freeAgent)
  {
    UI::sameLineIfFits(UI::buttonWidth(LOC("PROFILE_VIEW_CLUB")));
    if (ImGui::SmallButton(LOC("PROFILE_VIEW_CLUB")))
      Navigation::openClub(guiView, row.team_id);
  }
}

void PlayerProfileScene::sendScout()
{
  GameController& controller = guiView->getController();
  const auto& scouts = controller.getScouts();
  const auto& assignments = controller.getScoutAssignments();
  const auto idle = std::ranges::find_if(
      scouts,
      [&assignments](const ScoutProfile& scout)
      {
        return std::ranges::none_of(assignments,
                                    [&scout](const ScoutAssignment& assignment)
                                    {
                                      return !assignment.finished &&
                                             assignment.scout_id == scout.id;
                                    });
      });
  if (idle == scouts.end())
  {
    showToast(
        LOC(scouts.empty() ? "SCOUTING_NO_SCOUTS" : "SCOUTING_NO_IDLE_SCOUT"),
        true);
    return;
  }
  const ScoutAssignError error = controller.startScoutAssignment(
      idle->id, ScoutTargetKind::Player, player_id, SCOUT_DAYS);
  showToast(LOC(scoutAssignErrorKey(error)), error != ScoutAssignError::None);
  refresh();
}

void PlayerProfileScene::renderBio(const Player& current, float width,
                                   float height) const
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginCard("profile_bio", LOC("PROFILE_BIO"), ImVec2(width, height), true);
  const float keyWidth = keyWidthFor(BIO_KEY_WIDTH);
  const std::string age = std::to_string(current.getAge());
  const std::string heightText = std::format("{} cm", current.getHeight());
  const char* foot = current.getFoot() == Foot::Right ? LOC("PLAYER_FOOT_RIGHT")
                                                      : LOC("PLAYER_FOOT_LEFT");
  UI::keyValue(LOC("PLAYER_AGE"), age.c_str(), keyWidth);
  UI::keyValue(LOC("PROFILE_NATIONALITY"), nationality.c_str(), keyWidth);
  UI::keyValue(LOC("PROFILE_HEIGHT"), heightText.c_str(), keyWidth);
  UI::keyValue(LOC("PROFILE_FOOT"), foot, keyWidth);
  UI::keyValue(LOC("PROFILE_POSITION"), row.role.c_str(), keyWidth);
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * Theme::scale()));
  UI::sectionLabel(LOC("PROFILE_CONTRACT"));
  const std::string wage =
      fmt::sprintf(LOC("PROFILE_WAGE_VALUE"), row.wage_text.c_str());
  // A release clause shares the contract's row.
  const std::string contract =
      fmt::sprintf(LOC("PROFILE_CONTRACT_VALUE"), row.contract_years) +
      (clause_text.empty()
           ? std::string()
           : fmt::sprintf(LOC("PROFILE_CLAUSE_SUFFIX"), clause_text));
  UI::keyValue(LOC("PLAYER_WEEKLY_WAGE"), wage.c_str(), keyWidth);
  ImGui::PushStyleColor(
      ImGuiCol_Text, row.contract_years <= 1 ? palette.negative : palette.text);
  UI::keyValue(LOC("PLAYER_CONTRACT"), contract.c_str(), keyWidth);
  ImGui::PopStyleColor();

  UI::keyValue(
      LOC(scouted ? "PROFILE_ESTIMATED_VALUE" : "PROFILE_MARKET_VALUE_LABEL"),
      row.value_text.c_str(), keyWidth);
  UI::keyValue(
      LOC("PROFILE_TRANSFER_STATUS"),
      LOC(row.listed ? "PROFILE_STATUS_LISTED" : "PROFILE_STATUS_NOT_LISTED"),
      keyWidth);
  UI::endCard();
}

void PlayerProfileScene::renderStatus(float width, float height) const
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginCard("profile_status", LOC("PROFILE_STATUS"), ImVec2(width, height),
                true);
  // Availability first: it is what decides selection.
  if (dynamics.injury_days > 0)
  {
    UI::badge(LOC("PROFILE_INJURED"), palette.negative);
    ImGui::SameLine();
    ImGui::TextUnformatted(
        fmt::sprintf(LOC("PROFILE_INJURY_DETAIL"),
                     LOC(InjuryModel::nameKey(dynamics.injury)),
                     dynamics.injury_days)
            .c_str());
  }
  else if (league_ban > 0 || cup_ban > 0)
  {
    UI::badge(LOC("PROFILE_SUSPENDED"), palette.warning);
    ImGui::SameLine();
    ImGui::TextUnformatted(fmt::sprintf(LOC("PLAYER_SUSPENDED_MATCHES"),
                                        std::max(league_ban, cup_ban))
                               .c_str());
  }
  else
  {
    UI::badge(LOC("PROFILE_AVAILABLE"), palette.positive);
  }
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));

  // Condition, sharpness and morale are only known inside the club.
  if (!scouted)
  {
    const float labelWidth = keyWidthFor(ATTRIBUTE_LABEL_WIDTH);
    const auto percentMeter =
        [labelWidth](const char* label, float value, const ImVec4& color)
    {
      const std::string text =
          std::format("{:.0f}%", static_cast<double>(value));
      UI::meter(label, value / 100.0f, labelWidth, color, text.c_str());
    };
    percentMeter(LOC("PROFILE_CONDITION"), dynamics.condition,
                 conditionColor(dynamics.condition));
    percentMeter(LOC("PROFILE_SHARPNESS"), dynamics.sharpness,
                 levelColor(dynamics.sharpness));
    percentMeter(LOC("PROFILE_MORALE"), dynamics.morale,
                 levelColor(dynamics.morale));
    ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
  }

  const float keyWidth = keyWidthFor(BIO_KEY_WIDTH);
  const std::string formText =
      dynamics.rating_count > 0
          ? fmt::sprintf(
                Format::plural("PROFILE_FORM_VALUE", dynamics.rating_count),
                static_cast<double>(form), dynamics.rating_count)
          : std::string(LOC("PROFILE_FORM_NONE"));
  keyValueWrapped(LOC("PROFILE_FORM"), formText.c_str(), keyWidth);
  if (squad_role_key != nullptr)
    keyValueWrapped(LOC("PROFILE_SQUAD_ROLE"), LOC(squad_role_key), keyWidth);
  if (potential_high > 0.0f)
  {
    const std::string range =
        std::format("{:.0f} – {:.0f}", static_cast<double>(potential_low),
                    static_cast<double>(potential_high));
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        Theme::ratingColor(static_cast<double>(potential_low + potential_high) *
                           0.5));
    UI::keyValue(LOC("PROFILE_POTENTIAL"), range.c_str(), keyWidth);
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("%s", LOC(isOwnPlayer() ? "PROFILE_POTENTIAL_OWN"
                                                : "PROFILE_POTENTIAL_SCOUTED"));
  }
  UI::endCard();
}

void PlayerProfileScene::renderAttributes(float width, float height) const
{
  UI::beginCard(
      "profile_attributes",
      LOC(scouted ? "PROFILE_ATTRIBUTES_SCOUTED" : "PROFILE_ATTRIBUTES"),
      ImVec2(width, height), true);
  const float labelWidth = keyWidthFor(ATTRIBUTE_LABEL_WIDTH);
  if (scouted && sections.empty())
  {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(Theme::palette().faint, "%s", LOC("PROFILE_NO_REPORT"));
    ImGui::PopTextWrapPos();
  }
  for (const AttributeSection& section : sections)
  {
    {
      Theme::ScopedText title(Theme::Text::SMALL);
      ImGui::TextUnformatted(LOC(section.title_key));
    }
    for (const AttributeLine& line : section.lines)
    {
      if (scouted)
        rangeBar(line.name.c_str(), line.value, line.low, line.high,
                 labelWidth);
      else
        UI::attributeBar(line.name.c_str(), line.value, labelWidth);
    }
    ImGui::Dummy(ImVec2(0.0f, 2.0f * Theme::scale()));
  }
  if (scouted && !sections.empty())
  {
    Theme::ScopedText small(Theme::Text::SMALL);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(Theme::palette().faint, "%s", LOC("PROFILE_RANGE_HINT"));
    ImGui::PopTextWrapPos();
  }
  UI::endCard();
}

void PlayerProfileScene::renderSuitability(float width, float height) const
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginCard("profile_fit", LOC("PROFILE_ROLE_FIT"), ImVec2(width, height),
                true);
  const float labelWidth = keyWidthFor(ATTRIBUTE_LABEL_WIDTH);
  for (const PlayerView::RoleFit& fit : fits)
    UI::attributeBar(LOC(PlayerView::groupKey(fit.group)), fit.rating,
                     labelWidth);
  const std::string natural = fmt::sprintf(
      LOC("PROFILE_NATURAL_POSITION"), LOC(PlayerView::groupKey(row.group)));
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(palette.faint, "%s", natural.c_str());
  ImGui::PopTextWrapPos();
  UI::endCard();
}

void PlayerProfileScene::renderScouting(float width, float height)
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginCard("profile_scouting", LOC("PROFILE_SCOUTING"),
                ImVec2(width, height), true);
  const float keyWidth = keyWidthFor(BIO_KEY_WIDTH);
  const std::string knowledgeText =
      std::format("{}%", static_cast<int>(knowledge));
  UI::meter(LOC("PROFILE_KNOWLEDGE"), static_cast<float>(knowledge) / 100.0f,
            keyWidth, palette.info, knowledgeText.c_str());
  if (being_scouted) UI::badge(LOC("PROFILE_BEING_SCOUTED"), palette.info);
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
  if (latest_report)
  {
    UI::sectionLabel(LOC("PROFILE_LATEST_REPORT"));
    {
      Theme::ScopedText small(Theme::Text::SMALL);
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextColored(palette.muted, "%s", latest_report->heading.c_str());
      ImGui::PopTextWrapPos();
    }
    const std::array<char, 2> grade = {latest_report->grade, '\0'};
    ImVec4 gradeColor = palette.muted;
    if (latest_report->grade == 'A')
      gradeColor = palette.positive;
    else if (latest_report->grade == 'B')
      gradeColor = palette.warning;
    UI::badge(grade.data(), gradeColor);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("%s", LOC(latest_report->grade_key));
    keyValueWrapped(LOC("PROFILE_REPORT_ABILITY"),
                    latest_report->ability.c_str(), keyWidth);
    keyValueWrapped(LOC("PROFILE_POTENTIAL"), latest_report->potential.c_str(),
                    keyWidth);
    keyValueWrapped(LOC("PROFILE_REPORT_FEE"), latest_report->fee.c_str(),
                    keyWidth);
    if (UI::link(LOC("PROFILE_OPEN_REPORTS"), "profile_open_reports"))
      guiView->navigateTo(std::make_unique<ScoutingScene>(
          guiView, ScoutingScene::Tab::REPORTS));
  }
  else
  {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(palette.faint, "%s", LOC("PROFILE_NO_REPORT"));
    ImGui::PopTextWrapPos();
  }
  UI::endCard();
}

void PlayerProfileScene::renderStatistics(float width, float height)
{
  UI::beginCard("profile_stats", nullptr, ImVec2(width, height), true);
  if (ImGui::BeginTabBar("profile_stats_tabs"))
  {
    if (ImGui::BeginTabItem(LOC("PROFILE_TAB_SEASON")))
    {
      renderStatsTable(season_rows, false);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(LOC("PROFILE_TAB_CAREER")))
    {
      renderStatsTable(career_rows, true);
      ImGui::EndTabItem();
    }
    if (const std::string transfersLabel =
            std::format("{} ({})###profile_transfers",
                        LOC("PROFILE_TAB_TRANSFERS"), transfers.size());
        ImGui::BeginTabItem(transfersLabel.c_str()))
    {
      renderTransfers();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  UI::endCard();
}

void PlayerProfileScene::renderStatsTable(const std::vector<StatsRow>& rows,
                                          bool career) const
{
  const Theme::Palette& palette = Theme::palette();
  if (rows.empty())
  {
    ImGui::TextColored(palette.faint, "%s", LOC("PROFILE_NO_STATS"));
    return;
  }
  if (!UI::beginDataTable(
          "profile_stats_table", 8,
          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
              ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit,
          560.0f, ImVec2(0.0f, 0.0f), 1))
    return;
  ImGui::TableSetupColumn(
      LOC(career ? "CLUB_COL_SEASON" : "CLUB_COL_COMPETITION"),
      ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableSetupColumn(LOC("STATS_COL_APPS"));
  ImGui::TableSetupColumn(LOC("STATS_COL_MINUTES"));
  ImGui::TableSetupColumn(LOC("STATS_COL_GOALS"));
  ImGui::TableSetupColumn(LOC("STATS_COL_ASSISTS"));
  ImGui::TableSetupColumn(LOC("STATS_COL_YELLOW"));
  ImGui::TableSetupColumn(LOC("STATS_COL_RED"));
  ImGui::TableSetupColumn(LOC("STATS_COL_RATING"));
  ImGui::TableHeadersRow();
  for (const StatsRow& line : rows)
  {
    const PlayerSeasonStats& stats = line.stats;
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(line.label.c_str());
    ImGui::TableNextColumn();
    ImGui::Text("%u (%u)", stats.appearances, stats.starts);
    ImGui::TableNextColumn();
    ImGui::Text("%u", stats.minutes);
    ImGui::TableNextColumn();
    ImGui::Text("%u", stats.goals);
    ImGui::TableNextColumn();
    ImGui::Text("%u", stats.assists);
    ImGui::TableNextColumn();
    ImGui::TextColored(stats.yellow_cards > 0 ? palette.warning : palette.muted,
                       "%u", stats.yellow_cards);
    ImGui::TableNextColumn();
    ImGui::TextColored(stats.red_cards > 0 ? palette.negative : palette.muted,
                       "%u", stats.red_cards);
    ImGui::TableNextColumn();
    if (stats.rated_matches > 0)
      ImGui::Text("%.2f", static_cast<double>(stats.averageRating()));
    else
      ImGui::TextColored(palette.faint, "–");
  }
  ImGui::EndTable();
}

void PlayerProfileScene::renderTransfers() const
{
  const Theme::Palette& palette = Theme::palette();
  if (transfers.empty())
  {
    ImGui::TextColored(palette.faint, "%s", LOC("PROFILE_NO_TRANSFERS"));
    return;
  }
  if (!UI::beginDataTable(
          "profile_transfer_table", 5,
          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
              ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit,
          520.0f, ImVec2(0.0f, 0.0f), 1))
    return;
  ImGui::TableSetupColumn(LOC("TRANSFER_COL_DATE"));
  ImGui::TableSetupColumn(LOC("TRANSFER_COL_FROM"),
                          ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableSetupColumn(LOC("TRANSFER_COL_TO"),
                          ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableSetupColumn(LOC("TRANSFER_COL_TYPE"));
  ImGui::TableSetupColumn(LOC("TRANSFER_COL_FEE"));
  ImGui::TableHeadersRow();
  for (const TransferLine& line : transfers)
  {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextColored(palette.muted, "%s", line.date.c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(line.from.c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(line.to.c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(LOC(line.kind_key));
    ImGui::TableNextColumn();
    UI::textRight(line.fee.c_str());
  }
  ImGui::EndTable();
}

void PlayerProfileScene::renderDialogs()
{
  GameController& controller = guiView->getController();
  if (talk_dialog.render(controller)) refresh();
  if (list_confirm_requested)
  {
    list_confirm_requested = false;
    ImGui::OpenPopup(LIST_CONFIRM_ID);
  }
  if (ImGui::IsPopupOpen(LIST_CONFIRM_ID))
  {
    const std::string body =
        fmt::sprintf(LOC("PROFILE_LIST_CONFIRM_BODY"), row.name.c_str(),
                     Format::moneyFull(row.market_value).c_str());
    if (UI::confirmDialog(LIST_CONFIRM_ID, LOC("PROFILE_TRANSFER_LIST"),
                          body.c_str(), LOC("PROFILE_TRANSFER_LIST"),
                          LOC("TRANSFER_CANCEL")) == UI::DialogResult::CONFIRM)
    {
      const GameController::PlayerActionBlock block =
          controller.getListingBlock(row.id);
      controller.listPlayerForTransfer(row.id, row.market_value);
      const bool listed = controller.isPlayerListed(row.id);
      showToast(LOC(listed ? "PROFILE_LISTED_TOAST"
                    : block != GameController::PlayerActionBlock::None
                        ? GameController::playerActionBlockKey(block)
                        : "PROFILE_LIST_FAILED"),
                !listed);
      refresh();
    }
  }

  // Renewals are contract talks with the player and his agent.
  if (renew_requested)
  {
    renew_requested = false;
    if (!contract_talks.open(controller, player_id))
      showToast(LOC(GameController::playerActionBlockKey(
                    controller.getRenewalBlock(player_id))),
                true);
  }
  if (contract_talks.render(controller) == ContractTalksDialog::Event::Signed)
  {
    showToast(contract_talks.signedMessage());
    refresh();
  }
}
