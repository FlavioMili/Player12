// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_set>
#include <utility>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/language_manager.h"
#include "global/logger.h"
#include "global/paths.h"
#include "gui/gui_view.h"
#include "gui/render_scale.h"
#include "gui/view_models/competition_view.h"
#include "gui/view_models/formation.h"
#include "gui/view_models/player_view.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/game.h"
#include "model/lineup.h"
#include "model/settings_manager.h"

namespace
{
class ViewModelTest : public ::testing::Test
{
 protected:
  void SetUp() override
  {
    Logger::init();
    controller = std::make_unique<GameController>();
    controller->newGame(97);
    ASSERT_FALSE(controller->getTeams().empty());
    team_id = controller->getTeams().front().get().getId();
    controller->selectManagedTeam(team_id);
  }

  std::unique_ptr<GameController> controller;
  TeamID team_id = 0;
};

const Match* firstLeagueFixture(const GameController& controller,
                                LeagueID leagueId)
{
  const auto league = controller.getLeagueById(leagueId);
  const auto& teams = league->get().getTeamIDs();
  for (const auto& [date, matches] :
       controller.getGame()->getCalendar().getFullCalendar())
  {
    for (const Match& match : matches)
    {
      if (match.getMatchType() == MatchType::LEAGUE &&
          std::ranges::find(teams, match.getHomeTeamId()) != teams.end())
        return &match;
    }
  }
  return nullptr;
}
}  // namespace

