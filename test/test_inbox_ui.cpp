// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The inbox through the real GUIView: a decision moment with its two
// answers, the filters kept across a scene change and a reload, and the
// followed players' feed, at 1280x720, a narrow window and UI scale 2.
// Screenshots land in the test runtime's captures folder
// (FM_KEEP_TEST_ARTIFACTS=1 keeps them).

#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <unistd.h>

#include <algorithm>
#include <cfloat>
#include <filesystem>
#include <memory>
#include <utility>

#include "controller/game_controller.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "gui/gui_view.h"
#include "gui/scenes/inbox_dilemma_card.h"
#include "gui/scenes/inbox_scene.h"
#include "gui/scenes/main_game_scene.h"
#include "gui/scenes/management_scene.h"
#include "model/game.h"
#include "model/inbox.h"
#include "model/settings_manager.h"
#include "model/stories.h"
#include "model/world_rng.h"
#include "model/world_simulation.h"

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
  static int tab(const InboxScene& scene) { return scene.tab; }
  static int category(const InboxScene& scene) { return scene.category_filter; }
  static bool followedOnly(const InboxScene& scene)
  {
    return scene.followed_only;
  }
  static bool hasDilemma(const InboxScene& scene)
  {
    return std::ranges::any_of(
        scene.decisions, [](const auto& decision)
        { return decision.action == InboxAction::Dilemma; });
  }
  static std::size_t threadCount(const InboxScene& scene)
  {
    return scene.threads.size();
  }
  /** What a click on a filter row does. */
  static void pickCategory(InboxScene& scene, int category)
  {
    scene.followed_only = category == -2;
    scene.category_filter = category == -2 ? -1 : category;
    scene.storeView();
  }
  static void pickTab(InboxScene& scene, int tab)
  {
    scene.tab = tab;
    scene.storeView();
  }
};

using Bridge = GameFlowTest_GUIFlowLifecycle_Test;

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

int uniqueSlot(int offset)
{
  return 9'200'000 + getpid() % 100'000 * 10 + offset;
}

struct SlotCleanup
{
  explicit SlotCleanup(int saveSlot) : slot(saveSlot) {}
  SlotCleanup(const SlotCleanup&) = delete;
  SlotCleanup& operator=(const SlotCleanup&) = delete;

  int slot;
  ~SlotCleanup() { RuntimePaths::removeSave(slot); }
};

void capture(GUIView& view, const char* name)
{
  const auto path = RuntimePaths::capturePath(name);
  std::filesystem::remove(path);
  EXPECT_TRUE(view.captureScreenshot(path.string())) << name;
}

void resize(GUIView& view, int width, int height)
{
  SDL_SetWindowSize(view.getWindow(), width, height);
  SDL_Event event{.window = {}};
  event.window.type = SDL_EVENT_WINDOW_RESIZED;
  event.window.windowID = SDL_GetWindowID(view.getWindow());
  event.window.data1 = width;
  event.window.data2 = height;
  SDL_PushEvent(&event);
}

void frames(GUIView& view, int count)
{
  for (int index = 0; index < count; ++index) Bridge::frame(view);
}

void hoverEverywhere(GUIView& view, float step = 48.0f)
{
  ImGuiIO& io = ImGui::GetIO();
  const ImVec2 size = io.DisplaySize;
  for (int y_index = 0; step * (static_cast<float>(y_index) + 0.5f) < size.y;
       ++y_index)
    for (int x_index = 0; step * (static_cast<float>(x_index) + 0.5f) < size.x;
         ++x_index)
    {
      const float y = step * (static_cast<float>(y_index) + 0.5f);
      const float x = step * (static_cast<float>(x_index) + 0.5f);
      io.AddMousePosEvent(x, y);
      Bridge::frame(view);
    }
  io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
  Bridge::frame(view);
}

void setUiScale(GUIView& view, float scale)
{
  SettingsManager::instance()->get().ui_scale = scale;
  view.refreshTheme();
}

InboxScene* openInbox(GUIView& view)
{
  Navigation::open(&view, NavSection::INBOX);
  frames(view, 3);
  return dynamic_cast<InboxScene*>(Bridge::activeScene(view));
}

/** Raises a training-ground clash today, with its message. */
void raiseClash(GameController& controller)
{
  const TeamID club = controller.getManagedTeam()->get().getId();
  const auto& squad = controller.getPlayersForTeam(club);
  ASSERT_GE(squad.size(), 2u);
  StoryEngine& stories = controller.getGame()->getWorld().getStories();
  StoryState state = stories.getState();
  const std::int32_t today = dayOrdinal(controller.getCurrentDate());
  Dilemma dilemma;
  dilemma.kind = StoryKind::TrainingClash;
  dilemma.subject = squad[0].get().getId();
  dilemma.other = squad[1].get().getId();
  dilemma.day = today;
  dilemma.expires_day = today + Stories::DILEMMA_ANSWER_DAYS;
  state.dilemmas.push_back(dilemma);
  stories.restore(state);
  InboxMessage message;
  message.date = controller.getCurrentDate();
  message.title_key = Stories::dilemmaTitleKey(StoryKind::TrainingClash);
  message.body_key = Stories::dilemmaBodyKey(StoryKind::TrainingClash);
  message.args = {squad[0].get().getName(), squad[1].get().getName(), ""};
  message.player_id = dilemma.subject;
  message.team_id = club;
  controller.getGame()->getWorld().getInbox().add(std::move(message));
}
}  // namespace

