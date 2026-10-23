// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The scouts tab of ScoutingScene: the scouts list, a scout's page (live
// assignment, history and reports) and the send-somewhere flow.

#include <fmt/printf.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <format>
#include <map>
#include <memory>
#include <utility>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/global.h"
#include "global/language_manager.h"
#include "gui/gui_view.h"
#include "gui/scenes/scouting_scene.h"
#include "gui/scenes/scouting_scene_internal.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/competition.h"

using namespace ScoutingUi;

namespace
{
constexpr float TWO_COLUMN_MIN_WIDTH = 980.0f;
constexpr float SCOUT_LIST_WIDTH = 330.0f;
constexpr float SEND_TWO_COLUMN_MIN_WIDTH = 600.0f;
constexpr float SCOUT_LIST_MAX_WIDTH = 440.0f;
constexpr float HEADER_BAR_MAX_WIDTH = 380.0f;
constexpr float PICKER_STACKED_HEIGHT = 260.0f;
/** Stacked layout: scouts always visible in the list, detail minimum. */
constexpr float STACKED_LIST_ROWS = 3.0f;
constexpr float STACKED_DETAIL_MIN_HEIGHT = 380.0f;
constexpr float KEY_WIDTH = 150.0f;
constexpr float REPORTS_MIN_HEIGHT = 220.0f;
constexpr std::size_t PLAYER_MATCH_LIMIT = 8;
constexpr std::size_t EXPERTISE_LINES = 4;
constexpr float DAYS_PER_YEAR = 365.0f;

/** ISO 639-1 codes in SpokenLanguage order, for the compact list. */
constexpr std::array<const char*, static_cast<std::size_t>(std::to_underlying(
                                      SpokenLanguage::COUNT))>
    LANGUAGE_CODES = {"EN", "IT", "ES", "FR", "DE", "PT", "NL", "RU", "SV",
                      "PL", "EL", "TR", "AR", "JA", "ZH", "KO", "RO", "HR",
                      "DA", "FI", "NO", "SK", "CS", "HU", "UK"};

uint64_t pickerKey(ScoutTargetKind kind, uint32_t id)
{
  return (static_cast<uint64_t>(std::to_underlying(kind)) << 32U) | id;
}

std::string nationalityName(Language nationality)
{
  const auto name = languageToString.find(nationality);
  if (name == languageToString.end()) return {};
  const std::string key = "NATION_" + name->second;
  const char* localized = LOC(key.c_str());
  return key == localized ? name->second : std::string(localized);
}

/** Native language first, then the others in enum order. */
std::vector<SpokenLanguage> languagesOf(const ScoutExpertise& expertise)
{
  std::vector<SpokenLanguage> languages;
  const SpokenLanguage native = nativeLanguage(expertise.nationality);
  if (speaks(expertise.languages, native)) languages.push_back(native);
  for (std::size_t index = 0;
       index <
       static_cast<std::size_t>(std::to_underlying(SpokenLanguage::COUNT));
       ++index)
  {
    const auto language = static_cast<SpokenLanguage>(index);
    if (language != native && speaks(expertise.languages, language))
      languages.push_back(language);
  }
  return languages;
}

ImVec4 multiplierColor(float multiplier)
{
  const Theme::Palette& palette = Theme::palette();
  if (multiplier >= 1.1f) return palette.positive;
  if (multiplier <= 0.9f) return palette.negative;
  return palette.warning;
}

std::string multiplierText(float multiplier)
{
  return std::format("×{:.2f}", static_cast<double>(multiplier));
}
}  // namespace

// ---------------------------------------------------------------------------
// View models
// ---------------------------------------------------------------------------

void ScoutingScene::rebuildWorld()
{
  const GameController& controller = guiView->getController();
  world.clear();
  std::map<LeagueID, CountryNode> countries;
  const auto rootOf = [&controller](LeagueID league_id)
  {
    LeagueID current = league_id;
    for (int depth = 0; depth < 8; ++depth)
    {
      const auto league = controller.getLeagueById(current);
      if (!league.has_value() || !league->get().getParentLeagueID().has_value())
        break;
      current = *league->get().getParentLeagueID();
    }
    return current;
  };
  for (const League& league : controller.getLeagues())
  {
    const LeagueID root = rootOf(league.getId());
    CountryNode& node = countries[root];
    node.id = root;
    if (league.getId() == root) node.name = Competitions::leagueName(league);
    node.divisions.emplace_back(league.getId(),
                                Competitions::leagueName(league));
  }
  for (auto& [id, country] : countries)
  {
    // Top division first, lower divisions by tier order of their ids.
    std::ranges::sort(country.divisions,
                      [id](const auto& a, const auto& b)
                      {
                        if ((a.first == id) != (b.first == id))
                          return a.first == id;
                        return a.first < b.first;
                      });
    const Continent continent = countryContinent(id);
    auto node = std::ranges::find(world, continent, &ContinentNode::continent);
    if (node == world.end())
    {
      world.push_back({continent, LOC(continentKey(continent)), {}});
      node = std::prev(world.end());
    }
    node->countries.push_back(std::move(country));
  }
  std::ranges::sort(world, {}, &ContinentNode::continent);
  for (ContinentNode& continent : world)
    std::ranges::sort(continent.countries, {}, &CountryNode::name);
}

