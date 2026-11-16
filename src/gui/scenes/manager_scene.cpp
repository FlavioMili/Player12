// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/manager_scene.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <format>
#include <utility>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/global.h"
#include "global/language_manager.h"
#include "gui/gui_view.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/board.h"
#include "model/competition.h"
#include "model/inbox.h"
#include "model/world_generation.h"
#include "model/world_rng.h"
#include "model/world_tuning.h"

namespace
{
constexpr float TWO_COLUMN_MIN_WIDTH = 980.0f;
constexpr float KEY_WIDTH = 170.0f;
constexpr float DIALOG_WIDTH = 560.0f;
constexpr float FIELD_MIN_WIDTH = 170.0f;
constexpr const char* RESIGN_POPUP_ID = "##resign_job";
constexpr const char* ACCEPT_POPUP_ID = "##accept_job";
constexpr const char* INTERVIEW_POPUP_ID = "##job_interview";
constexpr int MAX_YEARS = 4;
constexpr std::array<const char*, MAX_YEARS> YEAR_LABELS = {"1", "2", "3", "4"};

std::string nationName(Language nationality)
{
  const auto name = languageToString.find(nationality);
  if (name == languageToString.end()) return {};
  const std::string key = "NATION_" + name->second;
  const char* localized = LOC(key.c_str());
  return key == localized ? name->second : std::string(localized);
}

const char* stageKey(ApplicationStage stage)
{
  switch (stage)
  {
    using enum ApplicationStage;
    case Pending:
      return "JOB_STAGE_PENDING";
    case Interview:
      return "JOB_STAGE_INTERVIEW";
    case Rejected:
      return "JOB_STAGE_REJECTED";
    case Offered:
      break;
  }
  return "JOB_STAGE_OFFERED";
}

ImVec4 chanceColor(float chance)
{
  const Theme::Palette& palette = Theme::palette();
  if (chance >= 0.6f) return palette.positive;
  if (chance >= 0.3f) return palette.text;
  if (chance >= 0.1f) return palette.warning;
  return palette.negative;
}

const char* chanceKey(float chance)
{
  if (chance >= 0.6f) return "JOB_CHANCE_GOOD";
  if (chance >= 0.3f) return "JOB_CHANCE_FAIR";
  if (chance >= 0.1f) return "JOB_CHANCE_SLIM";
  return "JOB_CHANCE_REMOTE";
}

std::string clubName(const GameController& controller, TeamID team_id)
{
  const auto team = controller.getTeamById(team_id);
  return team ? team->get().getName() : std::string();
}

std::string leagueName(const GameController& controller, LeagueID league_id)
{
  const auto league = controller.getLeagueById(league_id);
  return league ? Competitions::leagueName(league->get()) : std::string();
}

std::string seasonLabel(std::uint16_t start_year)
{
  return std::format("{}/{:02}", start_year, (start_year + 1) % 100);
}

// Columns hide from the highest priority number down as the card narrows;
// row actions live in the detail strip of the selected row.
const std::array<UI::Column, 7>& vacancyColumns()
{
  static const std::array<UI::Column, 7> columns = {{
      {"JOB_COL_CLUB", 0.0f, 0},
      {"JOB_COL_LEAGUE", 150.0f, 2},
      {"JOB_COL_REPUTATION", 70.0f, 4},
      {"JOB_COL_EXPECTATION", 150.0f, 1},
      {"JOB_COL_LICENCE", 90.0f, 3},
      {"JOB_COL_CHANCE", 110.0f, 0},
      {"JOB_COL_STATUS", 110.0f, 1},
  }};
  return columns;
}

const std::array<UI::Column, 7>& historyColumns()
{
  static const std::array<UI::Column, 7> columns = {{
      {"MANAGER_COL_CLUB", 0.0f, 0},
      {"MANAGER_COL_PERIOD", 190.0f, 1},
      {"MANAGER_COL_LEAGUE", 140.0f, 3},
      {"MANAGER_COL_RECORD", 90.0f, 2},
      {"MANAGER_COL_WIN_RATE", 64.0f, 0},
      {"MANAGER_COL_TROPHIES", 70.0f, 2},
      {"MANAGER_COL_DEPARTURE", 120.0f, 1},
  }};
  return columns;
}

const std::array<UI::Column, 4>& seasonColumns()
{
  static const std::array<UI::Column, 4> columns = {{
      {"MANAGER_COL_SEASON", 80.0f, 0},
      {"MANAGER_COL_CLUB", 0.0f, 0},
      {"MANAGER_COL_FINISH", 90.0f, 0},
      {"MANAGER_COL_EXPECTED", 90.0f, 1},
  }};
  return columns;
}

template <std::size_t N>
std::array<UI::Column, N> localize(const std::array<UI::Column, N>& columns)
{
  std::array<UI::Column, N> localized = columns;
  for (UI::Column& column : localized) column.label = LOC(column.label);
  return localized;
}

/** Raised strip under a table row or inside a card (decorative stripe). */
class Strip
{
 public:
  Strip()
  {
    draw_list->ChannelsSplit(2);
    draw_list->ChannelsSetCurrent(1);
    ImGui::SetCursorScreenPos(ImVec2(start.x + pad, start.y + pad));
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - 2.0f * pad);
  }
  Strip(const Strip&) = delete;
  Strip& operator=(const Strip&) = delete;
  ~Strip()
  {
    const Theme::Palette& palette = Theme::palette();
    const float scale = Theme::scale();
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    const float bottom = ImGui::GetItemRectMax().y + pad;
    draw_list->ChannelsSetCurrent(0);
    draw_list->AddRectFilled(start, ImVec2(start.x + width, bottom),
                             Theme::toU32(palette.raised), 4.0f * scale);
    draw_list->AddRectFilled(start, ImVec2(start.x + 3.0f * scale, bottom),
                             Theme::toU32(palette.accent), 2.0f * scale);
    draw_list->ChannelsMerge();
    ImGui::SetCursorScreenPos(ImVec2(start.x, bottom));
    ImGui::Dummy(ImVec2(width, Theme::Space::XS * scale));
  }
  [[nodiscard]] float innerWidth() const { return width - 2.0f * pad; }

 private:
  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  ImVec2 start = ImGui::GetCursorScreenPos();
  float width = ImGui::GetContentRegionAvail().x;
  float pad = Theme::Space::M * Theme::scale();
};
}  // namespace

// ---------------------------------------------------------------------------
// New-game manager card
// ---------------------------------------------------------------------------

