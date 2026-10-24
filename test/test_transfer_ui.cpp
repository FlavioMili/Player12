// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The talks over an AI club's bid for a managed player through the real
// GUIView: the bid arrives in the inbox, the Negotiate button opens the
// talks, the club counters, the buyer answers on a later day with its own
// offer, the club accepts, the player leaves and the ledger shows the
// upfront payment. The dialog is also opened from the transfer market and
// captured at 1280x720, 900x700 and 2560x1440 at scale 2 (screenshots in
// the runtime captures folder; FM_KEEP_TEST_ARTIFACTS=1 keeps them).

#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/language_manager.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "gui/gui_view.h"
#include "gui/scenes/contract_talks_dialog.h"
#include "gui/scenes/inbox_scene.h"
#include "gui/scenes/main_game_scene.h"
#include "gui/scenes/management_scene.h"
#include "gui/scenes/offer_negotiation_dialog.h"
#include "gui/scenes/transfer_market_scene.h"
#include "gui/scenes/transfer_terms_editor.h"
#include "model/buyer_negotiation.h"
#include "model/finances.h"
#include "model/game.h"
#include "model/inbox.h"
#include "model/player_agent.h"
#include "model/settings_manager.h"
#include "model/transfer_market.h"
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
  static void refresh(InboxScene& scene) { scene.refresh(); }
  static bool offersNegotiation(const InboxScene& scene, std::uint32_t offer_id)
  {
    for (const auto& decision : scene.decisions)
      for (const auto& option : decision.options)
        if (option.id == offer_id && option.negotiable) return true;
    return false;
  }
  static OfferNegotiationDialog& talks(InboxScene& scene)
  {
    return scene.offer_dialog;
  }
  static OfferNegotiationDialog& talks(TransferMarketScene& scene)
  {
    return scene.negotiation_dialog;
  }
  static void counter(OfferNegotiationDialog& dialog,
                      const TransferNegotiation::OfferTerms& terms,
                      GameController& controller)
  {
    dialog.mode = OfferNegotiationDialog::Mode::Counter;
    dialog.counter = terms;
    dialog.submit(controller);
  }
  static void accept(OfferNegotiationDialog& dialog, GameController& controller)
  {
    dialog.accept(controller);
  }
  static const TransferNegotiation::OfferTerms& counterTerms(
      const OfferNegotiationDialog& dialog)
  {
    return dialog.counter;
  }
  static bool finished(const OfferNegotiationDialog& dialog)
  {
    return dialog.finished;
  }
  static std::size_t roundLines(const OfferNegotiationDialog& dialog)
  {
    return dialog.rounds.size();
  }
  static const std::optional<GameController::IncomingOfferView>& view(
      const OfferNegotiationDialog& dialog)
  {
    return dialog.view;
  }
  static void counterLoan(OfferNegotiationDialog& dialog,
                          const TransferNegotiation::LoanTerms& terms,
                          GameController& controller)
  {
    dialog.loan_counter = terms;
    dialog.submit(controller);
  }
  static const TransferNegotiation::LoanTerms& loanCounter(
      const OfferNegotiationDialog& dialog)
  {
    return dialog.loan_counter;
  }
  static ContractTalksDialog& contractTalks(TransferMarketScene& scene)
  {
    return scene.contract_talks;
  }
  static void proposePackage(ContractTalksDialog& dialog,
                             GameController& controller)
  {
    dialog.offer = PlayerAgent::askedOffer(dialog.agent, dialog.offer.years);
    dialog.agent_fee = dialog.agent.agent_fee;
    dialog.propose(controller);
  }
  static std::size_t agentLines(const ContractTalksDialog& dialog)
  {
    return dialog.agent_lines.size();
  }
};

using Bridge = GameFlowTest_GUIFlowLifecycle_Test;

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

int uniqueSlot(int offset)
{
  return 720'000 + (getpid() % 100'000) * 10 + offset;
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
  EXPECT_TRUE(std::filesystem::exists(path)) << name;
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