void ScoutingScene::rebuildScouts()
{
  const GameController& controller = guiView->getController();
  scout_lines.clear();
  for (const ScoutSummary& summary : controller.getScoutSummaries())
  {
    ScoutLine line;
    line.summary = summary;
    line.nationality = nationalityName(summary.profile.nationality);
    for (const SpokenLanguage language :
         languagesOf(controller.getScoutExpertise(summary.profile.id)))
    {
      if (!line.languages.empty()) line.languages += " · ";
      line.languages += LANGUAGE_CODES[static_cast<std::size_t>(
          std::to_underlying(language))];
    }
    line.judging = fmt::sprintf(LOC("SCOUTING_JUDGING_SHORT"),
                                summary.profile.judging_ability,
                                summary.profile.judging_potential);
    for (const ScoutAssignment& assignment : controller.getScoutAssignments())
    {
      if (assignment.id != summary.active_assignment_id) continue;
      line.target = targetLabel(assignment.kind, assignment.target_id);
      line.days_left = assignment.duration_days - assignment.days_done;
      line.progress = assignment.duration_days == 0
                          ? 1.0f
                          : static_cast<float>(assignment.days_done) /
                                static_cast<float>(assignment.duration_days);
    }
    scout_lines.push_back(std::move(line));
  }
  if (selectedScoutLine() == nullptr)
  {
    // Open the scout with news first, otherwise the first one.
    const auto news =
        std::ranges::find_if(scout_lines, [](const ScoutLine& line)
                             { return line.summary.unread_reports > 0; });
    if (news != scout_lines.end())
      selected_scout = news->summary.profile.id;
    else if (scout_lines.empty())
      selected_scout = 0;
    else
      selected_scout = scout_lines.front().summary.profile.id;
    send_open = false;
    history_assignment = 0;
    fresh_reports.clear();
  }
  rebuildScoutDetail();
}

const ScoutingScene::ScoutLine* ScoutingScene::selectedScoutLine() const
{
  const auto found = std::ranges::find_if(
      scout_lines, [this](const ScoutLine& line)
      { return line.summary.profile.id == selected_scout; });
  return found == scout_lines.end() ? nullptr : std::to_address(found);
}

void ScoutingScene::selectScout(uint32_t scoutId)
{
  if (scoutId == selected_scout) return;
  selected_scout = scoutId;
  send_open = false;
  history_assignment = 0;
  fresh_reports.clear();
  // A new page suggests the destination where this scout does best.
  send_kind = ScoutTargetKind::League;
  send_target = 0;
  player_query.fill('\0');
  player_matches.clear();
  rebuildScoutDetail();
}

void ScoutingScene::rebuildScoutDetail()
{
  GameController& controller = guiView->getController();
  auto line = std::ranges::find_if(
      scout_lines, [this](const ScoutLine& entry)
      { return entry.summary.profile.id == selected_scout; });
  if (line == scout_lines.end()) return;
  const uint32_t scoutId = selected_scout;

  selected_expertise = controller.getScoutExpertise(scoutId);
  languages_full.clear();
  for (const SpokenLanguage language : languagesOf(selected_expertise))
  {
    if (!languages_full.empty()) languages_full += ", ";
    languages_full += LOC(spokenLanguageKey(language));
  }
  std::vector<std::pair<LeagueID, uint16_t>> experience(
      selected_expertise.league_days.begin(),
      selected_expertise.league_days.end());
  std::ranges::sort(experience, [](const auto& a, const auto& b)
                    { return a.second > b.second; });
  expertise_lines.clear();
  for (const auto& [league_id, days] : experience)
  {
    if (expertise_lines.size() >= EXPERTISE_LINES) break;
    const auto league = controller.getLeagueById(league_id);
    if (!league) continue;
    expertise_lines.push_back(
        days >= 60
            ? fmt::sprintf(LOC("SCOUTING_EXPERIENCE_YEARS"),
                           Competitions::leagueName(league->get()).c_str(),
                           static_cast<double>(days / DAYS_PER_YEAR))
            : fmt::sprintf(LOC("SCOUTING_EXPERIENCE_DAYS"),
                           Competitions::leagueName(league->get()).c_str(),
                           static_cast<int>(days)));
  }

  // Reports he filed that were never opened are highlighted while the page
  // stays open, then count as read.
  if (line->summary.unread_reports > 0)
  {
    for (const ScoutReport& report : controller.getScoutReports())
    {
      if (report.scout_id == scoutId && !report.seen)
        fresh_reports.push_back(report.id);
    }
    controller.markScoutReportsSeen(scoutId);
    line->summary.unread_reports = 0;
  }

  scout_history.clear();
  const auto& assignments = controller.getScoutAssignments();
  for (auto it = assignments.rbegin(); it != assignments.rend(); ++it)
  {
    if (it->scout_id != scoutId || !it->finished) continue;
    AssignmentLine entry{*it,
                         std::string(LOC(scoutTargetKindKey(it->kind))) +
                             " · " + targetLabel(it->kind, it->target_id),
                         Format::money(it->cost),
                         {}};
    entry.period_text = fmt::sprintf(LOC("SCOUTING_PERIOD_RANGE"),
                                     Format::dayMonth(it->start_date).c_str(),
                                     static_cast<int>(it->days_done));
    scout_history.push_back(std::move(entry));
  }
  if (history_assignment == 0 && !scout_history.empty())
    history_assignment = scout_history.front().assignment.id;

  picker_multipliers.clear();
  const auto remember = [&](ScoutTargetKind kind, uint32_t id)
  {
    picker_multipliers[pickerKey(kind, id)] =
        controller.getScoutEffectiveness(scoutId, kind, id).multiplier;
  };
  remember(ScoutTargetKind::FreeAgents, 0);
  for (const ContinentNode& continent : world)
  {
    remember(ScoutTargetKind::Region,
             static_cast<uint32_t>(std::to_underlying(continent.continent)));
    for (const CountryNode& country : continent.countries)
    {
      remember(ScoutTargetKind::Country, country.id);
      for (const auto& [league_id, name] : country.divisions)
        remember(ScoutTargetKind::League, league_id);
    }
  }

  if (line->summary.status == ScoutStatus::OnAssignment)
  {
    for (const ScoutAssignment& assignment : assignments)
    {
      if (assignment.id == line->summary.active_assignment_id)
      {
        send_kind = assignment.kind;
        send_target = assignment.target_id;
      }
    }
  }
  else if (send_target == 0 && send_kind != ScoutTargetKind::FreeAgents)
  {
    // Suggest the country where he will do best.
    float best = 0.0f;
    for (const ContinentNode& continent : world)
    {
      for (const CountryNode& country : continent.countries)
      {
        const float multiplier =
            picker_multipliers[pickerKey(ScoutTargetKind::Country, country.id)];
        if (multiplier <= best) continue;
        best = multiplier;
        send_kind = ScoutTargetKind::Country;
        send_target = country.id;
      }
    }
  }
  rebuildScoutReportList();
  send_dirty = true;
}