void ManagerSetupPanel::initialize(const GameController& controller)
{
  initialized = true;
  // A name from the default nationality; the player can type another.
  WorldRng rng = WorldRng::stream(controller.getWorldSeed(),
                                  RngDomain::Managers, 0x4E45'5747ULL);
  const auto language = static_cast<Language>(nationality);
  const NamePool& names = NamePool::instance();
  const auto& firsts = names.firstNames(language);
  if (const auto& lasts = names.lastNames(language);
      !firsts.empty() && !lasts.empty())
  {
    default_first = firsts[static_cast<std::size_t>(
        rng.uniformInt(0, static_cast<int>(firsts.size()) - 1))];
    default_last = lasts[static_cast<std::size_t>(
        rng.uniformInt(0, static_cast<int>(lasts.size()) - 1))];
  }
  const auto first = std::format_to_n(
      first_name.data(), static_cast<std::ptrdiff_t>(first_name.size() - 1),
      "{}", default_first);
  first_name[std::min(static_cast<std::size_t>(first.size),
                      first_name.size() - 1)] = '\0';
  const auto last = std::format_to_n(
      last_name.data(), static_cast<std::ptrdiff_t>(last_name.size() - 1), "{}",
      default_last);
  last_name[std::min(static_cast<std::size_t>(last.size),
                     last_name.size() - 1)] = '\0';
}

ManagerSetup ManagerSetupPanel::setup() const
{
  ManagerSetup result;
  result.first_name = first_name[0] != '\0' ? first_name.data() : default_first;
  result.last_name = last_name[0] != '\0' ? last_name.data() : default_last;
  result.nationality = static_cast<Language>(nationality);
  result.age = static_cast<std::uint8_t>(age);
  result.background = static_cast<ManagerBackground>(background);
  result.style = static_cast<ManagerStyle>(style);
  return result;
}

ManagerSetupPanel::Action ManagerSetupPanel::render(
    const GameController& controller, float width)
{
  if (!initialized) initialize(controller);
  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  Action action = Action::NONE;
  UI::beginAutoHeightCard("##manager_setup", LOC("MANAGER_SETUP_TITLE"), width);
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  const float available = ImGui::GetContentRegionAvail().x;
  // Five fields on one line when they fit, otherwise they wrap.
  const int per_row = std::clamp(
      static_cast<int>((available + gap) / (FIELD_MIN_WIDTH * scale + gap)), 1,
      5);
  const float field = (available - gap * static_cast<float>(per_row - 1)) /
                      static_cast<float>(per_row);
  int placed = 0;
  const auto next = [&](const char* label)
  {
    if (placed % per_row != 0) ImGui::SameLine();
    ++placed;
    ImGui::BeginGroup();
    // Same baseline for every label: fields placed with SameLine() inherit
    // the frame padding of the previous field's input.
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(palette.muted, "%s", label);
    ImGui::SetNextItemWidth(field);
  };
  const auto end = [] { ImGui::EndGroup(); };

  next(LOC("MANAGER_SETUP_FIRST_NAME"));
  ImGui::InputText("##first_name", first_name.data(), first_name.size());
  end();
  next(LOC("MANAGER_SETUP_LAST_NAME"));
  ImGui::InputText("##last_name", last_name.data(), last_name.size());
  end();

  next(LOC("MANAGER_SETUP_NATIONALITY"));
  if (const std::string nation = nationName(static_cast<Language>(nationality));
      ImGui::BeginCombo("##nationality", nation.c_str()))
  {
    for (int index = 0;
         index <= static_cast<int>(std::to_underlying(Language::US)); ++index)
    {
      const std::string option = nationName(static_cast<Language>(index));
      if (ImGui::Selectable(option.c_str(), index == nationality))
        nationality = index;
    }
    ImGui::EndCombo();
  }
  end();

  next(LOC("MANAGER_SETUP_BACKGROUND"));
  if (ImGui::BeginCombo("##background",
                        LOC(ManagerMarketModel::backgroundKey(
                            static_cast<ManagerBackground>(background)))))
  {
    for (int index = 0;
         index < static_cast<int>(std::to_underlying(ManagerBackground::COUNT));
         ++index)
    {
      if (ImGui::Selectable(LOC(ManagerMarketModel::backgroundKey(
                                static_cast<ManagerBackground>(index))),
                            index == background))
        background = index;
    }
    ImGui::EndCombo();
  }
  end();

  next(LOC("MANAGER_SETUP_STYLE"));
  if (ImGui::BeginCombo("##style", LOC(ManagerMarketModel::styleKey(
                                       static_cast<ManagerStyle>(style)))))
  {
    for (int index = 0;
         index < static_cast<int>(std::to_underlying(ManagerStyle::COUNT));
         ++index)
    {
      if (ImGui::Selectable(LOC(ManagerMarketModel::styleKey(
                                static_cast<ManagerStyle>(index))),
                            index == style))
        style = index;
    }
    ImGui::EndCombo();
  }
  end();

  // What the experience means, then the way out of the club choice.
  const auto chosen = static_cast<ManagerBackground>(background);
  const float start_reputation = ManagerMarketModel::startingReputation(chosen);
  const std::string summary = formatLocalized(
      "MANAGER_SETUP_SUMMARY",
      {std::format("{:.0f}", start_reputation),
       LOC(ManagerMarketModel::reputationTierKey(
           ManagerMarketModel::reputationTier(start_reputation))),
       LOC(ManagerMarketModel::licenceKey(
           ManagerMarketModel::startingLicence(chosen)))});
  const char* unemployed = LOC("MANAGER_SETUP_UNEMPLOYED");
  const float button = UI::buttonWidth(unemployed);
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * scale));
  const float text_width = ImGui::GetContentRegionAvail().x - button - gap;
  ImGui::BeginGroup();
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() +
                         std::max(text_width, available * 0.5f));
  ImGui::TextColored(palette.muted, "%s", summary.c_str());
  ImGui::PopTextWrapPos();
  ImGui::EndGroup();
  UI::sameLineIfFits(button);
  if (UI::secondaryButton(unemployed)) action = Action::START_UNEMPLOYED;
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
    ImGui::SetTooltip("%s", LOC("MANAGER_SETUP_UNEMPLOYED_HINT"));
  UI::endCard();
  return action;
}

// ---------------------------------------------------------------------------
// Manager screen
// ---------------------------------------------------------------------------

ManagerScene::ManagerScene(GUIView* parent, std::optional<Tab> initial)
    : ManagementScene(parent)
{
  tab = initial.value_or(
      parent->getController().isUnemployed() ? Tab::JOB_CENTRE : Tab::PROFILE);
}

void ManagerScene::update(float /*deltaTime*/)
{
  /* The page refreshes its model on entry and after actions; no frame-driven
   * animation or other time-dependent state needs updating. */
}