void setUiScale(GUIView& view, float scale)
{
  SettingsManager::instance()->get().ui_scale = scale;
  view.refreshTheme();
}

TeamID manageSmallClub(GameController& controller)
{
  TeamID club = 0;
  int lowest = std::numeric_limits<int>::max();
  for (const auto& team : controller.getTeams())
  {
    const TeamID id = team.get().getId();
    // Its country's transfer window must be open today.
    if (id != FREE_AGENTS_TEAM_ID && team.get().getReputation() < lowest &&
        controller.isTransferWindowOpenFor(id) &&
        !controller.getPlayersForTeam(id).empty())
    {
      lowest = team.get().getReputation();
      club = id;
    }
  }
  controller.selectManagedTeam(club);
  return club;
}

struct Bid
{
  PlayerID player = 0;
  TeamID buyer = 0;
  std::uint32_t offer_id = 0;
};

/**
 * Stands in for an AI club's approach and its inbox message: a bid from a
 * clearly bigger, well funded club for a managed player who wants the move
 * (made ambitious here), so personal terms never fail.
 */
Bid receiveBid(GameController& controller, TeamID managed, std::uint32_t fee,
               std::uint32_t ceiling, PlayerID skip = 0)
{
  auto data = controller.getGameData();
  TransferMarket& market = controller.getGame()->getTransfers();
  std::vector<TeamID> buyers;
  for (const auto& team : controller.getTeams())
    if (team.get().getId() != managed &&
        team.get().getId() != FREE_AGENTS_TEAM_ID &&
        controller.isTransferWindowOpenFor(team.get().getId()))
      buyers.push_back(team.get().getId());
  std::ranges::sort(buyers,
                    [&](TeamID a, TeamID b)
                    {
                      return controller.getTeamById(a)->get().getReputation() >
                             controller.getTeamById(b)->get().getReputation();
                    });
  const GameDateValue today = controller.getCurrentDate();
  // The best players are likeliest to be first-teamers at a bigger club.
  std::vector<PlayerID> candidates;
  for (const auto& reference : controller.getPlayersForTeam(managed))
    candidates.push_back(reference.get().getId());
  const StatsConfig& config = data->getStatsConfig();
  std::ranges::sort(candidates,
                    [&](PlayerID a, PlayerID b)
                    {
                      return data->getPlayer(a)->get().getOverall(config) >
                             data->getPlayer(b)->get().getOverall(config);
                    });
  for (const PlayerID candidate : candidates)
  {
    Player& player = data->getPlayers().at(candidate);
    if (player.getId() == skip || !market.canBeTraded(player.getId())) continue;
    PlayerTraits traits = player.getTraits();
    traits.ambition = 90;
    traits.loyalty = 20;
    player.setTraits(traits);
    for (const TeamID buyer : buyers)
    {
      IncomingOffer offer;
      offer.player_id = player.getId();
      offer.buyer = buyer;
      offer.terms = TransferNegotiation::aiOfferTerms(fee);
      offer.max_fee = ceiling;
      offer.patience = 3;
      offer.created = today;
      offer.expires = today + 5;
      const std::uint32_t id = market.addIncomingOffer(offer);
      if (const auto view = controller.getIncomingOfferView(id);
          view &&
          (view->stance == BuyerNegotiation::PlayerStance::WantsBiggerClub ||
           view->stance == BuyerNegotiation::PlayerStance::AskedToLeave))
      {
        data->getTeams().at(buyer).getFinances().addBalance(500'000'000LL);
        controller.getGame()->getWorld().onTransferBid(today, player.getId(),
                                                       buyer, fee, managed);
        return {player.getId(), buyer, id};
      }
      market.removeIncomingOffer(id);
    }
  }
  return {};
}