void ScoutingScene::rebuildScoutReportList()
{
  const GameController& controller = guiView->getController();
  const ScoutLine* line = selectedScoutLine();
  scout_reports.clear();
  if (line == nullptr) return;
  const uint32_t assignmentId =
      line->summary.status == ScoutStatus::OnAssignment
          ? line->summary.active_assignment_id
          : history_assignment;
  for (const ScoutReport& report : controller.getScoutReports())
  {
    if (report.scout_id != selected_scout ||
        report.assignment_id != assignmentId)
      continue;
    ReportLine entry = makeReportLine(report);
    entry.shortlisted = controller.isShortlisted(report.player_id);
    scout_reports.push_back(std::move(entry));
  }
  sortScoutReports();
}

void ScoutingScene::sortScoutReports()
{
  const auto key = [this](const ReportLine& a, const ReportLine& b)
  {
    using enum ReportColumn;
    switch (report_sort)
    {
      case PLAYER:
        return UI::compare(a.player_name, b.player_name);
      case GRADE:
        // A is the best grade: sort it as the highest.
        return UI::compare(b.report.grade, a.report.grade);
      case ABILITY:
        return UI::compare(a.report.overall, b.report.overall);
      case POTENTIAL:
        return UI::compare(a.report.potential_high, b.report.potential_high);
      case FEE:
        return UI::compare(a.report.estimated_fee, b.report.estimated_fee);
      case DATE:
        return UI::compare(a.report.id, b.report.id);
    }
    return 0;
  };
  std::ranges::stable_sort(scout_reports,
                           [&](const ReportLine& a, const ReportLine& b)
                           {
                             const int order = key(a, b);
                             return report_sort_ascending ? order < 0
                                                          : order > 0;
                           });
}

void ScoutingScene::rebuildSendPreview()
{
  using enum ScoutTargetKind;
  send_dirty = false;
  const GameController& controller = guiView->getController();
  send_cost =
      send_target == 0 && send_kind != FreeAgents && send_kind != Region
          ? 0
          : controller.getScoutAssignmentCost(send_kind, send_target,
                                              static_cast<uint16_t>(send_days));
  send_effect =
      controller.getScoutEffectiveness(selected_scout, send_kind, send_target);
  send_target_text = send_kind == Player && send_target == 0
                         ? std::string()
                         : targetLabel(send_kind, send_target);
  effect_lines.clear();
  for (const EffectFactor& factor : send_effect.factors)
    effect_lines.emplace_back(effectText(factor), factor.delta);
}

void ScoutingScene::rebuildPlayerMatches()
{
  player_matches.clear();
  const std::string query = lower(player_query.data());
  if (query.size() < 2) return;
  GameController& controller = guiView->getController();
  const auto managed = controller.getManagedTeam();
  for (const Player& player : controller.getGameData()->getPlayersVector())
  {
    if (managed && player.getTeamId() == managed->get().getId()) continue;
    if (!lower(player.getName()).contains(query)) continue;
    player_matches.emplace_back(player.getId(), player.getName());
    if (player_matches.size() >= PLAYER_MATCH_LIMIT) break;
  }
}

std::string ScoutingScene::targetLabel(ScoutTargetKind kind,
                                       uint32_t targetId) const
{
  const GameController& controller = guiView->getController();
  using enum ScoutTargetKind;
  switch (kind)
  {
    case Player:
      if (const auto player = controller.getGameData()->getPlayer(targetId))
        return player->get().getName();
      break;
    case League:
      if (const auto league =
              controller.getLeagueById(static_cast<LeagueID>(targetId)))
        return Competitions::leagueName(league->get());
      break;
    case Country:
      if (const auto league =
              controller.getLeagueById(static_cast<LeagueID>(targetId)))
        return fmt::sprintf(LOC("SCOUTING_COUNTRY_NODE"),
                            Competitions::leagueName(league->get()).c_str());
      break;
    case Region:
      return LOC(continentKey(static_cast<Continent>(targetId)));
    case FreeAgents:
      return LOC("SCOUTING_TARGET_FREE_AGENTS");
  }
  return {};
}

std::string ScoutingScene::effectText(const EffectFactor& factor) const
{
  const GameController& controller = guiView->getController();
  const auto leagueName = [&controller](uint32_t id)
  {
    const auto league = controller.getLeagueById(static_cast<LeagueID>(id));
    return league ? Competitions::leagueName(league->get()) : std::string();
  };
  const auto languageName = [](uint32_t id)
  {
    return std::string(LOC(spokenLanguageKey(static_cast<SpokenLanguage>(id))));
  };
  switch (factor.kind)
  {
    case EffectFactorKind::Judging:
      return fmt::sprintf(LOC("SCOUT_EFFECT_JUDGING"),
                          static_cast<int>(factor.subject));
    case EffectFactorKind::HomeCountry:
      return fmt::sprintf(LOC("SCOUT_EFFECT_HOME"),
                          leagueName(factor.subject).c_str());
    case EffectFactorKind::KnowsLeague:
      return fmt::sprintf(LOC("SCOUT_EFFECT_KNOWS_LEAGUE"),
                          leagueName(factor.subject).c_str());
    case EffectFactorKind::KnowsCountry:
      return fmt::sprintf(LOC("SCOUT_EFFECT_KNOWS_COUNTRY"),
                          leagueName(factor.subject).c_str());
    case EffectFactorKind::SpeaksLanguage:
      return fmt::sprintf(LOC("SCOUT_EFFECT_SPEAKS"),
                          languageName(factor.subject).c_str());
    case EffectFactorKind::LanguageBarrier:
      return fmt::sprintf(LOC("SCOUT_EFFECT_LANGUAGE_BARRIER"),
                          languageName(factor.subject).c_str());
    case EffectFactorKind::FarFromHome:
      return fmt::sprintf(
          LOC("SCOUT_EFFECT_FAR"),
          LOC(continentKey(static_cast<Continent>(factor.subject))));
  }
  return {};
}