void ManagerScene::refresh()
{
  GameController& controller = guiView->getController();
  const ManagerProfile* profile = controller.getManagerProfile();
  unemployed = controller.isUnemployed();
  stints.clear();
  seasons.clear();
  awards.clear();
  vacancies.clear();
  offers.clear();
  interviews.clear();
  profile_facts.clear();
  if (!profile) return;
  const GameDateValue today = controller.getCurrentDate();

  // ---- Profile ----
  subtitle = std::format(
      "{}  ·  {}  ·  {}", profile->name(), nationName(profile->nationality),
      formatLocalized("MANAGER_AGE", {std::to_string(profile->age)}));
  reputation = profile->reputation;
  reputation_value = std::format("{:.0f}", profile->reputation);
  reputation_tier_key = ManagerMarketModel::reputationTierKey(
      ManagerMarketModel::reputationTier(profile->reputation));
  licence_value = LOC(ManagerMarketModel::licenceKey(profile->licence));
  licence_note = profile->licence == CoachingLicence::Pro
                     ? std::string(LOC("MANAGER_LICENCE_TOP"))
                     : formatLocalized("MANAGER_LICENCE_PROGRESS",
                                       {std::to_string(profile->licence_days)});
  if (const auto club = controller.getManagedTeam())
  {
    club_value = club->get().getName();
    club_note = formatLocalized("MANAGER_CONTRACT_NOTE",
                                {Format::money(profile->contract.weekly_wage),
                                 Format::date(profile->contract.expires)});
  }
  else
  {
    club_value = LOC("MANAGER_UNEMPLOYED");
    club_note = formatLocalized(
        "MANAGER_UNEMPLOYED_SINCE",
        {Format::date(profile->unemployed_since),
         std::to_string(dayOrdinal(today) -
                        dayOrdinal(profile->unemployed_since))});
  }
  int played = 0;
  int won = 0;
  int drawn = 0;
  int lost = 0;
  int trophies = 0;
  for (const ManagerStint& stint : controller.getManagerStints())
  {
    played += stint.played;
    won += stint.won;
    drawn += stint.drawn;
    lost += stint.lost;
    trophies += stint.trophies;
    StintRow row;
    row.club = stint.club_name;
    row.league = leagueName(controller, stint.league_id);
    row.current = stint.reason == DepartureReason::Current;
    row.period = std::format("{} – {}", Format::date(stint.start),
                             row.current ? std::string(LOC("MANAGER_PRESENT"))
                                         : Format::date(stint.end));
    row.record = std::format("{}-{}-{}", stint.won, stint.drawn, stint.lost);
    row.win_rate = stint.played == 0
                       ? std::string("–")
                       : std::format("{:.0f}%", stint.winRate() * 100.0f);
    row.trophies = std::to_string(stint.trophies);
    row.departure_key = ManagerMarketModel::departureKey(stint.reason);
    stints.push_back(std::move(row));
  }
  std::ranges::reverse(stints);
  record_value = std::format("{}-{}-{}", won, drawn, lost);
  record_note = played == 0 ? std::string(LOC("MANAGER_RECORD_NONE"))
                            : formatLocalized(
                                  "MANAGER_RECORD_NOTE",
                                  {std::format("{:.0f}%", 100.0 * won / played),
                                   std::to_string(trophies)});

  profile_facts = {
      {"MANAGER_FACT_BACKGROUND",
       LOC(ManagerMarketModel::backgroundKey(profile->background))},
      {"MANAGER_FACT_STYLE", LOC(ManagerMarketModel::styleKey(profile->style))},
      {"MANAGER_FACT_SEASONS", std::to_string(profile->seasons_managed)},
      {"MANAGER_FACT_EARNINGS", Format::money(profile->career_earnings)},
  };
  if (!unemployed)
  {
    profile_facts.emplace_back(
        "MANAGER_FACT_WAGE",
        formatLocalized("MANAGER_PER_WEEK",
                        {Format::money(profile->contract.weekly_wage)}));
    profile_facts.emplace_back("MANAGER_FACT_EXPIRES",
                               Format::date(profile->contract.expires));
    profile_facts.emplace_back(
        "MANAGER_FACT_RELEASE",
        Format::money(profile->contract.release_compensation));
  }

  for (const ManagerSeasonLine& line : controller.getManagerSeasons())
  {
    SeasonRow row;
    row.season = seasonLabel(line.start_year);
    row.club = line.club_name;
    row.finish = formatLocalized(
        "MANAGER_FINISH",
        {std::to_string(line.position), std::to_string(line.league_size)});
    row.expected = std::to_string(line.expected_position);
    row.margin = line.expected_position - line.position;
    seasons.push_back(std::move(row));
  }
  std::ranges::reverse(seasons);
  for (const ManagerAward& award : controller.getManagerAwards())
    awards.emplace_back(seasonLabel(award.start_year),
                        ManagerMarketModel::awardKey(award.kind),
                        award.club_name);
  std::ranges::reverse(awards);

  // ---- Job Centre ----
  for (const GameController::VacancyView& view : controller.getVacancies())
  {
    VacancyRow row;
    row.team_id = view.team_id;
    row.club = clubName(controller, view.team_id);
    row.league = leagueName(controller, view.league_id);
    row.reputation = std::to_string(view.reputation);
    row.expectation =
        std::format("{} ({})", LOC(view.objective_key), view.expected_position);
    row.owner_key = ManagerMarketModel::ownerKey(view.owner);
    row.licence_key = ManagerMarketModel::licenceKey(view.required_licence);
    row.licence_missing = profile->licence < view.required_licence;
    row.chance = view.chance;
    row.chance_text = std::format("{:.0f}%  {}", view.chance * 100.0f,
                                  LOC(chanceKey(view.chance)));
    row.opened = Format::date(view.opened);
    row.stage = view.stage;
    if (view.stage == ApplicationStage::Interview)
      interviews.push_back(view.team_id);
    vacancies.push_back(std::move(row));
  }
  for (const JobOffer& offer : controller.getJobOffers())
  {
    OfferRow row;
    row.id = offer.id;
    row.team_id = offer.team_id;
    row.club = clubName(controller, offer.team_id);
    if (const auto team = controller.getTeamById(offer.team_id))
      row.league = leagueName(controller, team->get().getLeagueId());
    row.wage =
        formatLocalized("MANAGER_PER_WEEK", {Format::money(offer.weekly_wage)});
    row.terms = formatLocalized(
        "JOB_OFFER_TERMS",
        {std::to_string(offer.years), Format::money(offer.release_compensation),
         Format::date(offer.expires)});
    if (offer.compensation > 0 && !unemployed)
      row.compensation = formatLocalized("JOB_OFFER_COMPENSATION",
                                         {Format::money(offer.compensation)});
    row.unsolicited = offer.unsolicited;
    row.weekly_wage = offer.weekly_wage;
    row.years = offer.years;
    offers.push_back(std::move(row));
  }
  if (negotiating != 0 && std::ranges::none_of(offers, [this](const OfferRow& o)
                                               { return o.id == negotiating; }))
    negotiating = 0;
  refreshNational();
}

void ManagerScene::renderContent()
{
  if (const ManagerProfile* profile =
          guiView->getController().getManagerProfile();
      !profile)
    return;
  UI::pageHeader(LOC("MANAGER_TITLE"), subtitle.c_str());
  const std::array<const char*, 2> tabs = {LOC("MANAGER_TAB_PROFILE"),
                                           LOC("MANAGER_TAB_JOBS")};
  if (auto selected = static_cast<int>(std::to_underlying(tab));
      UI::segmented("##manager_tabs", selected, tabs))
    tab = static_cast<Tab>(selected);
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * Theme::scale()));
  if (tab == Tab::PROFILE)
    renderProfile();
  else
    renderJobCentre();
  renderInterviewDialog();
  renderConfirmations();
  runPendingAction();
}

void ManagerScene::renderProfile()
{
  const Theme::Palette& palette = Theme::palette();
  UI::TileRow tiles(4);
  const float tile = tiles.width();
  tiles.next();
  UI::statTile("reputation", LOC("MANAGER_TILE_REPUTATION"),
               reputation_value.c_str(), LOC(reputation_tier_key), palette.text,
               tile);
  tiles.next();
  UI::statTile("licence", LOC("MANAGER_TILE_LICENCE"), licence_value.c_str(),
               licence_note.c_str(), palette.text, tile);
  tiles.next();
  UI::statTile("club", LOC("MANAGER_TILE_CLUB"), club_value.c_str(),
               club_note.c_str(), unemployed ? palette.warning : palette.text,
               tile);
  tiles.next();
  UI::statTile("record", LOC("MANAGER_TILE_RECORD"), record_value.c_str(),
               record_note.c_str(), palette.text, tile);

  // One scroll surface: cards grow with their content and the page scrolls.
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
  const float available = ImGui::GetContentRegionAvail().x;
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  const bool two_columns = available >= TWO_COLUMN_MIN_WIDTH * Theme::scale();
  const float left =
      two_columns ? std::floor((available - gap) * 0.55f) : available;
  renderProfileCard(left);
  if (two_columns) ImGui::SameLine();
  renderHonours(two_columns ? available - gap - left : available);
  renderNationalCard(available);
  renderHistory();
  renderSeasons();
}

