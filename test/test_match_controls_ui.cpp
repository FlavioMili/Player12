// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Matchday controls of the live match: planned substitutions made together
// in one window, the reasons a change is refused, in-match tactics and
// formation, touchline shouts, keyboard shortcuts, and the dialogs through
// the real GUIView in normal and pitch-focus layouts at several sizes.
// Screenshots land in the test runtime's captures folder
// (FM_KEEP_TEST_ARTIFACTS=1 keeps them).

#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include "controller/game_controller.h"
#include "global/language_manager.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "gui/gui_view.h"
#include "gui/scenes/match_scene.h"
#include "gui/scenes/match_shouts_bar.h"
#include "gui/view_models/match_changes.h"
#include "model/match_engine.h"
#include "model/match_rules.h"
#include "model/settings_manager.h"
#include "model/team.h"

/** GUI classes grant their internals to this name (see test_game_flow). */
class GameFlowTest_GUIFlowLifecycle_Test
{
 public:
  static bool initialize(GUIView& view) { return view.initialize(); }
  static void frame(GUIView& view)
  {
    view.applyPendingSceneChanges();
    view.handleEvents();
    view.update(0.016f);
    view.render();
    EXPECT_EQ(ImGui::GetCurrentContext()->ErrorCountCurrentFrame, 0)
        << "ImGui reported a usage error";
  }
  static GUIScene* activeScene(const GUIView& view)
  {
    return view.getActiveScene();
  }
};

/** Live match internals (MatchScene grants them to this name). */
class GameFlowTest_ManagedMatchIntegration_Test
{
 public:
  static MatchEngine* engine(MatchScene& scene) { return scene.engine.get(); }
  static std::optional<TouchlineContext> touchline(MatchScene& scene)
  {
    return scene.touchline();
  }
  static MatchSubsPanel& subs(MatchScene& scene) { return scene.subs_panel; }
  static MatchTacticsPanel& tactics(MatchScene& scene)
  {
    return scene.tactics_panel;
  }
  static bool showingSubs(const MatchScene& scene)
  {
    return scene.show_substitutions;
  }
  static bool showingTactics(const MatchScene& scene)
  {
    return scene.show_tactics;
  }
  static bool paused(const MatchScene& scene) { return scene.is_paused; }
  static void setPaused(MatchScene& scene, bool paused)
  {
    scene.is_paused = paused;
  }
  static void setFocus(MatchScene& scene, bool focus)
  {
    scene.setPitchFocus(focus);
  }
  static bool startQuickResult(MatchScene& scene)
  {
    return scene.quickResult();
  }
  static void waitQuickResult(MatchScene& scene) { scene.quick_result.wait(); }
  static void watchHighlightsAt(MatchScene& scene, float speed)
  {
    scene.setPlaybackSpeed(speed);
    scene.setHighlightsOnly(true);
  }
  static bool managedHome(const MatchScene& scene)
  {
    return scene.managed_is_home.value_or(true);
  }
};

using Bridge = GameFlowTest_GUIFlowLifecycle_Test;
using MatchBridge = GameFlowTest_ManagedMatchIntegration_Test;
using MatchChanges::Refusal;

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;
constexpr std::uint32_t MATCH_SEED = 424242;

int uniqueSlot(int offset)
{
  return 470'000 + static_cast<int>(getpid() % 100'000) * 10 + offset;
}

struct SlotCleanup
{
  int slot;
  ~SlotCleanup() { RuntimePaths::removeSave(slot); }
};

void capture(GUIView& view, const char* name)
{
  const auto path = RuntimePaths::capturePath(name);
  std::filesystem::remove(path);
  EXPECT_TRUE(view.captureScreenshot(path.string())) << name;
  EXPECT_TRUE(std::filesystem::exists(path)) << name;
}

void resize(GUIView& view, int width, int height)
{
  SDL_SetWindowSize(view.getWindow(), width, height);
  SDL_Event event{};
  event.type = SDL_EVENT_WINDOW_RESIZED;
  event.window.windowID = SDL_GetWindowID(view.getWindow());
  event.window.data1 = width;
  event.window.data2 = height;
  SDL_PushEvent(&event);
}

void frames(GUIView& view, int count)
{
  for (int index = 0; index < count; ++index) Bridge::frame(view);
}

void setUiScale(GUIView& view, float scale)
{
  SettingsManager::instance()->get().ui_scale = scale;
  view.refreshTheme();
}

SDL_Event keyDown(SDL_Keycode key, SDL_Scancode scancode, SDL_Keymod mod)
{
  SDL_Event event{};
  event.type = SDL_EVENT_KEY_DOWN;
  event.key.key = key;
  event.key.scancode = scancode;
  event.key.mod = mod;
  event.key.down = true;
  return event;
}