void ScoutingScene::sendScout(ScoutTargetKind kind, uint32_t targetId)
{
  GameController& controller = guiView->getController();
  // The scout whose page is open goes if he is free, else the first free one.
  uint32_t scoutId = 0;
  if (const ScoutLine* line = selectedScoutLine();
      line != nullptr && line->summary.status != ScoutStatus::OnAssignment)
    scoutId = line->summary.profile.id;
  for (const ScoutLine& line : scout_lines)
  {
    if (scoutId != 0) break;
    if (line.summary.status != ScoutStatus::OnAssignment)
      scoutId = line.summary.profile.id;
  }
  if (scoutId == 0)
  {
    showToast(LOC(scout_lines.empty() ? "SCOUTING_NO_SCOUTS"
                                      : "SCOUTING_NO_IDLE_SCOUT"),
              true);
    return;
  }
  const ScoutAssignError error = controller.startScoutAssignment(
      scoutId, kind, targetId, static_cast<uint16_t>(send_days));
  showToast(LOC(scoutAssignErrorKey(error)), error != ScoutAssignError::None);
  if (error != ScoutAssignError::None) return;
  send_open = false;
  selected_scout = scoutId;
  rebuildScouts();
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void ScoutingScene::renderOverview()
{
  if (scout_lines.empty())
  {
    UI::emptyState(LOC("SCOUTING_NO_SCOUTS_TITLE"), LOC("SCOUTING_NO_SCOUTS"));
    return;
  }
  const ImVec2 available = ImGui::GetContentRegionAvail();
  if (available.x >= TWO_COLUMN_MIN_WIDTH * dpi())
  {
    const float listWidth =
        std::clamp(available.x * 0.3f, SCOUT_LIST_WIDTH * dpi(),
                   SCOUT_LIST_MAX_WIDTH * dpi());
    renderScoutList(listWidth, available.y);
    ImGui::SameLine();
    renderScoutDetail(available.y);
    return;
  }
  // Narrow windows: the list on top (at least three scouts in view, all of
  // them when they fit in half the page), the scout's page below. The page
  // scrolls when both do not fit.
  const float rowHeight = ImGui::GetTextLineHeightWithSpacing() * 3.0f +
                          2.0f * Theme::Space::S * dpi() +
                          ImGui::GetStyle().ItemSpacing.y;
  const float header = ImGui::GetTextLineHeightWithSpacing() * 2.5f;
  const float natural =
      rowHeight * static_cast<float>(scout_lines.size()) + header;
  const float listHeight = std::min(
      natural,
      std::max(available.y * 0.5f, rowHeight * STACKED_LIST_ROWS + header));
  renderScoutList(0.0f, listHeight);
  renderScoutDetail(std::max(ImGui::GetContentRegionAvail().y,
                             STACKED_DETAIL_MIN_HEIGHT * dpi()));
}

void ScoutingScene::renderScoutList(float width, float height)
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginCard("scout_list", LOC("SCOUTING_SCOUTS"), ImVec2(width, height),
                true);
  const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
  const float padding = Theme::Space::S * dpi();
  const float rowHeight = lineHeight * 3.0f + 2.0f * padding;
  for (const ScoutLine& line : scout_lines)
  {
    const ScoutSummary& summary = line.summary;
    ImGui::PushID(static_cast<int>(summary.profile.id));
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float rowWidth = ImGui::GetContentRegionAvail().x;
    if (ImGui::InvisibleButton("##scout", ImVec2(rowWidth, rowHeight)))
      selectScout(summary.profile.id);
    const bool selected = summary.profile.id == selected_scout;
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 end(start.x + rowWidth, start.y + rowHeight);
    if (selected || hovered)
      drawList->AddRectFilled(
          start, end,
          Theme::toU32(selected ? palette.accent : palette.raised,
                       selected ? 0.14f : 1.0f),
          4.0f * dpi());
    if (selected)
      drawList->AddRectFilled(start, ImVec2(start.x + 3.0f * dpi(), end.y),
                              Theme::toU32(palette.accent), 2.0f * dpi());

    const float left = start.x + Theme::Space::M * dpi();
    float right = end.x - padding;
    float y = start.y + padding;
    // Line 1: name and the new-reports badge.
    if (summary.unread_reports > 0)
    {
      const std::string badge =
          fmt::sprintf(LOC("SCOUTING_NEW_REPORTS"),
                       static_cast<int>(summary.unread_reports));
      const ImVec2 size = ImGui::CalcTextSize(badge.c_str());
      const ImVec2 badgeStart(right - size.x - 2.0f * padding, y);
      drawList->AddRectFilled(badgeStart,
                              ImVec2(right, y + size.y + 2.0f * dpi()),
                              Theme::toU32(palette.accent), 8.0f * dpi());
      drawList->AddText(ImVec2(badgeStart.x + padding, y + 1.0f * dpi()),
                        Theme::toU32(palette.on_accent), badge.c_str());
      right = badgeStart.x - padding;
    }
    UI::drawTextFitted(drawList, ImVec2(left, y), Theme::toU32(palette.text),
                       summary.profile.name, right - left);
    // Line 2: nationality, languages and judging.
    y += lineHeight;
    const std::string origin = line.nationality + "  ·  " + line.languages;
    const float judgingWidth = ImGui::CalcTextSize(line.judging.c_str()).x;
    const float lineRight = end.x - padding;
    UI::drawTextFitted(drawList, ImVec2(left, y), Theme::toU32(palette.muted),
                       origin, lineRight - left - judgingWidth - padding);
    drawList->AddText(ImVec2(lineRight - judgingWidth, y),
                      Theme::toU32(palette.muted), line.judging.c_str());
    // Line 3: status.
    y += lineHeight;
    std::string status;
    ImVec4 statusColor = palette.positive;
    using enum ScoutStatus;
    switch (summary.status)
    {
      case OnAssignment:
        status = fmt::sprintf(
            Format::plural("SCOUTING_STATUS_ASSIGNED", line.days_left),
            line.target.c_str(), line.days_left);
        statusColor = palette.warning;
        break;
      case IdleWithHistory:
        status = fmt::sprintf(LOC("SCOUTING_STATUS_IDLE_HISTORY"),
                              static_cast<int>(summary.total_reports));
        break;
      case IdleNew:
        status = LOC("SCOUTING_STATUS_IDLE_NEW");
        statusColor = palette.info;
        break;
    }
    const float dot = 3.5f * dpi();
    drawList->AddCircleFilled(
        ImVec2(left + dot, y + ImGui::GetTextLineHeight() * 0.5f), dot,
        Theme::toU32(statusColor));
    UI::drawTextFitted(drawList, ImVec2(left + 3.0f * dot, y),
                       Theme::toU32(palette.text), status,
                       lineRight - left - 3.0f * dot);
    if (summary.status == ScoutStatus::OnAssignment)
    {
      const float barY = end.y - 3.0f * dpi();
      drawList->AddRectFilled(ImVec2(left, barY),
                              ImVec2(lineRight, end.y - dpi()),
                              Theme::toU32(palette.raised));
      drawList->AddRectFilled(
          ImVec2(left, barY),
          ImVec2(left + (lineRight - left) * line.progress, end.y - dpi()),
          Theme::toU32(palette.warning));
    }
    ImGui::PopID();
  }
  UI::endCard();
}