void ManagerScene::renderProfileCard(float width)
{
  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  UI::beginAutoHeightCard("manager_profile", LOC("MANAGER_PROFILE"), width);
  const float key_width = KEY_WIDTH * scale;
  const std::string meter_value =
      std::format("{}  ·  {}", reputation_value, LOC(reputation_tier_key));
  UI::meter(LOC("MANAGER_TILE_REPUTATION"),
            std::clamp(reputation / 100.0f, 0.0f, 1.0f), key_width,
            palette.info, meter_value.c_str());
  // Scale marks: local, national, continental, world.
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + key_width);
  const std::string scale_text =
      std::format("{}  ·  {}  ·  {}  ·  {}", LOC("MANAGER_REP_LOCAL"),
                  LOC("MANAGER_REP_NATIONAL"), LOC("MANAGER_REP_CONTINENTAL"),
                  LOC("MANAGER_REP_WORLD"));
  UI::textFitted(scale_text, ImGui::GetContentRegionAvail().x, palette.faint);
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * scale));
  for (const auto& [key, value] : profile_facts)
    UI::keyValue(LOC(key), value.c_str(), key_width);
  if (!unemployed)
  {
    ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * scale));
    if (UI::dangerButton(LOC("MANAGER_RESIGN"))) resign_requested = true;
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(palette.faint, "%s", LOC("MANAGER_RESIGN_HINT"));
  }
  UI::endCard();
}

void ManagerScene::renderHonours(float width) const
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("manager_honours", LOC("MANAGER_HONOURS"), width);
  if (awards.empty())
    UI::emptyState(LOC("MANAGER_HONOURS_EMPTY"),
                   LOC("MANAGER_HONOURS_EMPTY_BODY"));
  for (const AwardRow& award : awards)
  {
    ImGui::TextColored(palette.muted, "%s", award.season.c_str());
    ImGui::SameLine(90.0f * Theme::scale());
    ImGui::TextUnformatted(LOC(award.award_key));
    ImGui::SameLine();
    UI::textFitted(award.club, ImGui::GetContentRegionAvail().x, palette.faint);
  }
  UI::endCard();
}

void ManagerScene::renderHistory() const
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("manager_history", LOC("MANAGER_HISTORY"));
  if (stints.empty())
  {
    UI::emptyState(LOC("MANAGER_HISTORY_EMPTY"),
                   LOC("MANAGER_HISTORY_EMPTY_BODY"));
    UI::endCard();
    return;
  }
  const auto columns = localize(historyColumns());
  if (const UI::ColumnMask mask =
          UI::fitColumns(columns, ImGui::GetContentRegionAvail().x);
      UI::beginResponsiveTable(
          "history", columns, mask,
          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
  {
    for (const StintRow& row : stints)
    {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      UI::textFitted(row.club, ImGui::GetContentRegionAvail().x, palette.text);
      if (UI::cell(mask, 1))
        UI::textFitted(row.period, ImGui::GetContentRegionAvail().x,
                       palette.muted);
      if (UI::cell(mask, 2))
        UI::textFitted(row.league, ImGui::GetContentRegionAvail().x,
                       palette.muted);
      if (UI::cell(mask, 3)) ImGui::TextUnformatted(row.record.c_str());
      if (UI::cell(mask, 4)) ImGui::TextUnformatted(row.win_rate.c_str());
      if (UI::cell(mask, 5)) ImGui::TextUnformatted(row.trophies.c_str());
      if (UI::cell(mask, 6))
        ImGui::TextColored(row.current ? palette.positive : palette.muted, "%s",
                           LOC(row.departure_key));
    }
    ImGui::EndTable();
  }
  UI::endCard();
}

void ManagerScene::renderSeasons() const
{
  if (seasons.empty()) return;
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("manager_seasons", LOC("MANAGER_SEASONS"));
  const auto columns = localize(seasonColumns());
  if (const UI::ColumnMask mask =
          UI::fitColumns(columns, ImGui::GetContentRegionAvail().x);
      UI::beginResponsiveTable(
          "seasons", columns, mask,
          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
  {
    for (const SeasonRow& row : seasons)
    {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(row.season.c_str());
      if (UI::cell(mask, 1))
        UI::textFitted(row.club, ImGui::GetContentRegionAvail().x,
                       palette.text);
      if (UI::cell(mask, 2))
      {
        ImVec4 finishColor = palette.text;
        if (row.margin > 0)
          finishColor = palette.positive;
        else if (row.margin < 0)
          finishColor = palette.negative;
        ImGui::TextColored(finishColor, "%s", row.finish.c_str());
      }
      if (UI::cell(mask, 3))
        ImGui::TextColored(palette.muted, "%s", row.expected.c_str());
    }
    ImGui::EndTable();
  }
  UI::endCard();
}

void ManagerScene::renderJobCentre()
{
  const Theme::Palette& palette = Theme::palette();
  UI::TileRow tiles(3);
  const float tile = tiles.width();
  const std::string open_jobs = std::to_string(vacancies.size());
  const std::string offer_count = std::to_string(offers.size());
  const std::string interview_count = std::to_string(interviews.size());
  tiles.next();
  UI::statTile("jobs", LOC("JOB_TILE_VACANCIES"), open_jobs.c_str(),
               LOC("JOB_TILE_VACANCIES_NOTE"), palette.text, tile);
  tiles.next();
  UI::statTile("interviews", LOC("JOB_TILE_INTERVIEWS"),
               interview_count.c_str(), LOC("JOB_TILE_INTERVIEWS_NOTE"),
               palette.text, tile);
  tiles.next();
  UI::statTile("offers", LOC("JOB_TILE_OFFERS"), offer_count.c_str(),
               LOC("JOB_TILE_OFFERS_NOTE"),
               offers.empty() ? palette.text : palette.positive, tile);
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
  renderOffers();
  renderNationalOffers();
  renderInterviews();
  renderVacancies();
  renderNationalVacancies();
}

void ManagerScene::renderOffers()
{
  if (offers.empty()) return;
  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  UI::beginAutoHeightCard("job_offers", LOC("JOB_OFFERS"));
  for (const OfferRow& offer : offers)
  {
    ImGui::PushID(static_cast<int>(offer.id));
    Strip strip;
    {
      Theme::ScopedText title(Theme::Text::TITLE);
      UI::textFitted(offer.club, strip.innerWidth(), palette.text);
    }
    ImGui::TextColored(palette.muted, "%s  ·  %s", offer.league.c_str(),
                       LOC(offer.unsolicited ? "JOB_OFFER_APPROACH"
                                             : "JOB_OFFER_AFTER_INTERVIEW"));
    ImGui::TextUnformatted(offer.wage.c_str());
    ImGui::TextColored(palette.muted, "%s", offer.terms.c_str());
    if (!offer.compensation.empty())
      ImGui::TextColored(palette.muted, "%s", offer.compensation.c_str());
    ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * scale));
    if (negotiating == offer.id)
    {
      ImGui::TextColored(palette.muted, "%s", LOC("JOB_NEGOTIATE_WAGE"));
      const std::array<UI::MoneyChip, 1> chips = {
          {{LOC("JOB_NEGOTIATE_CURRENT"), offer.weekly_wage}}};
      UI::moneyInput("##counter_wage", counter_wage,
                     {.minimum = 0,
                      .chips = chips,
                      .width = std::min(strip.innerWidth(), 320.0f * scale)});
      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(palette.muted, "%s", LOC("JOB_NEGOTIATE_YEARS"));
      ImGui::SameLine();
      UI::segmented("##counter_years", counter_years, YEAR_LABELS);
      if (UI::primaryButton(LOC("JOB_NEGOTIATE_PROPOSE")))
        pending = {PendingAction::Kind::NEGOTIATE, offer.id};
      UI::sameLineIfFits(UI::buttonWidth(LOC("SETTINGS_CANCEL")));
      if (UI::secondaryButton(LOC("SETTINGS_CANCEL"))) negotiating = 0;
    }
    else
    {
      if (UI::primaryButton(LOC("JOB_ACCEPT")))
      {
        // Without a world-class name a club job ends the national one.
        const bool ends_national =
            national_job && !guiView->getController().canCombineClubAndNation();
        if (unemployed && !ends_national)
          pending = {PendingAction::Kind::ACCEPT, offer.id};
        else
        {
          accept_candidate = offer.id;
          const auto club = guiView->getController().getManagedTeam();
          if (unemployed)
            accept_text =
                formatLocalized("JOB_ACCEPT_NATIONAL_BODY", {offer.club});
          else
          {
            std::string clubName;
            if (club) clubName = club->get().getName();
            accept_text = formatLocalized("JOB_ACCEPT_BODY",
                                          {std::move(clubName), offer.club});
          }
          if (ends_national)
            accept_text += "\n\n" + formatLocalized("JOB_ACCEPT_ENDS_NATIONAL",
                                                    {national_value});
          accept_requested = true;
        }
      }
      UI::sameLineIfFits(UI::buttonWidth(LOC("JOB_NEGOTIATE")));
      if (UI::secondaryButton(LOC("JOB_NEGOTIATE")))
      {
        negotiating = offer.id;
        counter_wage = offer.weekly_wage;
        counter_years = std::clamp(offer.years, 1, MAX_YEARS) - 1;
      }
      UI::sameLineIfFits(UI::buttonWidth(LOC("JOB_DECLINE")));
      if (UI::secondaryButton(LOC("JOB_DECLINE")))
        pending = {PendingAction::Kind::DECLINE, offer.id};
    }
    ImGui::PopID();
  }
  UI::endCard();
}

void ManagerScene::renderInterviews()
{
  if (interviews.empty()) return;
  const Theme::Palette& palette = Theme::palette();
  const GameController& controller = guiView->getController();
  UI::beginAutoHeightCard("job_interviews", LOC("JOB_INTERVIEWS"));
  for (const TeamID club : interviews)
  {
    ImGui::PushID(static_cast<int>(club));
    const std::string name = clubName(controller, club);
    const char* attend = LOC("JOB_ATTEND_INTERVIEW");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(name.c_str());
    UI::sameLineIfFits(UI::buttonWidth(attend) +
                       ImGui::GetStyle().ItemSpacing.x);
    if (UI::primaryButton(attend))
    {
      interview_club = club;
      interview_club_name = name;
      interview_step = 0;
      interview_answers.fill(-1);
      interview_result.reset();
      interview_requested = true;
    }
    ImGui::PopID();
  }
  ImGui::TextColored(palette.faint, "%s", LOC("JOB_INTERVIEWS_HINT"));
  UI::endCard();
}

void ManagerScene::renderVacancies()
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("job_vacancies", LOC("JOB_VACANCIES"));
  if (vacancies.empty())
  {
    UI::emptyState(LOC("JOB_VACANCIES_EMPTY"), LOC("JOB_VACANCIES_EMPTY_BODY"));
    UI::endCard();
    return;
  }
  const auto columns = localize(vacancyColumns());
  const UI::ColumnMask mask =
      UI::fitColumns(columns, ImGui::GetContentRegionAvail().x);
  const ImGuiTableFlags flags =
      ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH;
  // The selected row opens a detail strip right under it: the table is split
  // there and continues with the same id (shared column widths).
  UI::TableHeader header = UI::TableHeader::STATIC;
  std::size_t first = 0;
  while (first < vacancies.size())
  {
    if (!UI::beginResponsiveTable("vacancies", columns, mask, flags, header))
      break;
    header = UI::TableHeader::NONE;
    std::size_t index = first;
    const VacancyRow* opened = nullptr;
    while (index < vacancies.size() && opened == nullptr)
    {
      const VacancyRow& row = vacancies[index++];
      ImGui::PushID(static_cast<int>(row.team_id));
      ImGui::TableNextRow(ImGuiTableRowFlags_None,
                          UI::buttonHeight(UI::ButtonSize::COMPACT));
      ImGui::TableNextColumn();
      if (const bool is_selected = selected_vacancy == row.team_id;
          ImGui::Selectable(row.club.c_str(), is_selected,
                            ImGuiSelectableFlags_SpanAllColumns))
        selected_vacancy = is_selected ? 0 : row.team_id;
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("%s", LOC("JOB_ROW_HINT"));
      if (UI::cell(mask, 1))
        UI::textFitted(row.league, ImGui::GetContentRegionAvail().x,
                       palette.muted);
      if (UI::cell(mask, 2)) ImGui::TextUnformatted(row.reputation.c_str());
      if (UI::cell(mask, 3))
        UI::textFitted(row.expectation, ImGui::GetContentRegionAvail().x,
                       palette.text);
      if (UI::cell(mask, 4))
        ImGui::TextColored(
            row.licence_missing ? palette.warning : palette.muted, "%s",
            LOC(row.licence_key));
      if (UI::cell(mask, 5))
        ImGui::TextColored(chanceColor(row.chance), "%s",
                           row.chance_text.c_str());
      if (UI::cell(mask, 6))
        ImGui::TextColored(palette.muted, "%s",
                           row.stage ? LOC(stageKey(*row.stage)) : "");
      ImGui::PopID();
      if (selected_vacancy == row.team_id) opened = &row;
    }
    ImGui::EndTable();
    if (opened != nullptr) renderVacancyDetail(*opened);
    first = index;
  }
  UI::endCard();
}