/** The dialog window fits inside the work area. */
bool dialogFits(const char* id)
{
  const ImGuiWindow* dialog = ImGui::FindWindowByName(id);
  if (dialog == nullptr || !dialog->Active) return false;
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  return dialog->Rect().Min.x >= viewport->WorkPos.x &&
         dialog->Rect().Min.y >= viewport->WorkPos.y &&
         dialog->Rect().Max.x <= viewport->WorkPos.x + viewport->WorkSize.x &&
         dialog->Rect().Max.y <= viewport->WorkPos.y + viewport->WorkSize.y;
}

/** A managed club's match played by the engine alone (headless tests). */
class HeadlessMatch : public ::testing::Test
{
 protected:
  void SetUp() override
  {
    Logger::init();
    ASSERT_TRUE(LanguageManager::instance().loadLanguage(Language::EN));
    controller.newGame(slot.slot, WORLD_SEED);
    const auto& teams = controller.getTeams();
    ASSERT_GE(teams.size(), 2u);
    home = &teams[0].get();
    away = &teams[1].get();
    engine = std::make_unique<MatchEngine>(
        home->getLineup(), away->getLineup(), home->getStrategy(),
        away->getStrategy(), controller.getStatsConfig(), MATCH_SEED);
    // The manager makes the home side's changes.
    engine->setAutoSubstitutions(false, true);
    ASSERT_GE(bench().size(), 5u);
  }

  std::span<const Player* const> bench() const
  {
    return home->getLineup().getReserves();
  }

  /** Home outfield players on the pitch. */
  std::vector<PlayerID> onPitch() const
  {
    std::vector<PlayerID> result;
    for (const MatchPlayer& player : engine->getPlayers())
      if (player.player && player.onPitch && player.isHomeTeam &&
          !player.isGoalkeeper)
        result.push_back(player.player->getId());
    return result;
  }

  /** Outfield substitutes who have not played. */
  std::vector<PlayerID> freeSubstitutes() const
  {
    std::vector<PlayerID> result;
    for (const Player* reserve : bench())
      if (reserve && reserve->getRole() != PlayerRole::GK &&
          engine->findPlayerStats(reserve->getId()) == nullptr)
        result.push_back(reserve->getId());
    return result;
  }

  /** Plays on until the ball is in play, then until the next stoppage. */
  void playToNextStoppage()
  {
    for (int step = 0; step < 20000 && MatchChanges::atStoppage(*engine);
         ++step)
      engine->advance(0.1f);
    for (int step = 0; step < 20000 && !MatchChanges::atStoppage(*engine);
         ++step)
      engine->advance(0.1f);
    ASSERT_TRUE(MatchChanges::atStoppage(*engine));
  }

  void playUntilBallInPlay()
  {
    for (int step = 0;
         step < 20000 && engine->getState() != MatchState::PLAYING; ++step)
      engine->advance(0.1f);
    ASSERT_EQ(engine->getState(), MatchState::PLAYING);
  }

  const SlotCleanup slot{uniqueSlot(0)};
  GameController controller;
  const Team* home = nullptr;
  const Team* away = nullptr;
  std::unique_ptr<MatchEngine> engine;
};

/** Breaks a highlights playback stopped in (see playHighlights). */
struct BreaksSeen
{
  bool halfTimeBeforeSecondHalf = false;
  bool extraTimeBreak = false;
  bool shootout = false;
};