/** Clicks the Negotiate button of the decision card for @p offer_id. */
bool clickNegotiate(GUIView& view, std::uint32_t offer_id)
{
  const char* label = LOC("INBOX_DECISION_NEGOTIATE");
  const auto push_id = static_cast<int>(offer_id);
  ImGuiIO& io = ImGui::GetIO();
  std::vector<ImGuiWindow*> cards;
  for (ImGuiWindow* window : GImGui->Windows)
    if (window->Active && std::string_view(window->Name).find("/decision_") !=
                              std::string_view::npos)
      cards.push_back(window);
  for (const ImGuiWindow* card : cards)
  {
    const ImGuiID button =
        ImHashStr(label, 0, ImHashData(&push_id, sizeof(push_id), card->ID));
    const ImRect area(ImMax(card->Rect().Min, ImVec2(0.0f, 0.0f)),
                      ImMin(card->Rect().Max, io.DisplaySize));
    for (int y_index = 0;
         area.Min.y + 4.0f + 6.0f * static_cast<float>(y_index) < area.Max.y;
         ++y_index)
      for (int x_index = 0;
           area.Min.x + 4.0f + 12.0f * static_cast<float>(x_index) < area.Max.x;
           ++x_index)
      {
        const float y = area.Min.y + 4.0f + 6.0f * static_cast<float>(y_index);
        const float x = area.Min.x + 4.0f + 12.0f * static_cast<float>(x_index);
        io.AddMousePosEvent(x, y);
        Bridge::frame(view);
        if (GImGui->HoveredId != button) continue;
        io.AddMouseButtonEvent(0, true);
        Bridge::frame(view);
        io.AddMouseButtonEvent(0, false);
        Bridge::frame(view);
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        Bridge::frame(view);
        return true;
      }
  }
  io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
  Bridge::frame(view);
  return false;
}

/**
 * The talks dialog fits the window: no sideways scrolling, no scrolling
 * surface inside it (the dialog itself is the only one) and never wider
 * than the display. Returns what is wrong (empty when fine).
 */
std::string dialogLayoutProblems(
    std::string_view popup = "###offer_negotiation")
{
  std::string problems;
  const ImGuiWindow* dialog = nullptr;
  for (const ImGuiWindow* window : GImGui->Windows)
    if (window->Active && std::string_view(window->Name).ends_with(popup))
      dialog = window;
  if (dialog == nullptr) return "dialog not shown";
  if (dialog->ScrollbarX) problems += " [sideways scrolling]";
  if (dialog->Size.x > ImGui::GetIO().DisplaySize.x + 0.5f)
    problems += " [wider than the window]";
  for (const ImGuiWindow* window : GImGui->Windows)
    if (window->Active && window != dialog && window->RootWindow == dialog &&
        window->ScrollbarY)
      problems += std::format(" [inner scrolling: {}]", window->Name);
  return problems;
}

void waitForAnswer(GameController& controller, std::uint32_t offer_id)
{
  for (int day = 0; day < 3; ++day)
  {
    if (const IncomingOffer* offer =
            controller.getGame()->getTransfers().findIncomingOffer(offer_id);
        offer == nullptr || offer->status != OfferStatus::AwaitingBuyer)
      return;
    controller.advanceDay();
  }
}
}  // namespace