void ManagerScene::renderVacancyDetail(const VacancyRow& row)
{
  const Theme::Palette& palette = Theme::palette();
  ImGui::PushID(static_cast<int>(row.team_id));
  Strip strip;
  ImGui::TextColored(
      palette.muted, "%s",
      formatLocalized("JOB_DETAIL_BOARD",
                      {LOC(row.owner_key), row.expectation, row.opened})
          .c_str());
  if (row.licence_missing)
    ImGui::TextColored(
        palette.warning, "%s",
        formatLocalized("JOB_DETAIL_LICENCE", {LOC(row.licence_key)}).c_str());
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
  if (!row.stage)
  {
    if (UI::primaryButton(LOC("JOB_APPLY")))
      pending = {PendingAction::Kind::APPLY, row.team_id};
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(palette.faint, "%s", LOC("JOB_APPLY_HINT"));
  }
  else
  {
    ImGui::TextUnformatted(LOC(stageKey(*row.stage)));
  }
  ImGui::PopID();
}

void ManagerScene::renderInterviewDialog()
{
  if (interview_requested)
  {
    interview_requested = false;
    ImGui::OpenPopup(INTERVIEW_POPUP_ID);
  }
  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const float width =
      std::min(DIALOG_WIDTH * scale, viewport->WorkSize.x * 0.92f);
  ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f),
                                      ImVec2(width, viewport->WorkSize.y));
  if (!ImGui::BeginPopupModal(INTERVIEW_POPUP_ID, nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize |
                                  ImGuiWindowFlags_NoTitleBar |
                                  ImGuiWindowFlags_NoSavedSettings))
    return;
  // The vacancy went while answering (a toast says so): nothing to show.
  if (interview_close_requested)
  {
    interview_close_requested = false;
    ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return;
  }
  {
    Theme::ScopedText title(Theme::Text::TITLE);
    UI::textFitted(
        formatLocalized("JOB_INTERVIEW_TITLE", {interview_club_name}),
        ImGui::GetContentRegionAvail().x, palette.text);
  }
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() +
                         ImGui::GetContentRegionAvail().x);
  if (interview_result)
  {
    ImGui::TextColored(
        interview_result->offered ? palette.positive : palette.muted, "%s",
        LOC(interview_result->offered ? "JOB_INTERVIEW_OFFERED"
                                      : "JOB_INTERVIEW_REFUSED"));
    ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * scale));
    for (std::size_t topic = 0; topic < INTERVIEW_TOPICS; ++topic)
    {
      const int fit = interview_result->answer_fit[topic];
      ImVec4 fitColor = palette.negative;
      const char* fitKey = "JOB_FIT_POOR";
      if (fit >= 2)
      {
        fitColor = palette.positive;
        fitKey = "JOB_FIT_GREAT";
      }
      else if (fit >= 1)
      {
        fitColor = palette.text;
        fitKey = "JOB_FIT_GOOD";
      }
      else if (fit == 0)
      {
        fitColor = palette.muted;
        fitKey = "JOB_FIT_NEUTRAL";
      }
      ImGui::TextColored(fitColor, "%s  ·  %s",
                         LOC(ManagerMarketModel::interviewQuestionKey(
                             static_cast<InterviewTopic>(topic))),
                         LOC(fitKey));
    }
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * scale));
    if (UI::primaryButton(LOC("JOB_INTERVIEW_CLOSE"), ImVec2(-FLT_MIN, 0.0f)))
    {
      interview_result.reset();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    return;
  }

  // One question at a time keeps the dialog short on small windows.
  const auto topic = static_cast<InterviewTopic>(interview_step);
  ImGui::TextColored(
      palette.faint, "%s",
      formatLocalized("JOB_INTERVIEW_STEP", {std::to_string(interview_step + 1),
                                             std::to_string(INTERVIEW_TOPICS)})
          .c_str());
  ImGui::TextUnformatted(LOC(ManagerMarketModel::interviewQuestionKey(topic)));
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * scale));
  int& answer = interview_answers[interview_step];
  for (std::size_t option = 0; option < INTERVIEW_OPTIONS; ++option)
  {
    ImGui::PushID(static_cast<int>(option));
    if (ImGui::RadioButton("##option", answer == static_cast<int>(option)))
      answer = static_cast<int>(option);
    ImGui::SameLine();
    ImGui::TextUnformatted(
        LOC(ManagerMarketModel::interviewOptionKey(topic, option)));
    if (ImGui::IsItemClicked()) answer = static_cast<int>(option);
    ImGui::PopID();
  }
  ImGui::PopTextWrapPos();
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * scale));
  // Cancel is offered on every question, Back from the second one.
  const int buttons = interview_step == 0 ? 2 : 3;
  const float buttonWidth =
      (ImGui::GetContentRegionAvail().x -
       ImGui::GetStyle().ItemSpacing.x * static_cast<float>(buttons - 1)) /
      static_cast<float>(buttons);
  if (UI::secondaryButton(LOC("SETTINGS_CANCEL"), ImVec2(buttonWidth, 0.0f)))
    ImGui::CloseCurrentPopup();
  ImGui::SameLine();
  if (interview_step > 0)
  {
    if (UI::secondaryButton(LOC("JOB_INTERVIEW_BACK"),
                            ImVec2(buttonWidth, 0.0f)))
      --interview_step;
    ImGui::SameLine();
  }
  const bool last = interview_step + 1 == INTERVIEW_TOPICS;
  ImGui::BeginDisabled(answer < 0);
  if (UI::primaryButton(
          LOC(last ? "JOB_INTERVIEW_SUBMIT" : "JOB_INTERVIEW_NEXT"),
          ImVec2(buttonWidth, 0.0f)))
  {
    if (last)
      pending = {PendingAction::Kind::INTERVIEW, interview_club};
    else
      ++interview_step;
  }
  ImGui::EndDisabled();
  ImGui::EndPopup();
}