/** Watches a whole match as highlights at 30x in 60 Hz frames. */
BreaksSeen playHighlights(MatchEngine& engine)
{
  engine.setPlaybackMode(MatchPlaybackMode::HIGHLIGHTS);
  engine.setPlaybackSpeed(30.0f);
  engine.setHighlightPlaybackSpeed(30.0f);
  BreaksSeen seen;
  for (int frame = 0;
       frame < 400'000 && engine.getState() != MatchState::FULL_TIME; ++frame)
  {
    engine.advancePlayback(1.0f / 60.0f);
    const MatchState state = engine.getState();
    if (state == MatchState::HALF_TIME && engine.getPeriod() == 1)
      seen.halfTimeBeforeSecondHalf = true;
    if (state == MatchState::HALF_TIME && engine.getPeriod() >= 2)
      seen.extraTimeBreak = true;
    if (state == MatchState::PENALTY_SHOOTOUT) seen.shootout = true;
  }
  EXPECT_EQ(engine.getState(), MatchState::FULL_TIME);
  return seen;
}

/** A live match of the managed club through the real GUIView. */
struct LiveMatch
{
  explicit LiveMatch(int offset) : slot{uniqueSlot(offset)}
  {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    Logger::init();
    controller.newGame(slot.slot, WORLD_SEED);
    controller.selectManagedTeam(controller.getTeams().front().get().getId());
    controller.advanceToNextManagedFixture(60);
    const auto fixture = controller.getNextManagedFixture();
    if (!fixture || !(fixture->date == controller.getCurrentDate())) return;
    const TeamID club = controller.getManagedTeam()->get().getId();
    home = fixture->home ? club : fixture->opponent;
    away = fixture->home ? fixture->opponent : club;
    // Talks given beforehand, so their dialogs stay closed.
    controller.giveTeamTalk(TeamTalkMoment::PreMatch, TeamTalkTone::Calm);
    controller.giveTeamTalk(TeamTalkMoment::HalfTime, TeamTalkTone::Calm);
    view = std::make_unique<GUIView>(controller);
    if (!Bridge::initialize(*view)) return;
    resize(*view, 1280, 720);
    view->changeScene(std::make_unique<MatchScene>(view.get(), home, away));
    frames(*view, 2);
    scene = dynamic_cast<MatchScene*>(Bridge::activeScene(*view));
  }

  MatchEngine& engine() const { return *MatchBridge::engine(*scene); }
  TouchlineContext context() const { return *MatchBridge::touchline(*scene); }

  const SlotCleanup slot;
  GameController controller;
  TeamID home = 0;
  TeamID away = 0;
  std::unique_ptr<GUIView> view;
  MatchScene* scene = nullptr;
};
}  // namespace

TEST_F(HeadlessMatch, PlannedSubstitutionsShareOneWindow)
{
  playUntilBallInPlay();
  engine->advance(600.0f);
  playUntilBallInPlay();
  const std::vector<PlayerID> players = onPitch();
  const std::vector<PlayerID> substitutes = freeSubstitutes();
  ASSERT_GE(players.size(), 3u);
  ASSERT_GE(substitutes.size(), 3u);

  // Two changes planned and confirmed while the ball is in play wait for
  // the next stoppage.
  MatchChanges::SubstitutionPlan plan;
  EXPECT_EQ(plan.add(*engine, true, bench(), players[0], substitutes[0]),
            Refusal::NONE);
  EXPECT_EQ(plan.add(*engine, true, bench(), players[1], substitutes[1]),
            Refusal::NONE);
  plan.confirm();
  ASSERT_TRUE(plan.isConfirmed());
  if (engine->getState() == MatchState::PLAYING)
  {
    EXPECT_TRUE(plan.applyIfDue(*engine, true, bench()).empty());
    EXPECT_EQ(engine->getSubstitutionsUsed(true), 0);
  }
  playToNextStoppage();
  const auto outcomes = plan.applyIfDue(*engine, true, bench());
  ASSERT_EQ(outcomes.size(), 2u);
  for (const auto& outcome : outcomes)
    EXPECT_EQ(outcome.refusal, Refusal::NONE);
  EXPECT_TRUE(plan.empty());
  EXPECT_FALSE(plan.isConfirmed());
  EXPECT_EQ(engine->getSubstitutionsUsed(true), 2);
  EXPECT_EQ(engine->getSubstitutionWindowsUsed(true), 1)
      << "changes made together use one window";
  EXPECT_EQ(engine->getCommandLog().size(), 2u);

  // A later change at another stoppage uses a second window.
  playToNextStoppage();
  EXPECT_EQ(plan.add(*engine, true, bench(), players[2], substitutes[2]),
            Refusal::NONE);
  EXPECT_EQ(plan.apply(*engine, true, bench()).front().refusal, Refusal::NONE);
  EXPECT_EQ(engine->getSubstitutionsUsed(true), 3);
  EXPECT_EQ(engine->getSubstitutionWindowsUsed(true), 2);
}