TEST(FormatTest, MoneyIsCompactAndSigned)
{
  EXPECT_EQ(Format::money(0), "€0");
  EXPECT_EQ(Format::money(950), "€950");
  EXPECT_EQ(Format::money(13'400), "€13K");
  EXPECT_EQ(Format::money(2'450'000), "€2.45M");
  EXPECT_EQ(Format::money(-31'000'000), "-€31.0M");
  EXPECT_EQ(Format::moneyFull(1'250'000), "€1,250,000");
  EXPECT_EQ(Format::moneyFull(-999), "-€999");
  EXPECT_EQ(Format::signedInt(4), "+4");
  EXPECT_EQ(Format::signedInt(-2), "-2");
  EXPECT_EQ(Format::thousands(38'500), "38,500");
  EXPECT_EQ(Format::decimal(7.5, 1), "7.5");
}

TEST(FormatTest, ItalianUsesDecimalCommaAndItalianUnits)
{
  ASSERT_TRUE(LanguageManager::instance().loadLanguage(Language::IT));
  EXPECT_EQ(Format::money(13'400), "€\u00a013\u00a0mila");
  EXPECT_EQ(Format::money(2'450'000), "€\u00a02,45\u00a0mln");
  EXPECT_EQ(Format::money(-31'000'000), "-€\u00a031,0\u00a0mln");
  EXPECT_EQ(Format::moneyFull(1'250'000), "€\u00a01.250.000");
  EXPECT_EQ(Format::thousands(38'500), "38.500");
  EXPECT_EQ(Format::decimal(7.5, 1), "7,5");
  ASSERT_TRUE(LanguageManager::instance().loadLanguage(Language::EN));
  EXPECT_EQ(Format::money(2'450'000), "€2.45M");
}

TEST(FormationTest, RoleFitPrefersNaturalAndAdjacentRoles)
{
  EXPECT_FLOAT_EQ(Formation::roleFit(PlayerRole::CB, PlayerRole::CB), 1.0f);
  EXPECT_FLOAT_EQ(Formation::roleFit(PlayerRole::GK, PlayerRole::CB), 0.0f);
  EXPECT_FLOAT_EQ(Formation::roleFit(PlayerRole::ST, PlayerRole::GK), 0.0f);
  EXPECT_GT(Formation::roleFit(PlayerRole::LW, PlayerRole::LM),
            Formation::roleFit(PlayerRole::RW, PlayerRole::LM));
  EXPECT_GT(Formation::roleFit(PlayerRole::CM, PlayerRole::CDM),
            Formation::roleFit(PlayerRole::ST, PlayerRole::CDM));
}

TEST(FormationTest, PresetsHaveTenSlotsInsideThePitch)
{
  for (const Formation::Preset& preset : Formation::PRESETS)
  {
    for (const Formation::Slot& slot : preset.slots)
    {
      EXPECT_GT(slot.position.x, 0.0f) << preset.name;
      EXPECT_LT(slot.position.x, 1.0f) << preset.name;
      EXPECT_GT(slot.position.y, 0.0f) << preset.name;
      EXPECT_LT(slot.position.y, 1.0f) << preset.name;
      EXPECT_NE(slot.role, PlayerRole::GK) << preset.name;
    }
  }
}

TEST_F(ViewModelTest, AutoPickFillsElevenAndNamesABench)
{
  Team& team = controller->getManagedTeam()->get();
  std::vector<const Player*> squad;
  for (const auto& player : controller->getPlayersForTeam(team_id))
    squad.push_back(&player.get());
  ASSERT_GE(squad.size(), 11u);

  for (size_t index = 0; index < Formation::PRESETS.size(); ++index)
  {
    Lineup& lineup = team.getLineup();
    Formation::autoPick(lineup, Formation::PRESETS[index], squad,
                        controller->getStatsConfig());
    ASSERT_NE(lineup.getGoalkeeper(), nullptr);
    EXPECT_EQ(lineup.getGoalkeeper()->getRole(), PlayerRole::GK);
    EXPECT_EQ(lineup.getOutfieldPlayers().size(), 10u);
    EXPECT_EQ(lineup.getReserves().size(),
              std::min(squad.size() - 11u, Lineup::MAX_SUBSTITUTES));
    EXPECT_EQ(Formation::detectPreset(lineup), static_cast<int>(index));

    std::unordered_set<PlayerID> seen;
    seen.insert(lineup.getGoalkeeper()->getId());
    for (const auto& positioned : lineup.getOutfieldPlayers())
      EXPECT_TRUE(seen.insert(positioned.player->getId()).second);
    for (const Player* reserve : lineup.getReserves())
      EXPECT_TRUE(seen.insert(reserve->getId()).second);
    EXPECT_EQ(seen.size(), 11u + lineup.getReserves().size());
    // A reserve keeper when the squad has one.
    const auto keepers =
        std::ranges::count_if(squad, [](const Player* player)
                              { return player->getRole() == PlayerRole::GK; });
    if (keepers >= 2)
      EXPECT_TRUE(
          std::ranges::any_of(lineup.getReserves(), [](const Player* player)
                              { return player->getRole() == PlayerRole::GK; }));
  }
}

TEST_F(ViewModelTest, ApplyPresetKeepsTheSameEleven)
{
  Lineup& lineup = controller->getManagedTeam()->get().getLineup();
  std::unordered_set<PlayerID> before;
  for (const auto& positioned : lineup.getOutfieldPlayers())
    before.insert(positioned.player->getId());
  const Player* goalkeeper = lineup.getGoalkeeper();
  const size_t reserves = lineup.getReserves().size();

  Formation::applyPreset(lineup, Formation::PRESETS[3],
                         controller->getStatsConfig());
  EXPECT_EQ(lineup.getGoalkeeper(), goalkeeper);
  EXPECT_EQ(lineup.getReserves().size(), reserves);
  std::unordered_set<PlayerID> after;
  for (const auto& positioned : lineup.getOutfieldPlayers())
    after.insert(positioned.player->getId());
  EXPECT_EQ(before, after);
  EXPECT_EQ(Formation::detectPreset(lineup), 3);
}

TEST_F(ViewModelTest, StandingsReflectPlayedResults)
{
  const LeagueID leagueId = controller->getManagedTeam()->get().getLeagueId();
  const auto initial = CompetitionView::buildStandings(*controller, leagueId);
  ASSERT_EQ(initial.size(),
            controller->getLeagueById(leagueId)->get().getTeamIDs().size());
  EXPECT_TRUE(std::ranges::all_of(
      initial, [](const auto& row) { return row.played == 0; }));

  const Match* fixture = firstLeagueFixture(*controller, leagueId);
  ASSERT_NE(fixture, nullptr);
  const TeamID home = fixture->getHomeTeamId();
  const TeamID away = fixture->getAwayTeamId();
  ASSERT_TRUE(controller->setMatchResult(fixture->getDate(), home, away, 3, 1));

  const auto table = CompetitionView::buildStandings(*controller, leagueId);
  ASSERT_EQ(table.front().team_id, home);
  EXPECT_EQ(table.front().points, 3);
  EXPECT_EQ(table.front().goalDifference(), 2);
  EXPECT_EQ(table.front().form_count, 1);
  EXPECT_EQ(table.front().form[0], UI::Outcome::WIN);
  EXPECT_EQ(table.back().team_id, away);
  EXPECT_EQ(table.back().lost, 1);
  EXPECT_EQ(table.back().goals_against, 3);
  EXPECT_TRUE(std::ranges::is_sorted(table,
                                     [](const auto& left, const auto& right)
                                     { return left.points > right.points; }));

  const auto fixtures =
      CompetitionView::buildLeagueFixtures(*controller, leagueId);
  ASSERT_FALSE(fixtures.empty());
  EXPECT_EQ(fixtures.front().round, 1);
  EXPECT_TRUE(std::ranges::is_sorted(fixtures, {},
                                     &CompetitionView::FixtureRow::round));
  const auto clubFixtures =
      CompetitionView::buildClubFixtures(*controller, home);
  ASSERT_FALSE(clubFixtures.empty());
  EXPECT_TRUE(std::ranges::all_of(
      clubFixtures, [home](const auto& row)
      { return row.home_id == home || row.away_id == home; }));
  const auto played =
      std::ranges::find_if(clubFixtures, [&](const auto& row)
                           { return row.played && row.away_id == away; });
  ASSERT_NE(played, clubFixtures.end());
  EXPECT_EQ(CompetitionView::outcomeFor(*played, home), UI::Outcome::WIN);
  EXPECT_EQ(CompetitionView::outcomeFor(*played, away), UI::Outcome::LOSS);
}

TEST_F(ViewModelTest, RoleFitsAreSortedAndCoverEveryGroup)
{
  const Player& player = controller->getPlayersForTeam(team_id).front().get();
  const auto fits = PlayerView::roleFits(player, controller->getStatsConfig());
  ASSERT_EQ(fits.size(), 4u);
  EXPECT_TRUE(std::ranges::is_sorted(fits,
                                     [](const auto& left, const auto& right)
                                     { return left.rating > right.rating; }));
  const PlayerView::PlayerRow row = PlayerView::makeRow(*controller, player);
  EXPECT_EQ(row.id, player.getId());
  EXPECT_EQ(row.name_lower, PlayerView::toLower(player.getName()));
  EXPECT_FALSE(row.value_text.empty());
}

TEST(SettingsPersistenceTest, AppearanceOptionsRoundTrip)
{
  SettingsManager* manager = SettingsManager::instance();
  const Settings original = manager->get();
  Settings& settings = manager->get();
  settings.theme_preset = 3;
  settings.club_accent = false;
  settings.accent_rgb = 0x3366CC;
  settings.ui_scale = 1.25f;
  settings.compact_density = true;
  settings.reduced_motion = true;
  manager->save();

  settings = Settings{};
  manager->load();
  EXPECT_EQ(manager->get().theme_preset, 3);
  EXPECT_FALSE(manager->get().club_accent);
  EXPECT_EQ(manager->get().accent_rgb, 0x3366CCU);
  EXPECT_FLOAT_EQ(manager->get().ui_scale, 1.25f);
  EXPECT_TRUE(manager->get().compact_density);
  EXPECT_TRUE(manager->get().reduced_motion);

  settings = original;
  manager->save();
}

TEST(SettingsPersistenceTest, AutosavePolicyRoundTripsAndReachesTheController)
{
  SettingsManager* manager = SettingsManager::instance();
  const Settings original = manager->get();
  Settings& settings = manager->get();
  settings.autosave_frequency =
      static_cast<int>(std::to_underlying(AutosaveFrequency::Matchday));
  settings.autosave_backups = 7;
  manager->save();

  settings = Settings{};
  manager->load();
  EXPECT_EQ(manager->get().autosave_frequency,
            static_cast<int>(AutosaveFrequency::Matchday));
  EXPECT_EQ(manager->get().autosave_backups, 7);

  GameController controller;
  GUIView view(controller);
  view.applySavePolicy();  // also runs once when the window initializes
  const AutosavePolicy policy = controller.getAutosavePolicy();
  EXPECT_EQ(policy.frequency, AutosaveFrequency::Matchday);
  EXPECT_EQ(policy.backups, 7);

  settings = original;
  manager->save();
}

TEST(ThemeTest, PackedAccentRoundTripsAndRatingScaleIsMonotonic)
{
  EXPECT_EQ(Theme::packRgb(Theme::unpackRgb(0x21A663)), 0x21A663U);
  EXPECT_LT(Theme::ratingColor(30.0).y, Theme::ratingColor(80.0).y);
  EXPECT_GT(Theme::ratingColor(30.0).x, Theme::ratingColor(80.0).x);
}

TEST(RenderScaleTest, ScopedScaleAppliesFramebufferScaleAndRestores)
{
  SDL_Surface* surface = SDL_CreateSurface(64, 64, SDL_PIXELFORMAT_RGBA8888);
  ASSERT_NE(surface, nullptr);
  SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
  ASSERT_NE(renderer, nullptr);
  float x = 0.0f;
  float y = 0.0f;
  {
    // A Wayland scale-2 output reports DisplayFramebufferScale (2, 2).
    const ScopedRenderScale scale(renderer, ImVec2(2.0f, 2.0f));
    SDL_GetRenderScale(renderer, &x, &y);
    EXPECT_FLOAT_EQ(x, 2.0f);
    EXPECT_FLOAT_EQ(y, 2.0f);
  }
  SDL_GetRenderScale(renderer, &x, &y);
  EXPECT_FLOAT_EQ(x, 1.0f);
  EXPECT_FLOAT_EQ(y, 1.0f);
  SDL_DestroyRenderer(renderer);
  SDL_DestroySurface(surface);
}

TEST(WidgetsTest, ParseMoneyAcceptsSuffixesAndSeparators)
{
  int64_t value = 0;
  ASSERT_TRUE(UI::parseMoney("15m", value));
  EXPECT_EQ(value, 15'000'000);
  ASSERT_TRUE(UI::parseMoney("€14.4M", value));
  EXPECT_EQ(value, 14'400'000);
  ASSERT_TRUE(UI::parseMoney("850k", value));
  EXPECT_EQ(value, 850'000);
  ASSERT_TRUE(UI::parseMoney("1,200,000", value));
  EXPECT_EQ(value, 1'200'000);
  ASSERT_TRUE(UI::parseMoney("1.200.000", value));
  EXPECT_EQ(value, 1'200'000);
  ASSERT_TRUE(UI::parseMoney("14.500", value));
  EXPECT_EQ(value, 14'500);
  value = 7;
  EXPECT_FALSE(UI::parseMoney("", value));
  EXPECT_FALSE(UI::parseMoney("abc", value));
  EXPECT_FALSE(UI::parseMoney("12x", value));
  EXPECT_EQ(value, 7);
  // The decimal comma no longer multiplies the amount by ten.
  ASSERT_TRUE(UI::parseMoney("1,5M", value));
  EXPECT_EQ(value, 1'500'000);
  value = 7;
  EXPECT_FALSE(UI::parseMoney("1.50.000", value));
  EXPECT_EQ(value, 7);
}

TEST(WidgetsTest, MoneyPreviewReadsTheTypedAmountOrSaysWhyNot)
{
  ASSERT_TRUE(LanguageManager::instance().loadLanguage(Language::EN));
  UI::MoneyPreview preview = UI::moneyPreview("1,5M");
  EXPECT_FALSE(preview.error);
  EXPECT_EQ(preview.text, "Amount: €1,500,000");
  preview = UI::moneyPreview("1,5M", {.maximum = 1'000'000});
  EXPECT_FALSE(preview.error);
  EXPECT_EQ(preview.text, "Amount: €1,000,000 (the limit)");
  preview = UI::moneyPreview("1,500k");
  EXPECT_TRUE(preview.error);
  EXPECT_EQ(preview.text, LOC("WIDGET_MONEY_ERROR_AMBIGUOUS"));
  preview = UI::moneyPreview("lots");
  EXPECT_TRUE(preview.error);
  EXPECT_EQ(preview.text, LOC("WIDGET_MONEY_ERROR_INVALID"));

  ASSERT_TRUE(LanguageManager::instance().loadLanguage(Language::IT));
  preview = UI::moneyPreview("€ 2,25 mln");
  EXPECT_FALSE(preview.error);
  EXPECT_EQ(preview.text, "Importo: €\u00a02.250.000");
  preview = UI::moneyPreview("1,500k");
  EXPECT_FALSE(preview.error);
  EXPECT_EQ(preview.text, "Importo: €\u00a01.500");
  preview = UI::moneyPreview("");
  EXPECT_TRUE(preview.error);
  EXPECT_EQ(preview.text, "Inserisci un importo.");
  ASSERT_TRUE(LanguageManager::instance().loadLanguage(Language::EN));
}

TEST(WidgetsTest, MoneyStepsStayProportionalForSmallAmounts)
{
  // A ticket price of 40 once jumped to ~400 on either button.
  EXPECT_EQ(UI::stepMoney(40, true, 0.05), 42);
  EXPECT_EQ(UI::stepMoney(40, false, 0.05), 38);
  // Tiny amounts still move by one per press.
  EXPECT_EQ(UI::stepMoney(1, true, 0.05), 2);
  EXPECT_EQ(UI::stepMoney(2, false, 0.05), 1);
  EXPECT_EQ(UI::stepMoney(0, true, 0.05), 1);
  // Large amounts round to three significant figures.
  EXPECT_EQ(UI::stepMoney(14'400'000, true, 0.05), 15'100'000);
  EXPECT_EQ(UI::stepMoney(14'400'000, false, 0.10), 13'000'000);
  EXPECT_EQ(UI::stepMoney(900'000, true, 0.05), 945'000);
}

TEST(WidgetsTest, EveryLanguageStringHasGlyphsInTheFont)
{
  // A missing glyph renders as a box ("→" once did): every character of
  // every translation must exist in the UI font.
  ImGuiContext* previous = ImGui::GetCurrentContext();
  ImGuiContext* context = ImGui::CreateContext();
  ImFont* font = ImGui::GetIO().Fonts->AddFontFromFileTTF(
      AssetPaths::font().c_str(), 16.0f);
  ASSERT_NE(font, nullptr);
  for (const char* language : {"English", "Italian"})
  {
    std::ifstream file(AssetPaths::language(language));
    ASSERT_TRUE(file.is_open()) << language;
    const nlohmann::json strings = nlohmann::json::parse(file);
    std::string missing;
    for (const auto& [key, value] : strings.items())
    {
      if (!value.is_string()) continue;
      const std::string text = value.get<std::string>();
      const char* cursor = text.c_str();
      const char* end = cursor + text.size();
      while (cursor < end)
      {
        unsigned int character = 0;
        cursor += ImTextCharFromUtf8(&character, cursor, end);
        if (character >= 0x80 &&
            !font->IsGlyphInFont(static_cast<ImWchar>(character)))
          missing += std::format("\n  {}: U+{:04X}", key, character);
      }
    }
    EXPECT_TRUE(missing.empty()) << language << " glyphs missing:" << missing;
  }
  ImGui::DestroyContext(context);
  ImGui::SetCurrentContext(previous);
}

TEST(WidgetsTest, FitColumnsDropsHighestPriorityFirst)
{
  ImGuiContext* previous = ImGui::GetCurrentContext();
  ImGuiContext* context = ImGui::CreateContext();
  ImGui::GetStyle().FontScaleDpi = 1.0f;
  ImGui::GetStyle().CellPadding = ImVec2(0.0f, 0.0f);
  const std::array<UI::Column, 4> columns = {{
      {"Name", 0.0f, 0},
      {"Role", 100.0f, 1},
      {"Age", 50.0f, 3},
      {"Wage", 80.0f, 2},
  }};
  const auto all = UI::fitColumns(columns, 1000.0f, 100.0f);
  EXPECT_EQ(all, 0b1111U);
  // 100 stretch + 100 + 80 fits once Age (priority 3) is gone.
  EXPECT_EQ(UI::fitColumns(columns, 290.0f, 100.0f), 0b1011U);
  EXPECT_EQ(UI::fitColumns(columns, 210.0f, 100.0f), 0b0011U);
  // Priority 0 columns always stay, even when nothing fits.
  EXPECT_EQ(UI::fitColumns(columns, 10.0f, 100.0f), 0b0001U);
  ImGui::DestroyContext(context);
  ImGui::SetCurrentContext(previous);
}

TEST(SettingsPersistenceTest, PlayHalfDurationRoundTripsAndClamps)
{
  SettingsManager* manager = SettingsManager::instance();
  const Settings original = manager->get();
  EXPECT_EQ(Settings{}.play_half_minutes, 5);
  for (int minutes = 3; minutes <= 15; ++minutes)
  {
    manager->get().play_half_minutes = minutes;
    manager->save();
    manager->get() = Settings{};
    manager->load();
    EXPECT_EQ(manager->get().play_half_minutes, minutes);
  }
  for (const int minutes : {0, 99})
  {
    manager->get().play_half_minutes = minutes;
    manager->save();
    manager->load();
    EXPECT_EQ(manager->get().play_half_minutes, std::clamp(minutes, 3, 15));
  }
  manager->get() = original;
  manager->save();
}