void ManagerScene::renderConfirmations()
{
  if (resign_requested)
  {
    resign_requested = false;
    ImGui::OpenPopup(RESIGN_POPUP_ID);
  }
  if (const std::string resign_body =
          formatLocalized("MANAGER_RESIGN_BODY", {club_value});
      UI::confirmDialog(RESIGN_POPUP_ID, LOC("MANAGER_RESIGN_TITLE"),
                        resign_body.c_str(), LOC("MANAGER_RESIGN"),
                        LOC("SETTINGS_CANCEL")) == UI::DialogResult::CONFIRM)
    pending = {PendingAction::Kind::RESIGN, 0};

  if (accept_requested)
  {
    accept_requested = false;
    ImGui::OpenPopup(ACCEPT_POPUP_ID);
  }
  if (UI::confirmDialog(ACCEPT_POPUP_ID, LOC("JOB_ACCEPT_TITLE"),
                        accept_text.c_str(), LOC("JOB_ACCEPT"),
                        LOC("SETTINGS_CANCEL")) == UI::DialogResult::CONFIRM &&
      accept_candidate != 0)
  {
    pending = {PendingAction::Kind::ACCEPT, accept_candidate};
    accept_candidate = 0;
  }
  renderNationalConfirmation();
}

void ManagerScene::runPendingAction()
{
  if (pending.kind == PendingAction::Kind::NONE) return;
  GameController& controller = guiView->getController();
  const PendingAction action = pending;
  pending = {};
  if (runNationalAction(action))
  {
    refresh();
    return;
  }
  switch (action.kind)
  {
    case PendingAction::Kind::APPLY:
    {
      const ApplyResult result =
          controller.applyForJob(static_cast<TeamID>(action.id));
      showToast(
          LOC(result == ApplyResult::Ok             ? "JOB_APPLIED_TOAST"
              : result == ApplyResult::RecentlyLeft ? "JOB_RECENTLY_LEFT_TOAST"
                                                    : "JOB_APPLY_FAILED_TOAST"),
          result != ApplyResult::Ok);
      break;
    }
    case PendingAction::Kind::ACCEPT:
      if (controller.acceptJobOffer(action.id))
      {
        showToast(LOC("JOB_ACCEPTED_TOAST"));
        tab = Tab::PROFILE;
      }
      else
      {
        showToast(LOC("JOB_OFFER_GONE_TOAST"), true);
      }
      break;
    case PendingAction::Kind::DECLINE:
      controller.declineJobOffer(action.id);
      showToast(LOC("JOB_DECLINED_TOAST"));
      break;
    case PendingAction::Kind::NEGOTIATE:
    {
      const OfferReply reply = controller.negotiateJobOffer(
          action.id, counter_wage,
          static_cast<std::uint8_t>(counter_years + 1));
      negotiating = 0;
      showToast(LOC(reply == OfferReply::Accepted   ? "JOB_TERMS_AGREED_TOAST"
                    : reply == OfferReply::Improved ? "JOB_TERMS_IMPROVED_TOAST"
                                                    : "JOB_WITHDRAWN_TOAST"),
                reply == OfferReply::Withdrawn);
      break;
    }
    case PendingAction::Kind::RESIGN:
      if (controller.resignFromClub())
      {
        showToast(LOC("MANAGER_RESIGNED_TOAST"));
        tab = Tab::JOB_CENTRE;
      }
      break;
    case PendingAction::Kind::INTERVIEW:
    {
      std::array<std::uint8_t, INTERVIEW_TOPICS> answers{};
      for (std::size_t topic = 0; topic < INTERVIEW_TOPICS; ++topic)
        answers[topic] =
            static_cast<std::uint8_t>(std::max(interview_answers[topic], 0));
      interview_result =
          controller.attendInterview(static_cast<TeamID>(action.id), answers);
      if (!interview_result)
      {
        showToast(LOC("JOB_INTERVIEW_GONE_TOAST"), true);
        interview_close_requested = true;
      }
      break;
    }
    case PendingAction::Kind::NATIONAL_APPLY:
    case PendingAction::Kind::NATIONAL_ACCEPT:
    case PendingAction::Kind::NATIONAL_DECLINE:
    case PendingAction::Kind::NATIONAL_RESIGN:
    case PendingAction::Kind::NONE:
      break;
  }
  refresh();
}

// ---------------------------------------------------------------------------
// Home page while out of work
// ---------------------------------------------------------------------------