TEST_F(HeadlessMatch, RefusedSubstitutionsSayWhy)
{
  playUntilBallInPlay();
  engine->advance(300.0f);
  playToNextStoppage();
  const std::vector<PlayerID> players = onPitch();
  const std::vector<PlayerID> substitutes = freeSubstitutes();
  ASSERT_GE(players.size(), 6u);
  ASSERT_GE(substitutes.size(), 5u);

  MatchChanges::SubstitutionPlan plan;
  ASSERT_EQ(plan.add(*engine, true, bench(), players[0], substitutes[0]),
            Refusal::NONE);
  // Either player of a planned change.
  EXPECT_EQ(plan.check(*engine, true, bench(), players[0], substitutes[1]),
            Refusal::ALREADY_PLANNED);
  EXPECT_EQ(plan.check(*engine, true, bench(), players[1], substitutes[0]),
            Refusal::ALREADY_PLANNED);
  // A starter is not a substitute; an opponent is not on our pitch.
  EXPECT_EQ(plan.check(*engine, true, bench(), players[1], players[2]),
            Refusal::NOT_ON_BENCH);
  const PlayerID opponent = away->getLineup().starters().back()->getId();
  EXPECT_EQ(plan.check(*engine, true, bench(), opponent, substitutes[1]),
            Refusal::NOT_ON_PITCH);
  plan.apply(*engine, true, bench());

  // The substitute who came on cannot come on again, and the player who
  // went off is no longer on the pitch.
  EXPECT_EQ(plan.check(*engine, true, bench(), players[1], substitutes[0]),
            Refusal::ALREADY_PLAYED);
  EXPECT_EQ(plan.check(*engine, true, bench(), players[0], substitutes[1]),
            Refusal::NOT_ON_PITCH);

  // Planned changes count against the limit of five.
  for (std::size_t index = 1; index < 5; ++index)
    EXPECT_EQ(
        plan.add(*engine, true, bench(), players[index], substitutes[index]),
        Refusal::NONE);
  EXPECT_EQ(plan.check(*engine, true, bench(), players[5], substitutes[4]),
            Refusal::LIMIT);
  plan.clear();

  // Every window used: refused in play (outside half-time).
  playToNextStoppage();
  if (engine->getState() == MatchState::HALF_TIME) playToNextStoppage();
  ASSERT_EQ(plan.add(*engine, true, bench(), players[1], substitutes[1]),
            Refusal::NONE);
  plan.apply(*engine, true, bench());
  playToNextStoppage();
  // Half-time is free, so it cannot establish an exhausted-window fixture.
  if (engine->getState() == MatchState::HALF_TIME) playToNextStoppage();
  ASSERT_EQ(plan.add(*engine, true, bench(), players[2], substitutes[2]),
            Refusal::NONE);
  plan.apply(*engine, true, bench());
  ASSERT_EQ(engine->getSubstitutionWindowsUsed(true), 3);
  playUntilBallInPlay();
  if (engine->getState() != MatchState::HALF_TIME)
    EXPECT_EQ(plan.check(*engine, true, bench(), players[3], substitutes[3]),
              Refusal::WINDOWS);

  // Every refusal has a message.
  for (const Refusal refusal :
       {Refusal::MATCH_OVER, Refusal::LIMIT, Refusal::WINDOWS, Refusal::NOT_NOW,
        Refusal::NOT_ON_PITCH, Refusal::NOT_ON_BENCH, Refusal::ALREADY_PLAYED,
        Refusal::ALREADY_PLANNED})
  {
    const char* key = MatchChanges::refusalKey(refusal);
    EXPECT_STRNE(LOC(key), key) << key << " is missing from the language";
  }
  engine->simulateToEnd();
  EXPECT_EQ(plan.check(*engine, true, bench(), players[3], substitutes[3]),
            Refusal::MATCH_OVER);
}

TEST_F(HeadlessMatch, AssistantSuggestsTiredPlayers)
{
  // A starter who comes into the match worn out.
  const PlayerID tired = onPitch().front();
  ASSERT_TRUE(engine->setPlayerCondition(tired, 0.5f));
  playUntilBallInPlay();
  engine->advance(60.0f);
  MatchChanges::SubstitutionPlan plan;
  const auto suggestions = MatchChanges::suggestSubstitutions(
      *engine, true, bench(), plan, controller.getStatsConfig(), 3);
  ASSERT_FALSE(suggestions.empty());
  EXPECT_LE(suggestions.size(), 3u);
  EXPECT_EQ(suggestions.front().out, tired);
  EXPECT_NE(suggestions.front().reasons & MatchChanges::SUGGEST_TIRED, 0);
  for (const auto& suggestion : suggestions)
  {
    EXPECT_NE(suggestion.reasons, 0);
    // Every proposal is a change the rules allow now.
    EXPECT_EQ(plan.check(*engine, true, bench(), suggestion.out, suggestion.in),
              Refusal::NONE);
  }
  // A planned change is not proposed again.
  plan.add(*engine, true, bench(), suggestions.front().out,
           suggestions.front().in);
  for (const auto& suggestion : MatchChanges::suggestSubstitutions(
           *engine, true, bench(), plan, controller.getStatsConfig(), 3))
  {
    EXPECT_NE(suggestion.out, suggestions.front().out);
    EXPECT_NE(suggestion.in, suggestions.front().in);
  }
}