TEST(TransferUiTest, BidIsNegotiatedFromTheInboxToTheSale)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  const SlotCleanup slot{uniqueSlot(0)};
  SettingsManager::instance()->get().screen_tips = false;
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  const TeamID managed = manageSmallClub(controller);
  ASSERT_TRUE(controller.isTransferWindowOpen());
  const Bid bid = receiveBid(controller, managed, 1'000'000, 1'500'000);
  ASSERT_NE(bid.offer_id, 0u) << "no managed player keen on a bigger club";

  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  resize(view, 1280, 720);
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view, 2);
  Navigation::open(&view, NavSection::INBOX);
  frames(view, 3);
  auto* inbox = dynamic_cast<InboxScene*>(Bridge::activeScene(view));
  ASSERT_NE(inbox, nullptr);
  Bridge::refresh(*inbox);
  frames(view, 2);
  ASSERT_TRUE(Bridge::offersNegotiation(*inbox, bid.offer_id))
      << "the bid is a pending decision with a Negotiate button";

  // Negotiate opens the talks.
  ASSERT_TRUE(clickNegotiate(view, bid.offer_id));
  OfferNegotiationDialog& talks = Bridge::talks(*inbox);
  ASSERT_TRUE(talks.isOpen());
  frames(view, 2);
  EXPECT_EQ(dialogLayoutProblems(), "");
  capture(view, "offer_talks_inbox_1280.bmp");

  // A structured counter well above the ceiling: sent, answered later.
  TransferNegotiation::OfferTerms asked;
  asked.fee = 2'200'000;
  asked.upfront_percent = 60;
  asked.instalment_years = 2;
  asked.sell_on_percent = 10;
  Bridge::counter(talks, asked, controller);
  ASSERT_TRUE(Bridge::view(talks).has_value());
  EXPECT_EQ(Bridge::view(talks)->status, OfferStatus::AwaitingBuyer);
  frames(view, 2);
  capture(view, "offer_talks_awaiting_1280.bmp");

  waitForAnswer(controller, bid.offer_id);
  Bridge::refresh(*inbox);
  frames(view, 3);
  const auto& answered = Bridge::view(talks);
  ASSERT_TRUE(answered.has_value()) << "the buyer came back with an offer";
  EXPECT_EQ(answered->status, OfferStatus::AwaitingClub);
  ASSERT_GE(Bridge::roundLines(talks), 3u) << "bid, counter and answer shown";
  EXPECT_TRUE(BuyerNegotiation::byBuyer(answered->history.back().move));
  EXPECT_GT(answered->terms.fee, 1'000'000u);
  EXPECT_TRUE(std::ranges::any_of(
      controller.getInbox(), [](const InboxMessage& message)
      { return message.title_key == "INBOX_OFFER_REPLY_TITLE"; }));

  // The dialog at a narrow window and at 2560x1440 with scale 2.
  resize(view, 900, 700);
  frames(view, 3);
  EXPECT_EQ(dialogLayoutProblems(), "") << "900x700";
  capture(view, "offer_talks_900.bmp");
  setUiScale(view, 2.0f);
  resize(view, 2560, 1440);
  frames(view, 3);
  EXPECT_EQ(dialogLayoutProblems(), "") << "2560x1440 scale 2";
  capture(view, "offer_talks_2560_scale2.bmp");
  setUiScale(view, 0.0f);
  resize(view, 1280, 720);
  frames(view, 3);

  // Accept: the player moves and the upfront part is booked today.
  const TransferNegotiation::OfferTerms agreed = answered->terms;
  const GameDateValue today = controller.getCurrentDate();
  Bridge::accept(talks, controller);
  EXPECT_TRUE(Bridge::finished(talks));
  frames(view, 2);
  capture(view, "offer_talks_sold_1280.bmp");
  auto data = controller.getGameData();
  EXPECT_EQ(data->getPlayer(bid.player)->get().getTeamId(), bid.buyer);
  const auto& ledger = data->getTeams().at(managed).getFinances().getLedger();
  EXPECT_TRUE(std::ranges::any_of(
      ledger,
      [&](const FinanceTransaction& entry)
      {
        return entry.date == today &&
               entry.category == FinanceCategory::TransferFeeIn &&
               entry.amount == static_cast<std::int64_t>(
                                   TransferNegotiation::upfrontAmount(agreed));
      }))
      << "upfront payment in the ledger";
}