void ManagerScene::renderUnemployedHome(GUIView* view)
{
  GameController& controller = view->getController();
  const ManagerProfile* profile = controller.getManagerProfile();
  if (!profile) return;
  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  const int idle = dayOrdinal(controller.getCurrentDate()) -
                   dayOrdinal(profile->unemployed_since);
  UI::pageHeader(LOC("UNEMPLOYED_TITLE"),
                 formatLocalized("MANAGER_UNEMPLOYED_SINCE",
                                 {Format::date(profile->unemployed_since),
                                  std::to_string(idle)})
                     .c_str());
  const Game* game = controller.getGame();
  const std::size_t open_jobs =
      game ? game->getCareer().getVacancies().size() : 0;
  const std::size_t applications = controller.getJobApplications().size();
  const std::size_t offer_count = controller.getJobOffers().size();
  UI::TileRow tiles(4);
  const float tile = tiles.width();
  const std::string reputation = std::format("{:.0f}", profile->reputation);
  const std::string jobs = std::to_string(open_jobs);
  const std::string applied = std::to_string(applications);
  const std::string offered = std::to_string(offer_count);
  tiles.next();
  UI::statTile("reputation", LOC("MANAGER_TILE_REPUTATION"), reputation.c_str(),
               LOC(ManagerMarketModel::reputationTierKey(
                   ManagerMarketModel::reputationTier(profile->reputation))),
               palette.text, tile);
  tiles.next();
  UI::statTile("jobs", LOC("JOB_TILE_VACANCIES"), jobs.c_str(),
               LOC("JOB_TILE_VACANCIES_NOTE"), palette.text, tile);
  tiles.next();
  UI::statTile("applications", LOC("JOB_TILE_APPLICATIONS"), applied.c_str(),
               LOC("JOB_TILE_APPLICATIONS_NOTE"), palette.text, tile);
  tiles.next();
  UI::statTile("offers", LOC("JOB_TILE_OFFERS"), offered.c_str(),
               LOC("JOB_TILE_OFFERS_NOTE"),
               offer_count > 0 ? palette.positive : palette.text, tile);
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * scale));
  UI::beginAutoHeightCard("unemployed_home", LOC("UNEMPLOYED_CARD"));
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() +
                         ImGui::GetContentRegionAvail().x);
  ImGui::TextColored(palette.muted, "%s", LOC("UNEMPLOYED_BODY"));
  ImGui::PopTextWrapPos();
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * scale));
  if (UI::primaryButton(LOC("UNEMPLOYED_OPEN_JOBS")))
    view->navigateTo(std::make_unique<ManagerScene>(view, Tab::JOB_CENTRE));
  UI::sameLineIfFits(UI::buttonWidth(LOC("UNEMPLOYED_OPEN_PROFILE")));
  if (UI::secondaryButton(LOC("UNEMPLOYED_OPEN_PROFILE")))
    view->navigateTo(std::make_unique<ManagerScene>(view, Tab::PROFILE));
  UI::endCard();
  if (const NationalJob* job = controller.getNationalJob())
  {
    UI::beginAutoHeightCard("unemployed_national", LOC("NT_CARD_TITLE"));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() +
                           ImGui::GetContentRegionAvail().x);
    ImGui::TextColored(
        palette.muted, "%s",
        formatLocalized("NT_HOME_BODY",
                        {LOC(International::teamNameKey(job->nation).c_str())})
            .c_str());
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * scale));
    if (UI::primaryButton(LOC("NT_OPEN_CALLUPS")))
      Navigation::open(view, NavSection::CALL_UPS);
    UI::endCard();
  }
}

// ---------------------------------------------------------------------------
// National-team jobs
// ---------------------------------------------------------------------------

void ManagerScene::refreshNational()
{
  const GameController& controller = guiView->getController();
  national_facts.clear();
  national_history.clear();
  national_vacancies.clear();
  national_offers.clear();
  const ManagerProfile* profile = controller.getManagerProfile();
  if (!profile) return;
  const auto nation = [](Language value)
  { return std::string(LOC(International::teamNameKey(value).c_str())); };
  const NationalJob* job = controller.getNationalJob();
  national_job = job != nullptr;
  national_blocked = !unemployed && !controller.canCombineClubAndNation();
  if (job)
  {
    national_value = nation(job->nation);
    national_note = formatLocalized(
        "NT_CONTRACT_NOTE",
        {Format::money(job->weekly_wage), Format::date(job->expires)});
    national_facts = {
        {"NT_FACT_SINCE", Format::date(job->start)},
        {"NT_FACT_EXPIRES", Format::date(job->expires)},
        {"NT_FACT_WAGE", formatLocalized("MANAGER_PER_WEEK",
                                         {Format::money(job->weekly_wage)})},
        {"NT_FACT_RECORD",
         std::format("{}-{}-{}", job->won, job->drawn, job->lost)},
        {"NT_FACT_TROPHIES", std::to_string(job->trophies)},
        {"NT_FACT_QUALIFIED", LOC(job->qualified ? "NT_YES" : "NT_NO")},
    };
  }
  else
  {
    national_value.clear();
    national_note.clear();
  }
  for (auto it = controller.getNationalJobHistory().rbegin();
       it != controller.getNationalJobHistory().rend(); ++it)
    national_history.push_back(std::format(
        "{}  ·  {} – {}  ·  {}-{}-{}  ·  {}", nation(it->nation),
        Format::date(it->start), Format::date(it->end), it->won, it->drawn,
        it->lost, LOC(ManagerMarketModel::departureKey(it->reason))));
  for (const GameController::NationalVacancyView& view :
       controller.getNationalVacancies())
  {
    NationalVacancyRow row;
    row.nation = view.nation;
    row.name = nation(view.nation);
    row.rank = formatLocalized("NT_RANK", {std::to_string(view.rank)});
    row.wage =
        formatLocalized("MANAGER_PER_WEEK", {Format::money(view.weekly_wage)});
    row.licence_key = ManagerMarketModel::licenceKey(view.required_licence);
    row.licence_missing = profile->licence < view.required_licence;
    row.chance = view.chance;
    row.chance_text = std::format("{:.0f}%  {}", view.chance * 100.0f,
                                  LOC(chanceKey(view.chance)));
    row.stage = view.stage;
    national_vacancies.push_back(std::move(row));
  }
  for (const NationalJobOffer& offer : controller.getNationalJobOffers())
  {
    NationalOfferRow row;
    row.id = offer.id;
    row.name = nation(offer.nation);
    row.wage =
        formatLocalized("MANAGER_PER_WEEK", {Format::money(offer.weekly_wage)});
    row.terms = formatLocalized("NT_OFFER_TERMS",
                                {Format::date(NationalJobModel::contractEnd(
                                     controller.getCurrentDate())),
                                 Format::date(offer.expires)});
    row.unsolicited = offer.unsolicited;
    national_offers.push_back(std::move(row));
  }
}

void ManagerScene::renderNationalCard(float width)
{
  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  UI::beginAutoHeightCard("manager_national", LOC("NT_CARD_TITLE"), width);
  const float key_width = KEY_WIDTH * scale;
  if (national_job)
  {
    {
      Theme::ScopedText title(Theme::Text::TITLE);
      ImGui::TextUnformatted(national_value.c_str());
    }
    for (const auto& [key, value] : national_facts)
      UI::keyValue(LOC(key), value.c_str(), key_width);
    ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * scale));
    if (UI::secondaryButton(LOC("NT_OPEN_CALLUPS")))
      Navigation::open(guiView, NavSection::CALL_UPS);
    UI::sameLineIfFits(UI::buttonWidth(LOC("NT_RESIGN")));
    if (UI::dangerButton(LOC("NT_RESIGN"))) resign_national_requested = true;
  }
  else
  {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(palette.muted, "%s", LOC("NT_NONE_BODY"));
    ImGui::PopTextWrapPos();
  }
  if (!national_history.empty())
  {
    ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * scale));
    ImGui::TextColored(palette.faint, "%s", LOC("NT_HISTORY"));
    for (const std::string& line : national_history)
      UI::textFitted(line, ImGui::GetContentRegionAvail().x, palette.muted);
  }
  UI::endCard();
}