TEST_F(HeadlessMatch, FormationPresetsMapOntoTheSlots)
{
  const std::vector<Vector2F> before = engine->getFormation(true);
  ASSERT_EQ(before.size(), 10u);
  for (std::size_t index = 0; index < Formation::PRESETS.size(); ++index)
  {
    const std::vector<Vector2F> shape =
        MatchChanges::formationFor(*engine, true, Formation::PRESETS[index]);
    ASSERT_EQ(shape.size(), 10u);
    EXPECT_EQ(MatchChanges::detectFormation(shape), static_cast<int>(index));
  }
  // The engine takes the shape; the cost of a real change is what the
  // dialog showed.
  const std::vector<Vector2F> target =
      MatchChanges::formationFor(*engine, true, Formation::PRESETS[3]);
  const float cost = MatchChanges::reshapeCost(before, target);
  EXPECT_GT(cost, 0.0f);
  EXPECT_LE(cost, MatchTuning::Touchline::MAX_RESHAPE_FAMILIARITY_COST);
  ASSERT_TRUE(engine->setFormation(true, target));
  EXPECT_EQ(MatchChanges::detectFormation(engine->getFormation(true)), 3);
  EXPECT_FLOAT_EQ(MatchChanges::reshapeCost(target, target), 0.0f);
}

TEST_F(HeadlessMatch, HighlightsStopAtHalfTime)
{
  // The same match played headless, for the result.
  MatchEngine plain(*engine);
  plain.simulateToEnd();
  const BreaksSeen seen = playHighlights(*engine);
  EXPECT_TRUE(seen.halfTimeBeforeSecondHalf)
      << "the highlights skip jumped over half-time";
  // Stopping at the break does not change the match.
  EXPECT_EQ(engine->getHomeScore(), plain.getHomeScore());
  EXPECT_EQ(engine->getAwayScore(), plain.getAwayScore());
  EXPECT_EQ(engine->getStats().homeShots, plain.getStats().homeShots);
  EXPECT_EQ(engine->getStats().awayPassesAttempted,
            plain.getStats().awayPassesAttempted);
}

TEST_F(HeadlessMatch, HighlightsStopAtTheExtraTimeBreaks)
{
  // A cup tie that goes to extra time (the first seed that does).
  MatchRules::Knockout rules;
  rules.required = true;
  std::optional<std::uint32_t> seed;
  for (std::uint32_t candidate = 1; candidate <= 60 && !seed; ++candidate)
  {
    MatchEngine trial(home->getLineup(), away->getLineup(), home->getStrategy(),
                      away->getStrategy(), controller.getStatsConfig(),
                      candidate);
    trial.setKnockout(rules);
    trial.simulateToEnd();
    if (trial.wentToExtraTime()) seed = candidate;
  }
  ASSERT_TRUE(seed.has_value()) << "no seed reached extra time";
  MatchEngine tie(home->getLineup(), away->getLineup(), home->getStrategy(),
                  away->getStrategy(), controller.getStatsConfig(), *seed);
  tie.setKnockout(rules);
  const BreaksSeen seen = playHighlights(tie);
  EXPECT_TRUE(seen.halfTimeBeforeSecondHalf);
  EXPECT_TRUE(seen.extraTimeBreak)
      << "the highlights skip jumped over the break before extra time";
  EXPECT_EQ(seen.shootout, tie.hasShootout());
}

TEST_F(HeadlessMatch, HighlightsOpenOnLivePlay)
{
  // Watched as highlights at 1x: every highlight begins with the ball in
  // play or within a moment of the restart, never in a long dead-ball wait.
  engine->setPlaybackMode(MatchPlaybackMode::HIGHLIGHTS);
  int highlights = 0;
  int deadStarts = 0;
  double slowest = 0.0;
  for (int frame = 0;
       frame < 2'000'000 && engine->getState() != MatchState::FULL_TIME;
       ++frame)
  {
    const auto begun = std::chrono::steady_clock::now();
    const bool skipped = engine->advancePlayback(1.0f / 60.0f);
    slowest = std::max(slowest, std::chrono::duration<double, std::milli>(
                                    std::chrono::steady_clock::now() - begun)
                                    .count());
    if (!skipped || !engine->getScheduledHighlight()) continue;
    ++highlights;
    // The ball is in play within RESTART_LEAD (+ one step) of the start.
    MatchEngine probe(*engine);
    probe.advance(
        static_cast<float>(MatchTuning::Playback::RESTART_LEAD_SECONDS + 0.1));
    bool live = engine->getState() == MatchState::PLAYING ||
                probe.getState() == MatchState::PLAYING;
    // A break or a card shown at once is fine (nothing to build up to).
    live = live || engine->getState() == MatchState::HALF_TIME;
    if (!live) ++deadStarts;
  }
  RecordProperty("slowest_playback_call_ms", static_cast<int>(slowest));
  std::printf("highlights %d, dead starts %d, slowest call %.1f ms\n",
              highlights, deadStarts, slowest);
  EXPECT_GT(highlights, 10);
  EXPECT_EQ(deadStarts, 0);
  // Predicting and skipping to the next highlight fits in a frame or two
  // (generous bound: the test machine is shared).
  EXPECT_LT(slowest, 100.0) << "a highlight skip stalls the view";
}