TEST(TransferUiTest, TalksOpenFromTheTransferMarket)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  const SlotCleanup slot{uniqueSlot(1)};
  SettingsManager::instance()->get().screen_tips = false;
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  const TeamID managed = manageSmallClub(controller);
  const Bid first = receiveBid(controller, managed, 1'000'000, 1'500'000);
  ASSERT_NE(first.offer_id, 0u);
  // A rival bid for the same player shows in the talks.
  TransferMarket& market = controller.getGame()->getTransfers();
  IncomingOffer rival = *market.findIncomingOffer(first.offer_id);
  rival.history.clear();
  for (const auto& team : controller.getTeams())
    if (team.get().getId() != managed && team.get().getId() != first.buyer &&
        team.get().getId() != FREE_AGENTS_TEAM_ID)
      rival.buyer = team.get().getId();
  market.addIncomingOffer(rival);

  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  resize(view, 1280, 720);
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view, 2);
  Navigation::open(&view, NavSection::TRANSFERS);
  frames(view, 3);
  auto* scene = dynamic_cast<TransferMarketScene*>(Bridge::activeScene(view));
  ASSERT_NE(scene, nullptr);
  OfferNegotiationDialog& talks = Bridge::talks(*scene);
  talks.open(controller, first.offer_id);
  frames(view, 3);
  ASSERT_TRUE(talks.isOpen());
  ASSERT_TRUE(Bridge::view(talks).has_value());
  EXPECT_EQ(Bridge::view(talks)->rivals, 1);
  EXPECT_EQ(dialogLayoutProblems(), "");
  capture(view, "offer_talks_market_1280.bmp");
  resize(view, 900, 700);
  frames(view, 3);
  EXPECT_EQ(dialogLayoutProblems(), "") << "900x700";
  capture(view, "offer_talks_market_900.bmp");
  resize(view, 1280, 720);
  frames(view, 2);
}

namespace
{
/** The segment the editor highlights for @p value is @p value itself. */
template <typename T, std::size_t N>
bool highlighted(const std::array<T, N>& options, int value)
{
  return static_cast<int>(options[static_cast<std::size_t>(
             TransferTermsEditor::optionIndex(options, value))]) == value;
}
}  // namespace