void ManagerScene::renderNationalOffers()
{
  if (national_offers.empty()) return;
  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  UI::beginAutoHeightCard("national_offers", LOC("NT_OFFERS"));
  for (const NationalOfferRow& offer : national_offers)
  {
    ImGui::PushID(static_cast<int>(offer.id));
    Strip strip;
    {
      Theme::ScopedText title(Theme::Text::TITLE);
      UI::textFitted(offer.name, strip.innerWidth(), palette.text);
    }
    ImGui::TextColored(palette.muted, "%s",
                       LOC(offer.unsolicited ? "NT_OFFER_APPROACH"
                                             : "NT_OFFER_AFTER_APPLICATION"));
    ImGui::TextUnformatted(offer.wage.c_str());
    ImGui::TextColored(palette.muted, "%s", offer.terms.c_str());
    if (national_blocked)
      ImGui::TextColored(palette.warning, "%s", LOC("NT_CLUB_CONFLICT_HINT"));
    ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * scale));
    ImGui::BeginDisabled(national_blocked || national_job);
    if (UI::primaryButton(LOC("JOB_ACCEPT")))
      pending = {PendingAction::Kind::NATIONAL_ACCEPT, offer.id};
    ImGui::EndDisabled();
    UI::sameLineIfFits(UI::buttonWidth(LOC("JOB_DECLINE")));
    if (UI::secondaryButton(LOC("JOB_DECLINE")))
      pending = {PendingAction::Kind::NATIONAL_DECLINE, offer.id};
    ImGui::PopID();
  }
  UI::endCard();
}

void ManagerScene::renderNationalVacancies()
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("national_vacancies", LOC("NT_VACANCIES"));
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(
      palette.faint, "%s",
      LOC(national_blocked ? "NT_CLUB_CONFLICT_HINT" : "NT_VACANCIES_HINT"));
  ImGui::PopTextWrapPos();
  if (national_vacancies.empty())
  {
    UI::emptyState(LOC("NT_VACANCIES_EMPTY"), LOC("NT_VACANCIES_EMPTY_BODY"));
    UI::endCard();
    return;
  }
  static const std::array<UI::Column, 5> COLUMNS = {{
      {"NT_COL_NATION", 0.0f, 0},
      {"NT_COL_RANK", 90.0f, 2},
      {"NT_COL_WAGE", 130.0f, 3},
      {"JOB_COL_LICENCE", 90.0f, 1},
      {"JOB_COL_CHANCE", 110.0f, 0},
  }};
  const auto columns = localize(COLUMNS);
  const UI::ColumnMask mask =
      UI::fitColumns(columns, ImGui::GetContentRegionAvail().x);
  const ImGuiTableFlags flags =
      ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH;
  UI::TableHeader header = UI::TableHeader::STATIC;
  std::size_t first = 0;
  while (first < national_vacancies.size())
  {
    if (!UI::beginResponsiveTable("national", columns, mask, flags, header))
      break;
    header = UI::TableHeader::NONE;
    std::size_t index = first;
    const NationalVacancyRow* opened = nullptr;
    while (index < national_vacancies.size() && opened == nullptr)
    {
      const NationalVacancyRow& row = national_vacancies[index++];
      const auto id = static_cast<int>(std::to_underlying(row.nation));
      ImGui::PushID(id);
      ImGui::TableNextRow(ImGuiTableRowFlags_None,
                          UI::buttonHeight(UI::ButtonSize::COMPACT));
      ImGui::TableNextColumn();
      if (const bool is_selected = selected_nation == id;
          ImGui::Selectable(row.name.c_str(), is_selected,
                            ImGuiSelectableFlags_SpanAllColumns))
        selected_nation = is_selected ? -1 : id;
      if (UI::cell(mask, 1))
        ImGui::TextColored(palette.muted, "%s", row.rank.c_str());
      if (UI::cell(mask, 2)) ImGui::TextUnformatted(row.wage.c_str());
      if (UI::cell(mask, 3))
        ImGui::TextColored(
            row.licence_missing ? palette.warning : palette.muted, "%s",
            LOC(row.licence_key));
      if (UI::cell(mask, 4))
        ImGui::TextColored(chanceColor(row.chance), "%s",
                           row.chance_text.c_str());
      ImGui::PopID();
      if (selected_nation == id) opened = &row;
    }
    ImGui::EndTable();
    if (opened != nullptr)
    {
      ImGui::PushID(static_cast<int>(std::to_underlying(opened->nation)));
      Strip strip;
      if (opened->licence_missing)
        ImGui::TextColored(
            palette.warning, "%s",
            formatLocalized("JOB_DETAIL_LICENCE", {LOC(opened->licence_key)})
                .c_str());
      if (opened->stage)
      {
        ImGui::TextUnformatted(
            LOC(*opened->stage == NationalApplicationStage::Pending
                    ? "JOB_STAGE_PENDING"
                    : (*opened->stage == NationalApplicationStage::Offered
                           ? "JOB_STAGE_OFFERED"
                           : "JOB_STAGE_REJECTED")));
      }
      else
      {
        ImGui::BeginDisabled(national_blocked || national_job);
        if (UI::primaryButton(LOC("JOB_APPLY")))
          pending = {
              PendingAction::Kind::NATIONAL_APPLY,
              static_cast<std::uint32_t>(std::to_underlying(opened->nation))};
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(palette.faint, "%s", LOC("NT_APPLY_HINT"));
      }
      ImGui::PopID();
    }
    first = index;
  }
  UI::endCard();
}

void ManagerScene::renderNationalConfirmation()
{
  constexpr const char* POPUP_ID = "##resign_national";
  if (resign_national_requested)
  {
    resign_national_requested = false;
    ImGui::OpenPopup(POPUP_ID);
  }
  const std::string body = formatLocalized("NT_RESIGN_BODY", {national_value});
  if (UI::confirmDialog(POPUP_ID, LOC("NT_RESIGN"), body.c_str(),
                        LOC("NT_RESIGN"),
                        LOC("SETTINGS_CANCEL")) == UI::DialogResult::CONFIRM)
    pending = {PendingAction::Kind::NATIONAL_RESIGN, 0};
}

bool ManagerScene::runNationalAction(const PendingAction& action)
{
  GameController& controller = guiView->getController();
  switch (action.kind)
  {
    case PendingAction::Kind::NATIONAL_APPLY:
    {
      const NationalApplyResult result =
          controller.applyForNationalJob(static_cast<Language>(action.id));
      showToast(LOC(NationalJobModel::applyResultKey(result)),
                result != NationalApplyResult::Ok);
      return true;
    }
    case PendingAction::Kind::NATIONAL_ACCEPT:
    {
      const NationalApplyResult result =
          controller.acceptNationalJobOffer(action.id);
      showToast(LOC(result == NationalApplyResult::Ok
                        ? "NT_ACCEPTED_TOAST"
                        : NationalJobModel::applyResultKey(result)),
                result != NationalApplyResult::Ok);
      if (result == NationalApplyResult::Ok) tab = Tab::PROFILE;
      return true;
    }
    case PendingAction::Kind::NATIONAL_DECLINE:
      controller.declineNationalJobOffer(action.id);
      showToast(LOC("JOB_DECLINED_TOAST"));
      return true;
    case PendingAction::Kind::NATIONAL_RESIGN:
      if (controller.resignNationalJob()) showToast(LOC("NT_RESIGNED_TOAST"));
      return true;
    default:
      break;
  }
  return false;
}