TEST(MatchControlsUiTest, TacticsAndShoutsReachTheEngine)
{
  LiveMatch match(1);
  ASSERT_NE(match.scene, nullptr);
  MatchEngine& engine = match.engine();
  const TouchlineContext context = match.context();
  const bool home = MatchBridge::managedHome(*match.scene);
  MatchTacticsPanel& tactics = MatchBridge::tactics(*match.scene);

  // Instructions: the draft is applied as a strategy change.
  tactics.syncDraft(context);
  EXPECT_FALSE(tactics.hasChanges(context));
  const StrategySliders before = engine.getEffectiveSliders(home);
  tactics.draft_sliders = MatchChanges::TACTIC_PRESETS[1].sliders;
  tactics.draft_sliders.pressing = 0.97f;
  ASSERT_TRUE(tactics.hasChanges(context));
  ASSERT_TRUE(tactics.apply(context));
  ASSERT_FALSE(engine.getCommandLog().empty());
  const MatchCommandRecord& strategy = engine.getCommandLog().back();
  EXPECT_EQ(strategy.type, MatchCommandType::STRATEGY);
  EXPECT_EQ(strategy.homeTeam, home);
  EXPECT_FLOAT_EQ(strategy.strategy.getSliders().pressing, 0.97f);
  EXPECT_GT(engine.getEffectiveSliders(home).pressing, before.pressing);
  EXPECT_FALSE(tactics.apply(context)) << "nothing left to apply";

  // Formation: a preset mapped onto the slots, then undone.
  const std::vector<Vector2F> shape = engine.getFormation(home);
  const int current = MatchChanges::detectFormation(shape);
  const std::size_t other = current == 4 ? 0 : 4;
  tactics.draft_shape =
      MatchChanges::formationFor(engine, home, Formation::PRESETS[other]);
  ASSERT_TRUE(tactics.apply(context));
  EXPECT_EQ(engine.getCommandLog().back().type, MatchCommandType::FORMATION);
  EXPECT_EQ(MatchChanges::detectFormation(engine.getFormation(home)),
            static_cast<int>(other));
  ASSERT_TRUE(tactics.undo(context));
  EXPECT_EQ(MatchChanges::detectFormation(engine.getFormation(home)), current);
  EXPECT_FLOAT_EQ(tactics.getApplied().getSliders().pressing, 0.97f);
  ASSERT_TRUE(tactics.undo(context));
  EXPECT_FALSE(tactics.canUndo());
  EXPECT_NE(tactics.getApplied().getSliders().pressing, 0.97f);
  EXPECT_EQ(engine.getCommandLog().back().type, MatchCommandType::STRATEGY);

  // Shouts from the bar and from Shift+number.
  ASSERT_TRUE(MatchShoutsBar::shout(context, 5));
  EXPECT_EQ(engine.getActiveShout(home), MatchChanges::SHOUTS[5].shout);
  EXPECT_GT(engine.getShoutStrength(home), 0.0f);
  EXPECT_EQ(engine.getCommandLog().back().type, MatchCommandType::SHOUT);
  match.scene->handleEvent(
      keyDown(SDLK_EXCLAIM, SDL_SCANCODE_1, SDL_KMOD_LSHIFT));
  EXPECT_EQ(engine.getActiveShout(home), MatchChanges::SHOUTS[0].shout);
  match.scene->handleEvent(
      keyDown(SDLK_UNDERSCORE, SDL_SCANCODE_MINUS, SDL_KMOD_LSHIFT));
  EXPECT_EQ(engine.getActiveShout(home), MatchChanges::SHOUTS[10].shout);
  frames(*match.view, 2);
  capture(*match.view, "match_controls_shouts.bmp");

  // While a quick result plays the match on a worker, the touchline is
  // closed: no shout, dialog or change reaches the engine.
  const std::size_t commands = engine.getCommandLog().size();
  ASSERT_TRUE(MatchBridge::startQuickResult(*match.scene));
  EXPECT_FALSE(MatchBridge::touchline(*match.scene).has_value());
  match.scene->handleEvent(
      keyDown(SDLK_EXCLAIM, SDL_SCANCODE_1, SDL_KMOD_LSHIFT));
  match.scene->handleEvent(keyDown(SDLK_S, SDL_SCANCODE_S, SDL_KMOD_NONE));
  match.scene->handleEvent(keyDown(SDLK_T, SDL_SCANCODE_T, SDL_KMOD_NONE));
  EXPECT_FALSE(MatchBridge::showingSubs(*match.scene));
  EXPECT_FALSE(MatchBridge::showingTactics(*match.scene));
  MatchBridge::waitQuickResult(*match.scene);
  EXPECT_EQ(engine.getCommandLog().size(), commands);
}