TEST(InboxUiTest, DecisionMomentShowsItsAnswersAndEffects)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  const SlotCleanup slot{uniqueSlot(1)};
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  controller.selectManagedTeam(controller.getTeams().front().get().getId());
  raiseClash(controller);

  // Both answers list what they do, in words.
  for (const int option : {0, 1})
  {
    const auto effects = controller.getDilemmaEffects(option);
    ASSERT_TRUE(effects.has_value());
    const auto lines = InboxDilemmaCard::effectLines(*effects, "A", "B");
    EXPECT_GE(lines.size(), 2u);
    for (const auto& line : lines) EXPECT_FALSE(line.text.empty());
  }

  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  resize(view, 1280, 720);
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view, 2);
  InboxScene* scene = openInbox(view);
  ASSERT_NE(scene, nullptr);
  Bridge::pickTab(*scene, 0);
  frames(view, 2);
  EXPECT_TRUE(Bridge::hasDilemma(*scene));
  capture(view, "inbox_dilemma.bmp");
  hoverEverywhere(view);

  setUiScale(view, 2.0f);
  resize(view, 900, 700);
  frames(view, 3);
  capture(view, "inbox_dilemma_narrow_scale2.bmp");
  hoverEverywhere(view, 64.0f);
  setUiScale(view, 1.0f);
  resize(view, 1280, 720);
  frames(view, 2);

  // Answered: the decision leaves the tab and the answer is on file.
  ASSERT_TRUE(controller.resolveDilemma(1));
  Navigation::open(&view, NavSection::HOME);
  frames(view, 3);
  scene = openInbox(view);
  ASSERT_NE(scene, nullptr);
  frames(view, 2);
  EXPECT_FALSE(Bridge::hasDilemma(*scene));
  EXPECT_EQ(controller.getInbox().back().title_key, "INBOX_DILEMMA_DONE_TITLE");
}

TEST(InboxUiTest, FiltersSurviveASceneChangeAndAReload)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  const SlotCleanup slot{uniqueSlot(2)};
  auto controller = std::make_unique<GameController>();
  controller->newGame(slot.slot, WORLD_SEED);
  const TeamID club = controller->getTeams().front().get().getId();
  controller->selectManagedTeam(club);
  PlayerID followed = 0;
  for (const auto& team : controller->getTeams())
    if (team.get().getId() != club &&
        !controller->getPlayersForTeam(team.get().getId()).empty())
    {
      followed = controller->getPlayersForTeam(team.get().getId())
                     .front()
                     .get()
                     .getId();
      break;
    }
  ASSERT_NE(followed, 0u);
  ASSERT_TRUE(controller->followPlayer(followed));
  // A message about him, so the followed feed has something to show.
  InboxMessage message;
  message.date = controller->getCurrentDate();
  message.category = InboxCategory::Transfer;
  message.title_key = "INBOX_FOLLOW_TRANSFER_TITLE";
  message.body_key = "INBOX_FOLLOW_TRANSFER_BODY";
  message.args = {"A", "B", "C"};
  message.player_id = followed;
  controller->getGame()->getWorld().getInbox().add(std::move(message));

  {
    GUIView view(*controller);
    ASSERT_TRUE(Bridge::initialize(view));
    resize(view, 1280, 720);
    view.changeScene(std::make_unique<MainGameScene>(&view));
    frames(view, 2);
    InboxScene* scene = openInbox(view);
    ASSERT_NE(scene, nullptr);
    Bridge::pickTab(*scene, 1);
    Bridge::pickCategory(*scene, -2);
    frames(view, 2);
    EXPECT_EQ(Bridge::threadCount(*scene), 1u) << "only the followed player";
    capture(view, "inbox_followed_filter.bmp");
    hoverEverywhere(view);

    // Away and back: the same filters.
    Navigation::open(&view, NavSection::HOME);
    frames(view, 3);
    scene = openInbox(view);
    ASSERT_NE(scene, nullptr);
    EXPECT_EQ(Bridge::tab(*scene), 1);
    EXPECT_TRUE(Bridge::followedOnly(*scene));
    EXPECT_EQ(Bridge::threadCount(*scene), 1u);

    Bridge::pickCategory(
        *scene, static_cast<int>(std::to_underlying(InboxCategory::Transfer)));
    frames(view, 2);
  }
  controller->saveGame();

  // A reload brings the career's filters back.
  auto reloaded = std::make_unique<GameController>();
  ASSERT_TRUE(reloaded->loadGame(slot.slot));
  GUIView view(*reloaded);
  ASSERT_TRUE(Bridge::initialize(view));
  resize(view, 1280, 720);
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view, 2);
  InboxScene* scene = openInbox(view);
  ASSERT_NE(scene, nullptr);
  EXPECT_EQ(Bridge::tab(*scene), 1);
  EXPECT_EQ(Bridge::category(*scene),
            static_cast<int>(InboxCategory::Transfer));
  EXPECT_FALSE(Bridge::followedOnly(*scene));
  EXPECT_TRUE(reloaded->isFollowingPlayer(followed));
  frames(view, 2);
}