TEST(TransferUiTest, EveryBuyerStructureIsASegmentOfTheEditor)
{
  using namespace TransferTermsEditor;
  using TransferNegotiation::OfferTerms;
  // Opening bids of every size and the answers to counters built from the
  // editor's own segments: each upfront share and sell-on the buyer can
  // propose must be shown exactly, never rounded to a neighbour.
  std::vector<OfferTerms> proposals;
  for (const std::uint32_t ceiling : {800'000u, 6'000'000u, 30'000'000u})
    for (const double roll : {0.0, 0.5, 0.99})
      proposals.push_back(
          BuyerNegotiation::openingBid(ceiling, 22, 0, roll, roll));
  for (const std::uint8_t upfront : UPFRONT_OPTIONS)
    for (const std::uint8_t sell_on : SELL_ON_OPTIONS)
      for (const double roll : {0.1, 0.5, 0.9})
        for (const std::int64_t cash :
             {std::int64_t{1'000'000'000}, std::int64_t{2'000'000}})
        {
          BuyerNegotiation::BuyerContext context;
          context.ceiling = 10'000'000;
          context.cash = cash;
          context.age = 20;
          OfferTerms asked;
          asked.fee = 12'000'000;
          asked.upfront_percent = upfront;
          asked.instalment_years = upfront < 100 ? 3 : 0;
          asked.sell_on_percent = sell_on;
          proposals.push_back(BuyerNegotiation::respond(
                                  context,
                                  TransferNegotiation::aiOfferTerms(8'000'000),
                                  asked, false, roll)
                                  .terms);
        }
  for (const OfferTerms& terms : proposals)
  {
    const int upfront =
        terms.instalment_years > 0 ? terms.upfront_percent : 100;
    EXPECT_TRUE(highlighted(UPFRONT_OPTIONS, upfront))
        << upfront << "% upfront";
    EXPECT_TRUE(highlighted(SELL_ON_OPTIONS, terms.sell_on_percent))
        << static_cast<int>(terms.sell_on_percent) << "% sell-on";
    if (terms.appearance_bonus > 0)
      EXPECT_TRUE(highlighted(APPEARANCE_TARGETS, terms.appearance_target));
    if (terms.goal_bonus > 0)
      EXPECT_TRUE(highlighted(GOAL_TARGETS, terms.goal_target));
  }
}

TEST(TransferUiTest, CounterStartsFromTheBuyersExactStructure)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  const SlotCleanup slot{uniqueSlot(2)};
  SettingsManager::instance()->get().screen_tips = false;
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  const TeamID managed = manageSmallClub(controller);
  // A bid of at least 5M comes with 50% upfront and instalments.
  const Bid bid = receiveBid(controller, managed, 6'000'000, 8'000'000);
  ASSERT_NE(bid.offer_id, 0u);
  const auto offer = controller.getIncomingOfferView(bid.offer_id);
  ASSERT_TRUE(offer.has_value());
  ASSERT_EQ(offer->terms.upfront_percent, 50);
  ASSERT_GT(offer->terms.instalment_years, 0);

  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  resize(view, 1280, 720);
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view, 2);
  Navigation::open(&view, NavSection::TRANSFERS);
  frames(view, 3);
  auto* scene = dynamic_cast<TransferMarketScene*>(Bridge::activeScene(view));
  ASSERT_NE(scene, nullptr);
  OfferNegotiationDialog& talks = Bridge::talks(*scene);
  talks.open(controller, bid.offer_id);
  frames(view, 3);
  ASSERT_TRUE(talks.isOpen());
  // The counter starts from the bid's structure, and the upfront segment
  // shown is the share that would be sent.
  const TransferNegotiation::OfferTerms& counter = Bridge::counterTerms(talks);
  EXPECT_EQ(counter.upfront_percent, offer->terms.upfront_percent);
  EXPECT_EQ(counter.instalment_years, offer->terms.instalment_years);
  EXPECT_TRUE(highlighted(TransferTermsEditor::UPFRONT_OPTIONS,
                          counter.upfront_percent));
  EXPECT_TRUE(highlighted(TransferTermsEditor::SELL_ON_OPTIONS,
                          counter.sell_on_percent));
  capture(view, "offer_talks_structured_bid_1280.bmp");
}

namespace
{
/** Manages the smallest club whose transfer window is open today. */
TeamID manageOpenClub(GameController& controller)
{
  TeamID club = 0;
  int lowest = std::numeric_limits<int>::max();
  for (const auto& team : controller.getTeams())
  {
    const TeamID id = team.get().getId();
    if (id != FREE_AGENTS_TEAM_ID && controller.isTransferWindowOpenFor(id) &&
        team.get().getReputation() < lowest &&
        controller.getPlayersForTeam(id).size() > 16)
    {
      lowest = team.get().getReputation();
      club = id;
    }
  }
  controller.selectManagedTeam(club);
  return club;
}

/**
 * A loan offer from a well funded club with room for his wage for a managed
 * outfield player, announced in the inbox like the AI's own offers.
 */
Bid receiveLoanOffer(GameController& controller, TeamID managed,
                     std::uint8_t share, double ceiling_share)
{
  auto data = controller.getGameData();
  TransferMarket& market = controller.getGame()->getTransfers();
  PlayerID player = 0;
  for (const auto& reference : controller.getPlayersForTeam(managed))
    if (reference.get().getRole() != PlayerRole::GK &&
        market.canBeTraded(reference.get().getId()) &&
        reference.get().getContractYears() > 1 &&
        controller.getSaleBlock(reference.get().getId()) ==
            GameController::PlayerActionBlock::None)
      player = reference.get().getId();
  TeamID buyer = 0;
  for (const auto& team : controller.getTeams())
    if (team.get().getId() != managed &&
        team.get().getId() != FREE_AGENTS_TEAM_ID &&
        controller.isTransferWindowOpenFor(team.get().getId()))
      buyer = team.get().getId();
  if (player == 0 || buyer == 0) return {};
  const std::uint32_t wage = data->getPlayer(player)->get().getWage();
  Finances& finances = data->getTeams().at(buyer).getFinances();
  finances.addBalance(200'000'000LL);
  finances.setWageBudget(controller.getWeeklyWageBill(buyer) + 10 * wage +
                         1'000'000);
  const GameDateValue today = controller.getCurrentDate();
  IncomingOffer offer;
  offer.player_id = player;
  offer.buyer = buyer;
  offer.loan = true;
  offer.loan_terms.wage_share = share;
  offer.max_fee = static_cast<std::uint32_t>(wage * 52 * ceiling_share);
  offer.patience = 3;
  offer.created = today;
  offer.expires = today + 5;
  const std::uint32_t id = market.addIncomingOffer(offer);
  InboxMessage message;
  message.date = today;
  message.category = InboxCategory::Transfer;
  message.title_key = "INBOX_LOAN_OFFER_TITLE";
  message.body_key = "INBOX_LOAN_OFFER_BODY";
  message.args = {data->getPlayer(player)->get().getName(),
                  controller.getTeamById(buyer)->get().getName(),
                  std::to_string(static_cast<int>(share))};
  message.player_id = player;
  message.team_id = buyer;
  controller.getGame()->getWorld().getInbox().add(std::move(message));
  return {player, buyer, id};
}
}  // namespace

TEST(TransferUiTest, LoanIsNegotiatedInTheTalksAtEverySize)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  const SlotCleanup slot{uniqueSlot(3)};
  SettingsManager::instance()->get().screen_tips = false;
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  const TeamID managed = manageOpenClub(controller);
  ASSERT_TRUE(controller.isTransferWindowOpen());
  const Bid loan = receiveLoanOffer(controller, managed, 40, 1.3);
  ASSERT_NE(loan.offer_id, 0u);

  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  resize(view, 1280, 720);
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view, 2);
  // The inbox offers talks for the loan, not just accept or reject.
  Navigation::open(&view, NavSection::INBOX);
  frames(view, 3);
  auto* inbox = dynamic_cast<InboxScene*>(Bridge::activeScene(view));
  ASSERT_NE(inbox, nullptr);
  Bridge::refresh(*inbox);
  frames(view, 2);
  EXPECT_TRUE(Bridge::offersNegotiation(*inbox, loan.offer_id));

  Navigation::open(&view, NavSection::TRANSFERS);
  frames(view, 3);
  auto* scene = dynamic_cast<TransferMarketScene*>(Bridge::activeScene(view));
  ASSERT_NE(scene, nullptr);
  OfferNegotiationDialog& talks = Bridge::talks(*scene);
  talks.open(controller, loan.offer_id);
  frames(view, 3);
  ASSERT_TRUE(talks.isOpen());
  ASSERT_TRUE(Bridge::view(talks).has_value());
  EXPECT_TRUE(Bridge::view(talks)->loan);
  // The counter starts above their share, on the editor's steps.
  EXPECT_GT(Bridge::loanCounter(talks).wage_share, 40);
  EXPECT_EQ(Bridge::loanCounter(talks).wage_share %
                TransferTermsEditor::LOAN_WAGE_SHARE_STEP,
            0);
  EXPECT_EQ(dialogLayoutProblems(), "") << "1280x720";
  capture(view, "loan_talks_1280.bmp");

  TransferNegotiation::LoanTerms asked;
  asked.wage_share = 70;
  asked.recall_clause = true;
  asked.min_appearances = 5;
  // A few weeks of his wage if he plays fewer games.
  asked.unplayed_fee =
      (controller.getGameData()->getPlayer(loan.player)->get().getWage() * 4 /
           1'000 +
       1) *
      1'000;
  Bridge::counterLoan(talks, asked, controller);
  frames(view, 2);
  const bool awaiting =
      Bridge::view(talks).has_value() &&
      Bridge::view(talks)->status == OfferStatus::AwaitingBuyer;
  EXPECT_TRUE(awaiting || Bridge::finished(talks));
  capture(view, "loan_talks_awaiting_1280.bmp");

  // At 2560x1440 with scale 2 the talks still fit without inner scrolling.
  setUiScale(view, 2.0f);
  resize(view, 2560, 1440);
  frames(view, 3);
  EXPECT_EQ(dialogLayoutProblems(), "") << "2560x1440 scale 2";
  capture(view, "loan_talks_2560_scale2.bmp");
  setUiScale(view, 0.0f);
  resize(view, 1280, 720);
  frames(view, 3);

  if (awaiting) waitForAnswer(controller, loan.offer_id);
  frames(view, 3);
  if (const IncomingOffer* open =
          controller.getGame()->getTransfers().findIncomingOffer(loan.offer_id);
      open != nullptr && open->status == OfferStatus::AwaitingClub)
    Bridge::accept(talks, controller);
  frames(view, 2);
  capture(view, "loan_talks_done_1280.bmp");
  auto data = controller.getGameData();
  EXPECT_EQ(data->getPlayer(loan.player)->get().getTeamId(), loan.buyer);
  const LoanDeal* deal =
      controller.getGame()->getTransfers().findLoan(loan.player);
  ASSERT_NE(deal, nullptr);
  EXPECT_TRUE(deal->recall_clause);
}

TEST(TransferUiTest, ContractTalksWithTheAgentFitTheWindow)
{
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Logger::init();
  const SlotCleanup slot{uniqueSlot(4)};
  SettingsManager::instance()->get().screen_tips = false;
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  const TeamID managed = manageOpenClub(controller);
  auto data = controller.getGameData();
  Finances& finances = data->getTeams().at(managed).getFinances();
  finances.addBalance(50'000'000LL);
  finances.setWageBudget(controller.getWeeklyWageBill(managed) + 500'000);
  PlayerID own = 0;
  for (const auto& reference : controller.getPlayersForTeam(managed))
    if (controller.getRenewalBlock(reference.get().getId()) ==
        GameController::PlayerActionBlock::None)
      own = reference.get().getId();
  ASSERT_NE(own, 0u);
  const std::uint8_t before = data->getPlayer(own)->get().getContractYears();

  GUIView view(controller);
  ASSERT_TRUE(Bridge::initialize(view));
  resize(view, 1280, 720);
  view.changeScene(std::make_unique<MainGameScene>(&view));
  frames(view, 2);
  Navigation::open(&view, NavSection::TRANSFERS);
  frames(view, 3);
  auto* scene = dynamic_cast<TransferMarketScene*>(Bridge::activeScene(view));
  ASSERT_NE(scene, nullptr);
  ContractTalksDialog& talks = Bridge::contractTalks(*scene);
  ASSERT_TRUE(talks.open(controller, own));
  frames(view, 3);
  ASSERT_TRUE(talks.isOpen());
  EXPECT_EQ(Bridge::agentLines(talks), 1u) << "the agent opens the talks";
  EXPECT_EQ(dialogLayoutProblems("###contract_talks"), "") << "1280x720";
  capture(view, "contract_talks_1280.bmp");
  setUiScale(view, 2.0f);
  resize(view, 2560, 1440);
  frames(view, 3);
  EXPECT_EQ(dialogLayoutProblems("###contract_talks"), "")
      << "2560x1440 scale 2";
  capture(view, "contract_talks_2560_scale2.bmp");
  setUiScale(view, 0.0f);
  resize(view, 1280, 720);
  frames(view, 3);
  // His agent's own package is a proposal he answers, with a line.
  Bridge::proposePackage(talks, controller);
  frames(view, 2);
  EXPECT_EQ(Bridge::agentLines(talks), 2u);
  EXPECT_GE(data->getPlayer(own)->get().getContractYears(), before);
}