TEST(MatchControlsUiTest, DialogsWorkInEveryLayout)
{
  LiveMatch match(2);
  ASSERT_NE(match.scene, nullptr);
  MatchScene& scene = *match.scene;
  GUIView& view = *match.view;
  Settings& settings = SettingsManager::instance()->get();
  settings.pause_for_match_changes = true;
  MatchBridge::setPaused(scene, false);

  // S opens the substitutions (pausing play), Escape closes (resuming).
  scene.handleEvent(keyDown(SDLK_S, SDL_SCANCODE_S, SDL_KMOD_NONE));
  frames(view, 3);
  EXPECT_TRUE(MatchBridge::showingSubs(scene));
  EXPECT_TRUE(MatchBridge::paused(scene));
  EXPECT_TRUE(dialogFits(MatchSubsPanel::WINDOW_ID));

  // Plan two changes (click-click and the drag's API) and check the page.
  const TouchlineContext context = match.context();
  MatchSubsPanel& subs = MatchBridge::subs(scene);
  std::vector<PlayerID> players;
  for (const MatchPlayer& player : match.engine().getPlayers())
    if (player.player && player.onPitch && player.isHomeTeam == context.home &&
        !player.isGoalkeeper)
      players.push_back(player.player->getId());
  std::vector<PlayerID> substitutes;
  const Team& club = match.controller.getTeamById(context.team_id)->get();
  for (const Player* reserve : club.getLineup().getReserves())
    if (reserve->getRole() != PlayerRole::GK)
      substitutes.push_back(reserve->getId());
  ASSERT_GE(players.size(), 2u);
  ASSERT_GE(substitutes.size(), 2u);
  EXPECT_TRUE(subs.plan(context, players[0], substitutes[0]));
  EXPECT_TRUE(subs.plan(context, players[1], substitutes[1]));
  EXPECT_FALSE(subs.plan(context, players[1], substitutes[1]));
  EXPECT_TRUE(context.status_refused);
  EXPECT_EQ(subs.getPlan().entries().size(), 2u);
  subs.selected_out = players[2];
  frames(view, 2);
  capture(view, "match_controls_subs_1280.bmp");

  // Bigger UI, then HiDPI 2560x1440 at scale 2: the dialog still fits.
  setUiScale(view, 2.0f);
  frames(view, 3);
  EXPECT_TRUE(dialogFits(MatchSubsPanel::WINDOW_ID)) << "1280x720 @2";
  capture(view, "match_controls_subs_1280_scale2.bmp");
  resize(view, 2560, 1440);
  frames(view, 3);
  EXPECT_TRUE(dialogFits(MatchSubsPanel::WINDOW_ID)) << "2560x1440 @2";
  capture(view, "match_controls_subs_2560_scale2.bmp");
  setUiScale(view, 0.0f);
  resize(view, 1280, 720);
  frames(view, 3);

  // Pitch focus on and off with the dialog open.
  MatchBridge::setFocus(scene, true);
  frames(view, 3);
  EXPECT_TRUE(MatchBridge::showingSubs(scene));
  EXPECT_TRUE(dialogFits(MatchSubsPanel::WINDOW_ID)) << "pitch focus";
  capture(view, "match_controls_subs_focus.bmp");
  MatchBridge::setFocus(scene, false);
  frames(view, 2);
  EXPECT_TRUE(MatchBridge::showingSubs(scene));

  // T swaps to the tactics, still paused, in both layouts.
  scene.handleEvent(keyDown(SDLK_T, SDL_SCANCODE_T, SDL_KMOD_NONE));
  frames(view, 3);
  EXPECT_FALSE(MatchBridge::showingSubs(scene));
  EXPECT_TRUE(MatchBridge::showingTactics(scene));
  EXPECT_TRUE(MatchBridge::paused(scene));
  EXPECT_TRUE(dialogFits(MatchTacticsPanel::WINDOW_ID));
  capture(view, "match_controls_tactics_1280.bmp");
  MatchBridge::setFocus(scene, true);
  frames(view, 3);
  EXPECT_TRUE(dialogFits(MatchTacticsPanel::WINDOW_ID)) << "pitch focus";
  resize(view, 900, 700);
  frames(view, 3);
  EXPECT_TRUE(dialogFits(MatchTacticsPanel::WINDOW_ID)) << "900x700";
  capture(view, "match_controls_tactics_focus_900.bmp");
  resize(view, 1280, 720);
  frames(view, 2);

  // Escape closes the dialog, play resumes; the planned changes remain.
  ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
  frames(view, 1);
  ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
  frames(view, 2);
  EXPECT_FALSE(MatchBridge::showingTactics(scene));
  EXPECT_FALSE(MatchBridge::paused(scene));
  EXPECT_EQ(subs.getPlan().entries().size(), 2u);
  // The focus HUD with the shouts chip, no dialog.
  capture(view, "match_controls_focus_hud.bmp");
  MatchBridge::setFocus(scene, false);
  frames(view, 2);
  capture(view, "match_controls_hud_1280.bmp");

  // Confirming makes both changes at the next stoppage, in one window.
  MatchEngine& engine = match.engine();
  const int usedBefore = engine.getSubstitutionsUsed(context.home);
  const int windowsBefore = engine.getSubstitutionWindowsUsed(context.home);
  subs.confirm(context);
  for (int step = 0; step < 40000 && !subs.getPlan().empty(); ++step)
  {
    engine.advance(0.1f);
    subs.update(context);
  }
  EXPECT_TRUE(subs.getPlan().empty());
  EXPECT_EQ(engine.getSubstitutionsUsed(context.home), usedBefore + 2);
  EXPECT_EQ(engine.getSubstitutionWindowsUsed(context.home), windowsBefore + 1);

  // With the setting off the match keeps running under the dialog.
  settings.pause_for_match_changes = false;
  scene.handleEvent(keyDown(SDLK_S, SDL_SCANCODE_S, SDL_KMOD_NONE));
  frames(view, 2);
  EXPECT_TRUE(MatchBridge::showingSubs(scene));
  EXPECT_FALSE(MatchBridge::paused(scene));
  scene.handleEvent(keyDown(SDLK_S, SDL_SCANCODE_S, SDL_KMOD_NONE));
  frames(view, 2);
  EXPECT_FALSE(MatchBridge::showingSubs(scene));
  settings.pause_for_match_changes = true;
}