void ScoutingScene::renderScoutDetail(float height)
{
  const ScoutLine* line = selectedScoutLine();
  UI::beginCard("scout_detail", nullptr, ImVec2(0.0f, height), true);
  if (line == nullptr)
  {
    UI::emptyState(LOC("SCOUTING_SELECT_SCOUT_TITLE"),
                   LOC("SCOUTING_SELECT_SCOUT_BODY"));
    UI::endCard();
    return;
  }
  if (send_dirty) rebuildSendPreview();
  renderScoutHeader(*line);
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * dpi()));
  switch (line->summary.status)
  {
    case ScoutStatus::OnAssignment:
      renderActiveAssignment(*line);
      ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * dpi()));
      UI::sectionLabel(LOC("SCOUTING_REPORTS_SO_FAR"));
      renderScoutReports("scout_live_reports",
                         std::max(REPORTS_MIN_HEIGHT * dpi(),
                                  ImGui::GetContentRegionAvail().y));
      break;
    case ScoutStatus::IdleWithHistory:
      if (send_open)
      {
        if (ImGui::Button(LOC("SCOUTING_BACK_TO_HISTORY"))) send_open = false;
        renderSendFlow(*line);
        break;
      }
      if (UI::primaryButton(LOC("SCOUTING_SEND_SOMEWHERE")))
      {
        send_open = true;
        send_dirty = true;
      }
      ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * dpi()));
      renderScoutHistory();
      break;
    case ScoutStatus::IdleNew:
      ImGui::TextWrapped("%s", fmt::sprintf(LOC("SCOUTING_SEND_INTRO"),
                                            line->summary.profile.name.c_str())
                                   .c_str());
      renderSendFlow(*line);
      break;
  }
  UI::endCard();
}

void ScoutingScene::renderScoutHeader(const ScoutLine& line) const
{
  const Theme::Palette& palette = Theme::palette();
  const ScoutProfile& scout = line.summary.profile;
  {
    const Theme::ScopedText title(Theme::Text::TITLE);
    ImGui::TextUnformatted(scout.name.c_str());
  }
  ImGui::TextColored(
      palette.muted, "%s",
      fmt::sprintf(LOC("SCOUTING_ORIGIN"), line.nationality.c_str(),
                   languages_full.c_str())
          .c_str());
  const float available = ImGui::GetContentRegionAvail().x;
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  const bool twoColumns = available >= 560.0f * dpi();
  const float barWidth =
      std::min(HEADER_BAR_MAX_WIDTH * dpi(),
               twoColumns ? (available - gap) * 0.5f : available);
  const float labelWidth = std::min(KEY_WIDTH * dpi(), barWidth * 0.45f);
  UI::attributeBar(LOC("SCOUTING_JUDGING_ABILITY"), scout.judging_ability,
                   labelWidth, 100.0f, barWidth);
  if (twoColumns) ImGui::SameLine();
  UI::attributeBar(LOC("SCOUTING_JUDGING_POTENTIAL"), scout.judging_potential,
                   labelWidth, 100.0f, barWidth);
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(palette.muted, "%s", LOC("SCOUTING_EXPERIENCE"));
  if (expertise_lines.empty())
  {
    ImGui::SameLine();
    ImGui::TextColored(palette.faint, "%s", LOC("SCOUTING_EXPERIENCE_NONE"));
  }
  for (const std::string& experience : expertise_lines)
  {
    UI::sameLineIfFits(ImGui::CalcTextSize(experience.c_str()).x +
                       2.0f * Theme::Space::S * dpi());
    UI::badge(experience.c_str(), palette.info);
  }
}

void ScoutingScene::renderActiveAssignment(const ScoutLine& line)
{
  const Theme::Palette& palette = Theme::palette();
  GameController& controller = guiView->getController();
  const ScoutAssignment* assignment = nullptr;
  for (const ScoutAssignment& entry : controller.getScoutAssignments())
  {
    if (entry.id == line.summary.active_assignment_id) assignment = &entry;
  }
  if (assignment == nullptr) return;
  UI::beginAutoHeightCard("scout_active", LOC("SCOUTING_CURRENT_ASSIGNMENT"));
  {
    const Theme::ScopedText title(Theme::Text::TITLE);
    UI::textFitted(std::string(LOC(scoutTargetKindKey(assignment->kind))) +
                       " · " + line.target,
                   ImGui::GetContentRegionAvail().x, palette.text);
  }
  const std::string progress = fmt::sprintf(
      LOC("SCOUTING_PERIOD"), assignment->days_done, assignment->duration_days);
  ImGui::ProgressBar(line.progress, ImVec2(-FLT_MIN, 0.0f), progress.c_str());
  const float keyWidth = KEY_WIDTH * dpi();
  UI::keyValue(LOC("SCOUTING_DAYS_LEFT"),
               std::to_string(line.days_left).c_str(), keyWidth);
  UI::keyValue(LOC("SCOUTING_COL_OBSERVED"),
               std::format("{} / {}", assignment->players_observed,
                           assignment->reports_filed)
                   .c_str(),
               keyWidth);
  UI::keyValue(LOC("SCOUTING_COL_COST"),
               Format::money(assignment->cost).c_str(), keyWidth);
  ImGui::PushStyleColor(ImGuiCol_Text, multiplierColor(send_effect.multiplier));
  UI::keyValue(LOC("SCOUTING_EFFECTIVENESS"),
               multiplierText(send_effect.multiplier).c_str(), keyWidth);
  ImGui::PopStyleColor();
  if (ImGui::IsItemHovered() && !effect_lines.empty())
  {
    ImGui::BeginTooltip();
    for (const EffectLine& effect : effect_lines)
      ImGui::TextColored(
          effect.delta >= 0.0f ? palette.positive : palette.negative,
          "%+.0f%%  %s", static_cast<double>(effect.delta * 100.0f),
          effect.text.c_str());
    ImGui::EndTooltip();
  }
  if (ImGui::Button(LOC("SCOUTING_RECALL")) &&
      controller.cancelScoutAssignment(assignment->id))
  {
    showToast(LOC("SCOUTING_RECALLED"));
    rebuildScouts();
  }
  else if (ImGui::IsItemHovered())
  {
    ImGui::SetTooltip("%s", LOC("SCOUTING_RECALL_HINT"));
  }
  UI::endCard();
}

void ScoutingScene::renderScoutHistory()
{
  UI::sectionLabel(LOC("SCOUTING_PREVIOUS_ASSIGNMENTS"));
  const auto rows =
      static_cast<float>(std::min<std::size_t>(scout_history.size(), 5));
  if (const float tableHeight =
          (rows + 1.3f) * ImGui::GetFrameHeightWithSpacing();
      UI::beginDataTable("scout_history", 4,
                         ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                             ImGuiTableFlags_ScrollY |
                             ImGuiTableFlags_SizingFixedFit,
                         480.0f, ImVec2(0.0f, tableHeight)))
  {
    ImGui::TableSetupColumn(LOC("SCOUTING_COL_TARGET"),
                            ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn(LOC("SCOUTING_COL_PERIOD"));
    ImGui::TableSetupColumn(LOC("SCOUTING_COL_OBSERVED"));
    ImGui::TableSetupColumn(LOC("SCOUTING_COL_COST"));
    UI::staticHeadersRow();
    for (const AssignmentLine& entry : scout_history)
    {
      const ScoutAssignment& assignment = entry.assignment;
      ImGui::PushID(static_cast<int>(assignment.id));
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      if (ImGui::Selectable(entry.target_text.c_str(),
                            history_assignment == assignment.id,
                            ImGuiSelectableFlags_SpanAllColumns))
      {
        history_assignment = assignment.id;
        rebuildScoutReportList();
      }
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(entry.period_text.c_str());
      ImGui::TableNextColumn();
      ImGui::Text("%d / %d", assignment.players_observed,
                  assignment.reports_filed);
      ImGui::TableNextColumn();
      UI::textRight(entry.cost_text.c_str());
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * dpi()));
  UI::sectionLabel(LOC("SCOUTING_ASSIGNMENT_REPORTS"));
  renderScoutReports(
      "scout_past_reports",
      std::max(REPORTS_MIN_HEIGHT * dpi(), ImGui::GetContentRegionAvail().y));
}

void ScoutingScene::renderScoutReports(const char* id, float height)
{
  const Theme::Palette& palette = Theme::palette();
  GameController& controller = guiView->getController();
  if (scout_reports.empty())
  {
    ImGui::TextColored(palette.muted, "%s", LOC("SCOUTING_SCOUT_NO_REPORTS"));
    return;
  }
  if (!UI::beginDataTable(
          id, 7,
          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
              ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit |
              ImGuiTableFlags_Sortable,
          600.0f, ImVec2(0.0f, height)))
    return;
  const auto column =
      [](const char* key, ReportColumn user, ImGuiTableColumnFlags extra = 0)
  {
    ImGui::TableSetupColumn(LOC(key), extra, 0.0f,
                            static_cast<ImGuiID>(std::to_underlying(user)));
  };
  column("SCOUTING_COL_PLAYER", ReportColumn::PLAYER,
         ImGuiTableColumnFlags_WidthStretch);
  column("SCOUTING_COL_GRADE", ReportColumn::GRADE,
         ImGuiTableColumnFlags_PreferSortDescending);
  column("SCOUTING_COL_ABILITY", ReportColumn::ABILITY,
         ImGuiTableColumnFlags_PreferSortDescending);
  column("SCOUTING_COL_POTENTIAL", ReportColumn::POTENTIAL,
         ImGuiTableColumnFlags_PreferSortDescending);
  column("SCOUTING_COL_FEE", ReportColumn::FEE,
         ImGuiTableColumnFlags_PreferSortDescending);
  column("SCOUTING_COL_DATE", ReportColumn::DATE,
         ImGuiTableColumnFlags_PreferSortDescending |
             ImGuiTableColumnFlags_DefaultSort);
  ImGui::TableSetupColumn("", ImGuiTableColumnFlags_NoSort);
  ImGui::TableHeadersRow();
  if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs();
      specs != nullptr && specs->SpecsDirty && specs->SpecsCount > 0)
  {
    report_sort = static_cast<ReportColumn>(specs->Specs[0].ColumnUserID);
    report_sort_ascending =
        specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
    sortScoutReports();
    specs->SpecsDirty = false;
  }
  ImGuiListClipper clipper;
  clipper.Begin(static_cast<int>(scout_reports.size()));
  while (clipper.Step())
  {
    for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
    {
      const ReportLine& line = scout_reports[static_cast<std::size_t>(index)];
      const ScoutReport& report = line.report;
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::PushID(static_cast<int>(report.id));
      if (line.fresh)
      {
        UI::badge(LOC("SCOUTING_BADGE_NEW"), palette.accent);
        ImGui::SameLine();
      }
      playerCell(report.player_id, line.player_name,
                 selected_player == report.player_id);
      ImGui::TableNextColumn();
      UI::badge(gradeLabel(report.grade), gradeColor(report.grade));
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", LOC(scoutGradeKey(report.grade)));
      ImGui::TableNextColumn();
      ImGui::TextColored(
          Theme::ratingColor(static_cast<double>(report.overall)), "%s",
          line.ability_text.c_str());
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", fmt::sprintf(LOC("SCOUTING_CONFIDENCE_HINT"),
                                             report.confidence)
                                    .c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(line.potential_text.c_str());
      ImGui::TableNextColumn();
      UI::textRight(line.fee_text.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(line.date_text.c_str());
      ImGui::TableNextColumn();
      if (line.shortlisted)
      {
        UI::badge(LOC("SCOUTING_BADGE_SHORTLISTED"), palette.accent);
      }
      else if (ImGui::SmallButton(LOC("SCOUTING_SHORTLIST_ACTION")))
      {
        if (controller.addToShortlist(report.player_id))
        {
          for (ReportLine& other : scout_reports)
          {
            if (other.report.player_id == report.player_id)
              other.shortlisted = true;
          }
          showToast(LOC("SCOUTING_SHORTLIST_ADDED"));
          rebuildShortlist();
          search_dirty = true;
        }
        else
        {
          showToast(LOC("SCOUTING_SHORTLIST_REFUSED"), true);
        }
      }
      ImGui::PopID();
    }
  }
  ImGui::EndTable();
}