TEST(MatchControlsUiTest, ManagedMatchPausesAtHalfTimeAtAnySpeed)
{
  LiveMatch match(3);
  ASSERT_NE(match.scene, nullptr);
  SettingsManager::instance()->get().pause_at_breaks = true;
  MatchScene& scene = *match.scene;
  MatchEngine& engine = match.engine();
  MatchBridge::watchHighlightsAt(scene, 30.0f);
  MatchBridge::setPaused(scene, false);
  for (int frame = 0; frame < 200'000 && !MatchBridge::paused(scene) &&
                      engine.getPeriod() == 1;
       ++frame)
    scene.update(1.0f / 60.0f);
  ASSERT_TRUE(MatchBridge::paused(scene))
      << "the match ran on through half-time";
  EXPECT_EQ(engine.getState(), MatchState::HALF_TIME);
  EXPECT_EQ(engine.getPeriod(), 1);
  // It stays paused until the manager resumes (Space or Resume).
  frames(*match.view, 3);
  EXPECT_TRUE(MatchBridge::paused(scene));
  EXPECT_EQ(engine.getState(), MatchState::HALF_TIME);
  capture(*match.view, "match_controls_half_time_pause.bmp");
  scene.handleEvent(keyDown(SDLK_SPACE, SDL_SCANCODE_SPACE, SDL_KMOD_NONE));
  EXPECT_FALSE(MatchBridge::paused(scene));
  for (int frame = 0; frame < 20'000 && engine.getPeriod() == 1; ++frame)
    scene.update(1.0f / 60.0f);
  EXPECT_EQ(engine.getPeriod(), 2);
  EXPECT_FALSE(MatchBridge::paused(scene));
}

TEST(MatchControlsUiTest, PauseSettingIsSaved)
{
  Settings& settings = SettingsManager::instance()->get();
  settings.pause_for_match_changes = false;
  SettingsManager::instance()->save();
  settings.pause_for_match_changes = true;
  SettingsManager::instance()->load();
  EXPECT_FALSE(SettingsManager::instance()->get().pause_for_match_changes);
  SettingsManager::instance()->get().pause_for_match_changes = true;
  SettingsManager::instance()->save();
}