void ScoutingScene::renderSendFlow(const ScoutLine& line)
{
  if (const float available = ImGui::GetContentRegionAvail().x;
      available >= SEND_TWO_COLUMN_MIN_WIDTH * dpi())
  {
    const float height = std::max(PICKER_STACKED_HEIGHT * dpi(),
                                  ImGui::GetContentRegionAvail().y);
    renderWorldPicker(available * 0.46f, height);
    ImGui::SameLine();
    // A child (not a group) so the summary's own layout starts at its left.
    ImGui::BeginChild("##send_summary", ImVec2(0.0f, height));
    renderSendSummary(line);
    ImGui::EndChild();
    return;
  }
  renderWorldPicker(0.0f, PICKER_STACKED_HEIGHT * dpi());
  renderSendSummary(line);
}

void ScoutingScene::renderWorldPicker(float width, float height)
{
  const Theme::Palette& palette = Theme::palette();
  ImGui::BeginChild("##world_picker", ImVec2(width, height),
                    ImGuiChildFlags_Borders);
  const auto pick = [this](ScoutTargetKind kind, uint32_t id)
  {
    if (send_kind == kind && send_target == id) return;
    send_kind = kind;
    send_target = id;
    send_dirty = true;
  };
  // Expected effectiveness of an entry in the second column.
  const auto multiplierTag = [&](ScoutTargetKind kind, uint32_t id)
  {
    ImGui::TableNextColumn();
    const auto found = picker_multipliers.find(pickerKey(kind, id));
    if (found == picker_multipliers.end()) return;
    UI::textRightColored(multiplierColor(found->second),
                         multiplierText(found->second).c_str());
  };
  // Tree node whose label (the node's ID too) ends with an ellipsis when
  // the column is narrow; the full name is in the tooltip.
  const auto node = [&palette](const char* label, ImGuiTreeNodeFlags flags)
  {
    const bool open = ImGui::TreeNodeEx(label, flags, "%s", "");
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const float textX = min.x + ImGui::GetTreeNodeToLabelSpacing();
    const float textY =
        min.y + (max.y - min.y - ImGui::GetTextLineHeight()) * 0.5f;
    if (const bool cut = UI::drawTextFitted(
            ImGui::GetWindowDrawList(), ImVec2(textX, textY),
            Theme::toU32(palette.text), label, max.x - textX);
        cut && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
      ImGui::SetTooltip("%s", label);
    return open;
  };
  const auto leaf = [&](const char* label, ScoutTargetKind kind, uint32_t id)
  {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    node(label, ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                    ImGuiTreeNodeFlags_SpanAvailWidth |
                    (send_kind == kind && send_target == id
                         ? ImGuiTreeNodeFlags_Selected
                         : 0));
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) pick(kind, id);
    multiplierTag(kind, id);
  };
  const auto branch = [&](const char* label, ScoutTargetKind kind, uint32_t id,
                          bool defaultOpen)
  {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    const bool open =
        node(label, ImGuiTreeNodeFlags_OpenOnArrow |
                        ImGuiTreeNodeFlags_OpenOnDoubleClick |
                        ImGuiTreeNodeFlags_SpanAvailWidth |
                        (defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : 0) |
                        (send_kind == kind && send_target == id
                             ? ImGuiTreeNodeFlags_Selected
                             : 0));
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) pick(kind, id);
    multiplierTag(kind, id);
    return open;
  };

  UI::sectionLabel(LOC("SCOUTING_PICK_TITLE"));
  if (!ImGui::BeginTable("##picker", 2, ImGuiTableFlags_SizingFixedFit))
  {
    ImGui::EndChild();
    return;
  }
  ImGui::TableSetupColumn("##entry", ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableSetupColumn("##effect", ImGuiTableColumnFlags_WidthFixed,
                          ImGui::CalcTextSize("×0.00").x);
  leaf(LOC("SCOUTING_TARGET_FREE_AGENTS"), ScoutTargetKind::FreeAgents, 0);
  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  node(LOC("SCOUTING_PICK_PLAYER"),
       ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
           ImGuiTreeNodeFlags_SpanAvailWidth |
           (send_kind == ScoutTargetKind::Player ? ImGuiTreeNodeFlags_Selected
                                                 : 0));
  if (ImGui::IsItemClicked() && send_kind != ScoutTargetKind::Player)
    pick(ScoutTargetKind::Player, 0);
  const Continent home = homeContinent(selected_expertise.nationality);
  for (const ContinentNode& continent : world)
  {
    ImGui::PushID(static_cast<int>(std::to_underlying(continent.continent)));
    if (branch(continent.name.c_str(), ScoutTargetKind::Region,
               static_cast<uint32_t>(std::to_underlying(continent.continent)),
               continent.continent == home))
    {
      for (const CountryNode& country : continent.countries)
      {
        ImGui::PushID(country.id);
        if (const std::string label = fmt::sprintf(LOC("SCOUTING_COUNTRY_NODE"),
                                                   country.name.c_str());
            branch(label.c_str(), ScoutTargetKind::Country, country.id, false))
        {
          for (const auto& [league_id, name] : country.divisions)
          {
            ImGui::PushID(league_id);
            leaf(name.c_str(), ScoutTargetKind::League, league_id);
            ImGui::PopID();
          }
          ImGui::TreePop();
        }
        ImGui::PopID();
      }
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
  ImGui::EndTable();
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * dpi()));
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(palette.faint, "%s", LOC("SCOUTING_PICK_HINT"));
  ImGui::PopTextWrapPos();
  ImGui::EndChild();
}

void ScoutingScene::renderSendSummary(const ScoutLine& line)
{
  const Theme::Palette& palette = Theme::palette();
  const float width = ImGui::GetContentRegionAvail().x;
  UI::sectionLabel(LOC("SCOUTING_SEND_TO"));
  {
    const Theme::ScopedText title(Theme::Text::TITLE);
    UI::textFitted(
        send_target_text.empty() ? std::string(LOC("SCOUTING_PICK_NONE"))
                                 : send_target_text,
        width, send_target_text.empty() ? palette.muted : palette.text);
  }
  if (send_kind == ScoutTargetKind::Player)
  {
    ImGui::SetNextItemWidth(width);
    if (ImGui::InputTextWithHint("##scout_player", LOC("SCOUTING_FILTER_NAME"),
                                 player_query.data(), player_query.size()))
      rebuildPlayerMatches();
    for (const auto& [id, name] : player_matches)
    {
      ImGui::PushID(static_cast<int>(id));
      if (ImGui::Selectable(name.c_str(), send_target == id))
      {
        send_target = id;
        send_dirty = true;
      }
      ImGui::PopID();
    }
  }
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * dpi()));
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(LOC("SCOUTING_FORM_DURATION"));
  ImGui::SameLine(KEY_WIDTH * dpi());
  ImGui::SetNextItemWidth(std::max(120.0f * dpi(), width - KEY_WIDTH * dpi()));
  if (ImGui::SliderInt("##send_days", &send_days,
                       ScoutingTuning::MIN_DURATION_DAYS,
                       ScoutingTuning::MAX_DURATION_DAYS, LOC("SCOUTING_DAYS")))
    send_dirty = true;
  if (send_dirty) rebuildSendPreview();
  const float keyWidth = KEY_WIDTH * dpi();
  UI::keyValue(LOC("SCOUTING_COL_COST"),
               send_cost > 0 ? Format::money(send_cost).c_str() : "–",
               keyWidth);

  const float multiplier = send_effect.multiplier;
  const float fraction = (multiplier - ScoutExpertiseModel::MIN_MULTIPLIER) /
                         (ScoutExpertiseModel::MAX_MULTIPLIER -
                          ScoutExpertiseModel::MIN_MULTIPLIER);
  UI::meter(LOC("SCOUTING_EFFECTIVENESS"), fraction, keyWidth,
            multiplierColor(multiplier), multiplierText(multiplier).c_str());
  for (const EffectLine& effect : effect_lines)
  {
    ImGui::TextColored(
        effect.delta >= 0.0f ? palette.positive : palette.negative, "%+4.0f%%",
        static_cast<double>(effect.delta * 100.0f));
    ImGui::SameLine();
    UI::textFitted(effect.text, ImGui::GetContentRegionAvail().x, palette.text);
  }
  const bool watchesMany = send_kind != ScoutTargetKind::Player;
  const int perDay = std::max(
      1, static_cast<int>(std::lround(
             static_cast<float>(ScoutingTuning::COVERAGE_PLAYERS_PER_DAY) *
             multiplier)));
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
  ImGui::TextColored(
      palette.faint, "%s",
      watchesMany
          ? fmt::sprintf(LOC("SCOUTING_EFFECT_EXPLAIN_MANY"), perDay).c_str()
          : LOC("SCOUTING_EFFECT_EXPLAIN_ONE"));
  ImGui::PopTextWrapPos();
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * dpi()));
  ImGui::BeginDisabled(send_cost <= 0);
  if (UI::primaryButton(fmt::sprintf(LOC("SCOUTING_SEND_NAMED"),
                                     line.summary.profile.name.c_str())
                            .c_str()))
    sendScout(send_kind, send_target);
  ImGui::EndDisabled();
}
