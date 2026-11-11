// Offline integration coverage for per-match possession dispatch and
// persistence.
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "model/match_engine.h"
#include "model/match_possession_claim.h"
#include "model/match_possession_control.h"
#include "model/match_recorder.h"
#include "model/player.h"
#include "tools/lab_fixtures.h"
#include "tools/match_sandbox_drill.h"
#include "tools/match_sandbox_recorder.h"
#include "tools/match_sandbox_recording.h"
#include "tools/match_sandbox_setup.h"

namespace
{
int failures = 0;
PossessionId testedPossession = PossessionId::AARDVARK;
PossessionId otherPossession()
{
  return testedPossession == PossessionId::AARDVARK ? PossessionId::BINTURONG
                                                    : PossessionId::AARDVARK;
}
constexpr auto OBSERVATION_COUNT =
    static_cast<std::size_t>(PossessionObservation::COUNT);
std::array<unsigned, OBSERVATION_COUNT> totalRoutes{};
unsigned contestedHeaders = 0;
unsigned controlCollections = 0, controlLosses = 0, controlGraceSamples = 0;
unsigned controlReacquisitions = 0;
unsigned claimExpiries = 0;
unsigned retainedBeyondCollectionReach = 0;
void check(bool value, const char* message)
{
  if (!value)
  {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

template <class T>
concept ExposesPossessionStorage = requires(T& value) { value.possession; };
template <class T>
concept ExposesLegacyOwner = requires(T& value) { value.possessedBy; };
static_assert(!ExposesPossessionStorage<MatchBall>);
static_assert(!ExposesLegacyOwner<MatchBall>);
static_assert(
    std::is_same_v<decltype(std::declval<MatchBall&>().possessionState()),
                   const MatchPossessionState&>);
static_assert(
    std::is_same_v<decltype(std::declval<MatchEngine&>().getPossessionState()),
                   const MatchPossessionState&>);
static_assert(std::is_same_v<
              decltype(std::declval<MatchEngine&>().getPossessionHistory()),
              const PossessionHistory&>);

// Compare every stored possession/history field, without padding or object
// addresses.
auto possessionStateKey(const MatchEngine& engine)
{
  const auto& s = engine.getPossessionState();
  return std::tuple{
      s.inKeeperHands,
      s.heldFrom.x,
      s.heldFrom.y,
      s.heldFrom.z,
      s.heldSince,
      s.heldRelease,
      s.possessedBy,
      s.inTouch,
      s.touchCollected,
      s.footControlled,
      s.footControlSeparationSeconds,
      s.claimUncontrolledSeconds,
      s.claimUnrecoverableSeconds,
      s.passOffsidePlayers,
      s.nudgedBy,
      s.nudgedFrom,
      s.touchPasser,
      s.touchDropping,
      s.lastPossessor,
      s.intendedReceiver,
      s.kicker,
      s.pathSince,
      s.deflectedBy,
      s.deflectedPathSince,
      s.deflectedAfter,
      s.isPass,
      s.passTarget.x,
      s.passTarget.y,
      s.passByHome,
      s.passWasOffside,
      s.isShot,
      s.shotByHome,
      s.shotOnTarget,
      s.shotXG,
      s.shotTargetY,
      s.shotTargetHeightMetres,
      s.shotElapsedSeconds,
      s.shotIsHeader,
      s.shotIsPenalty,
      s.shotFromSetPiece,
      s.shotSaveResolved,
      s.shotBlocked,
      s.shotFromDirectFreeKick,
      s.isAerialDelivery,
      s.fromThrowIn,
      s.throwEntering,
      s.throwByHome,
      s.shotOffWoodwork,
      s.woodworkPathSince,
      s.woodworkLastPossessor,
      s.woodworkDeflectedBy,
      s.history.lastControlledTeamHome,
      s.history.possessionStartSeconds,
      s.history.lastCompletedPasser,
      s.history.lastCompletedReceiver,
      s.history.lastShooter,
      s.history.shotAssistCandidate,
      s.history.headerRun,
      s.history.headerRunLastHome,
      s.history.pendingTackle.has_value(),
      s.history.pendingTackle ? s.history.pendingTackle->tackler : nullptr,
      s.history.pendingTackle ? s.history.pendingTackle->homeTeam : false,
      s.history.pendingTackle ? s.history.pendingTackle->contest : false};
}

auto historyKey(const PossessionHistory& h)
{
  return std::tuple{h.lastControlledTeamHome,
                    h.possessionStartSeconds,
                    h.lastCompletedPasser,
                    h.lastCompletedReceiver,
                    h.lastShooter,
                    h.shotAssistCandidate,
                    h.headerRun,
                    h.headerRunLastHome,
                    h.pendingTackle.has_value(),
                    h.pendingTackle ? h.pendingTackle->tackler : nullptr,
                    h.pendingTackle ? h.pendingTackle->homeTeam : false,
                    h.pendingTackle ? h.pendingTackle->contest : false};
}

class Routes final : public MatchRecorder
{
 public:
  bool wantsPossession() const override { return true; }
  void onPossessionObservation(const MatchEngine& engine,
                               PossessionObservation kind,
                               const PossessionSnapshot& state,
                               PossessionId executed) override
  {
    correct = correct && executed == engine.getPossessionVersion() &&
              state.implementation == executed;
    ++counts[static_cast<std::size_t>(kind)];
    const auto playerId = [](const Player* player) -> PlayerID
    { return player ? player->getId() : 0; };
    check(playerId(engine.getPossessionAttachment()) == state.legacyOwner &&
              playerId(engine.getTouchClaimant()) == state.claimant &&
              playerId(engine.getConfirmedController()) == state.controller &&
              playerId(engine.getRestartOwner()) ==
                  (state.openPlay ? 0 : state.legacyOwner) &&
              playerId(engine.getExpectedReceiver()) == state.expectedReceiver,
          "possession reader queries agree with value projection");
    check(engine.hasTouchClaim() == state.inTouch &&
              engine.isTouchCollected() == state.collected &&
              engine.isTouchDropping() == state.dropping &&
              engine.isKeeperHoldingBall() == state.inHands &&
              playerId(engine.getLastPossessionAttachment()) ==
                  state.legacyLastPossessor &&
              playerId(engine.getLegacyContactActor()) == state.legacyKicker &&
              playerId(engine.getPendingPasser()) == state.pendingPasser,
          "touch and history queries agree with independent value projection");
    const auto& ball = engine.getBall();
    const std::array<float, 8> motion{
        ball.position.x, ball.position.y, ball.z,         ball.velocity.x,
        ball.velocity.y, ball.velocityZ,  ball.spin.side, ball.spin.top};
    const float separation =
        engine.getPossessionState().footControlSeparationSeconds;
    const bool controlled = state.footControlled.value_or(state.collected);
    check(state.claimUncontrolledSeconds >= 0.0f &&
              state.claimUnrecoverableSeconds >= 0.0f,
          "claim clocks remain nonnegative");
    if (controlled || testedPossession == PossessionId::AARDVARK)
      check(state.claimUncontrolledSeconds == 0.0f &&
                state.claimUnrecoverableSeconds == 0.0f,
            "control and Aardvark do not age uncollected claims");
    check(separation >= 0.0f &&
              separation < PossessionControl::SEPARATION_GRACE_SECONDS,
          "control tolerance remains bounded");
    if (testedPossession == PossessionId::AARDVARK)
      check(separation == 0.0f,
            "Aardvark never uses Binturong control tolerance");
    if (!controlled)
      check(separation == 0.0f,
            "uncollected claim does not inherit control tolerance");
    if (kind == PossessionObservation::NOTE_CARRIER_HISTORY)
    {
      beforeTouch = state;
      beforeTouchMotion = motion;
      beforePassCredits = counts[static_cast<std::size_t>(
          PossessionObservation::CREDIT_PASS_COMPLETION)];
      for (const auto& player : engine.getPlayers())
        if (player.player && player.player->getId() == state.legacyOwner)
          beforeTouchCooldown = player.actionCooldown;
    }
    if (kind == PossessionObservation::UPDATE_TOUCH)
    {
      check(beforeTouch && beforeTouch->inTouch &&
                beforeTouch->legacyOwner == state.legacyOwner &&
                beforeTouchMotion == motion,
            "control update preserves claimant and physical motion");
      if (beforeTouch)
      {
        const bool beforeControl =
            beforeTouch->footControlled.value_or(beforeTouch->collected);
        if (!beforeControl && controlled) ++controlCollections;
        if (!beforeControl && controlled && beforeTouch->collected)
        {
          ++controlReacquisitions;
          check(counts[static_cast<std::size_t>(
                    PossessionObservation::CREDIT_PASS_COMPLETION)] ==
                    beforePassCredits,
                "regaining current control does not credit the completed "
                "receipt again");
          for (const auto& player : engine.getPlayers())
            if (player.player && player.player->getId() == state.legacyOwner)
              check(player.actionCooldown == beforeTouchCooldown,
                    "regaining control does not restart first-collection "
                    "decision delay");
        }
        if (beforeControl && !controlled)
        {
          ++controlLosses;
          check(
              state.controller == 0 &&
                  state.claimant == beforeTouch->claimant &&
                  state.collected == beforeTouch->collected,
              "loss of foot control preserves claimant and collection history");
        }
      }
      if (separation > 0.0f) ++controlGraceSamples;
      if (controlled &&
          state.ownerGapMetres > MatchTuning::Dribble::KICK_REACH_METRES)
        ++retainedBeyondCollectionReach;
      beforeTouch.reset();
      lastClaimUpdate = state;
      lastClaimHistory = historyKey(engine.getPossessionHistory());
    }
    if (kind == PossessionObservation::EXPIRE_TOUCH_CLAIM)
    {
      ++claimExpiries;
      check(testedPossession == PossessionId::BINTURONG && lastClaimUpdate &&
                lastClaimUpdate->claimant != 0 &&
                !lastClaimUpdate->footControlled.value_or(true) &&
                state.legacyOwner == 0 && state.claimant == 0 &&
                state.controller == 0,
            "only an uncontrolled Binturong claim expires into a free ball");
      check(motion == beforeTouchMotion && lastClaimUpdate &&
                state.pathSince == lastClaimUpdate->pathSince &&
                state.legacyLastPossessor ==
                    lastClaimUpdate->legacyLastPossessor &&
                state.legacyKicker == lastClaimUpdate->legacyKicker &&
                state.legacyDeflector == lastClaimUpdate->legacyDeflector &&
                lastClaimHistory &&
                *lastClaimHistory == historyKey(engine.getPossessionHistory()),
            "expiry preserves motion, path, rules provenance and "
            "continuity/credit history");
      check(lastClaimUpdate && state.meetFor != lastClaimUpdate->legacyOwner,
            "expired claimant has no remaining meet plan");
    }
    if (kind == PossessionObservation::RECORD_PASS_COMPLETION)
    {
      incoming = motion;
      incomingDelivery = ball.possessionState().isAerialDelivery;
      haveIncoming = true;
    }
    if (kind == PossessionObservation::CONSUME_FIRST_TIME_INTENT)
      preservesIncoming =
          preservesIncoming && haveIncoming && motion == incoming &&
          incomingDelivery == ball.possessionState().isAerialDelivery;
    if (kind == PossessionObservation::NOTE_CUSHIONED_HEADER)
      contactStates = contactStates && ball.possessionState().inTouch &&
                      !ball.possessionState().touchCollected &&
                      ball.possessionState().touchDropping &&
                      ball.possessionState().possessedBy;
    if (kind == PossessionObservation::NOTE_TACKLE_NUDGE)
      contactStates =
          contactStates && ball.possessionState().inTouch &&
          ball.possessionState().nudgedBy &&
          ball.possessionState().nudgedFrom &&
          ball.possessionState().nudgedBy == ball.possessionState().possessedBy;
    if (kind == PossessionObservation::BEGIN_POKED_TACKLE)
      contactStates = contactStates && !ball.possessionState().possessedBy &&
                      !ball.possessionState().inTouch &&
                      !ball.possessionState().isPass &&
                      !ball.possessionState().isShot;
    if (kind == PossessionObservation::NOTE_FALLEN_DEFLECTION)
      contactStates = contactStates && ball.possessionState().deflectedBy &&
                      ball.possessionState().deflectedPathSince ==
                          ball.possessionState().pathSince &&
                      ball.possessionState().deflectedAfter ==
                          ball.possessionState().lastPossessor;
    if (kind == PossessionObservation::BEGIN_BODY_DEFLECTION)
      contactStates = contactStates && !ball.possessionState().isPass &&
                      !ball.possessionState().isShot;
    if (kind == PossessionObservation::END_DEFLECTED_SHOT_FLIGHT)
      contactStates = contactStates && !ball.possessionState().isShot &&
                      !ball.possessionState().shotOnTarget;
    if (kind == PossessionObservation::BEGIN_KEEPER_HOLD)
      contactStates = contactStates && ball.possessionState().inKeeperHands &&
                      ball.possessionState().possessedBy &&
                      !ball.possessionState().inTouch;
    if (kind == PossessionObservation::END_KEEPER_HOLD)
      contactStates = contactStates && !ball.possessionState().inKeeperHands;
    if (kind == PossessionObservation::BEGIN_KEEPER_HAND_REBOUND)
      contactStates = contactStates && !ball.possessionState().isPass &&
                      !ball.possessionState().isShot &&
                      ball.possessionState().kicker;
  }
  bool correct = true;
  std::array<unsigned, OBSERVATION_COUNT> counts{};
  bool preservesIncoming = true;
  bool contactStates = true;

 private:
  std::optional<PossessionSnapshot> beforeTouch;
  std::optional<PossessionSnapshot> lastClaimUpdate;
  std::optional<decltype(historyKey(std::declval<const PossessionHistory&>()))>
      lastClaimHistory;
  std::array<float, 8> beforeTouchMotion{};
  float beforeTouchCooldown = 0.0f;
  unsigned beforePassCredits = 0;
  bool haveIncoming = false;
  bool incomingDelivery = false;
  std::array<float, 8> incoming{};
};

bool sameEvents(const MatchEngine& left, const MatchEngine& right)
{
  const auto& a = left.getEvents();
  const auto& b = right.getEvents();
  if (a.size() != b.size()) return false;
  const auto fields = [](const MatchEvent& e)
  {
    return std::tie(e.timeMinute, e.description, e.type, e.period,
                    e.addedMinute, e.hasTeam, e.isHomeTeam, e.primaryPlayerId,
                    e.secondaryPlayerId, e.position.x, e.position.y, e.xg,
                    e.detail, e.homeScore, e.awayScore, e.minutes,
                    e.homeShootout, e.awayShootout);
  };
  for (std::size_t i = 0; i < a.size(); ++i)
    if (fields(a[i]) != fields(b[i])) return false;
  return true;
}

void compareVersions(const Lineup& home, const Lineup& away,
                     const StatsConfig& config, BrainId homeBrain,
                     BrainId awayBrain, BodyId homeBody, BodyId awayBody,
                     bool shootout = false,
                     OutOfPlayId outOfPlay = OutOfPlayId::BINTURONG,
                     unsigned seed = 2)
{
  MatchEngine legacy(home, away, Strategy{}, Strategy{}, config, seed,
                     HandshakeId::BINTURONG);
  MatchEngine developing(home, away, Strategy{}, Strategy{}, config, seed,
                         HandshakeId::BINTURONG, PossessionId::BINTURONG);
  check(legacy.getPossessionVersion() == PossessionId::AARDVARK,
        "engine default remains legacy");
  check(developing.setPossessionVersion(PossessionId::BINTURONG),
        "select Binturong before kickoff");
  check(!developing.setPossessionVersion(static_cast<PossessionId>(255)),
        "reject invalid version");
  check(legacy.setPossessionVersion(testedPossession) &&
            developing.setPossessionVersion(testedPossession),
        "select the policy under test for both independent runs");
  for (MatchEngine* engine : {&legacy, &developing})
  {
    engine->setBrain(true, homeBrain);
    engine->setBrain(false, awayBrain);
    engine->setBody(true, homeBody);
    engine->setBody(false, awayBody);
    engine->setOutOfPlay(outOfPlay);
    engine->setBallPhysics(BallPhysicsId::BINTURONG);
  }
  Routes a, b;
  legacy.setRecorder(&a);
  developing.setRecorder(&b);
  if (shootout)
    for (auto* engine : {&legacy, &developing})
    {
      MatchRules::Knockout rules;
      rules.required = true;
      rules.extraTime = false;
      engine->setKnockout(rules);
      MatchScenario empty;
      for (const auto& player : engine->getPlayers())
        if (player.player && player.isGoalkeeper && player.isHomeTeam)
        {
          empty.carrierId = player.player->getId();
          empty.players.push_back({empty.carrierId, {0.08f, 0.5f}, false});
          break;
        }
      check(engine->applyScenario(empty, MatchState::PLAYING, 120.0f, 0, 0),
            "create a tied knockout at the final whistle");
    }
  const MatchEngine copied(developing);
  check(copied.getPossessionVersion() == testedPossession &&
            !copied.getRecorder(),
        "copy preserves implementation and detaches observer");
  bool equal = true;
  std::uint64_t ticks = 0;
  while (legacy.getState() != MatchState::FULL_TIME && equal &&
         ticks++ < 200000)
  {
    legacy.advance(0.1f);
    developing.advance(0.1f);
    if (ticks == 1)
      check(!developing.setPossessionVersion(otherPossession()),
            "reject version change during play after the first step");
    auto left = legacy.getPossessionSnapshot(),
         right = developing.getPossessionSnapshot();
    if (ticks % 5000 == 0)
    {
      const auto original = possessionStateKey(developing);
      std::optional<MatchEngine> restored;
      restored.emplace(developing);
      check(
          !restored->getRecorder() && possessionStateKey(*restored) == original,
          "complete possession/history survives detached engine copy");
      restored->advance(0.1f);
      check(possessionStateKey(developing) == original,
            "advancing an engine copy leaves original possession unchanged");
      restored.emplace(developing);
      check(
          !restored->getRecorder() && possessionStateKey(*restored) == original,
          "engine reconstruction restores complete possession/history");
    }
    right.implementation = left.implementation;
    equal = left == right &&
            possessionStateKey(legacy) == possessionStateKey(developing) &&
            legacy.getState() == developing.getState() &&
            legacy.getEvents().size() == developing.getEvents().size() &&
            SandboxRecorder::stateChecksum(legacy) ==
                SandboxRecorder::stateChecksum(developing);
  }
  check(equal && legacy.getState() == MatchState::FULL_TIME &&
            developing.getState() == MatchState::FULL_TIME,
        "same-policy runs reach full time with matching projections, checksums "
        "and event counts");
  check(a.correct && b.correct && a.counts == b.counts,
        "correct implementation executes every helper");
  check(sameEvents(legacy, developing),
        "match event order and attribution agree");
  if (shootout)
  {
    check(legacy.hasShootout() && developing.hasShootout() &&
              legacy.getShootoutScore(true) ==
                  developing.getShootoutScore(true) &&
              legacy.getShootoutScore(false) ==
                  developing.getShootoutScore(false) &&
              legacy.getStats().shootoutKicks ==
                  developing.getStats().shootoutKicks,
          "shootout reaches a decision with matching kicks, saves and scores");
  }
  {
    nlohmann::json record = nlohmann::json::array();
    for (const auto& e : legacy.getEvents())
      record.push_back(
          {e.timeMinute, e.description, static_cast<unsigned>(e.type), e.period,
           e.addedMinute, e.hasTeam, e.isHomeTeam, e.primaryPlayerId,
           e.secondaryPlayerId, e.position.x, e.position.y, e.xg,
           static_cast<unsigned>(e.detail), e.homeScore, e.awayScore, e.minutes,
           e.homeShootout, e.awayShootout});
    const auto& stats = legacy.getStats();
    record.push_back({stats.homeShots, stats.awayShots, stats.homeSaves,
                      stats.awaySaves, stats.homePassesCompleted,
                      stats.awayPassesCompleted, stats.shootoutKicks,
                      stats.shootoutGoals, stats.shootoutSaves});
    std::uint64_t digest = 14695981039346656037ULL;
    for (unsigned char c : record.dump())
      digest = (digest ^ c) * 1099511628211ULL;
    std::cout << (shootout ? "Shootout result " : "Match result ")
              << static_cast<unsigned>(homeBody) << '/'
              << static_cast<unsigned>(awayBody) << '/'
              << static_cast<unsigned>(outOfPlay) << " seed " << seed
              << " score "
              << (shootout ? legacy.getShootoutScore(true)
                           : legacy.getHomeScore())
              << '-'
              << (shootout ? legacy.getShootoutScore(false)
                           : legacy.getAwayScore())
              << " events/stats " << digest << " state "
              << SandboxRecorder::stateChecksum(legacy) << '\n';
  }
  check(a.contactStates && b.contactStates,
        "tackle releases/nudges and cushioned header claims are coherent");
  contestedHeaders += legacy.getStats().aerialDuelsContested;
  for (std::size_t i = 0; i < b.counts.size(); ++i)
    totalRoutes[i] += b.counts[i];
  check(!developing.setPossessionVersion(otherPossession()),
        "reject version change after the match");
  check(developing.setPossessionVersion(testedPossession),
        "same-version query remains harmless");
  legacy.setRecorder(nullptr);
  developing.setRecorder(nullptr);
  std::cout << "Compared independent runs for " << ticks << " steps\n";
}

void compareFirstTimeDrill(const StatsConfig& config, const char* name,
                           bool firstTime)
{
  auto make = [&]()
  {
    for (auto& drill : makeDrills())
      if (std::string_view(drill->name()) == name)
      {
        for (const auto& p : drill->parameters())
        {
          if (std::string_view(p.name) == "Play it")
            *p.value = firstTime ? 0.0f : 1.0f;
          if (std::string_view(p.name) == "Ball comes in")
            *p.value = firstTime ? 1.0f : 2.0f;
        }
        return std::move(drill);
      }
    return std::unique_ptr<Drill>{};
  };
  auto a = make(), b = make();
  check(a && b, "first-time drill exists");
  if (!a || !b) return;
  Routes ar, br;
  sandboxBrains() = {BrainId::BINTURONG, BrainId::BINTURONG};
  sandboxBodies() = {BodyId::BINTURONG, BodyId::BINTURONG};
  sandboxBallPhysics() = BallPhysicsId::BINTURONG;
  sandboxPossession() = testedPossession;
  DrillRun left(*a, config, false);
  sandboxPossession() = testedPossession;
  DrillRun right(*b, config, false);
  left.engine().setRecorder(&ar);
  right.engine().setRecorder(&br);
  unsigned steps = 0;
  bool equal = true;
  while (!left.finished() && !right.finished() && equal && ++steps < 2000)
  {
    (void)left.step();
    (void)right.step();
    auto ls = left.engine().getPossessionSnapshot();
    auto rs = right.engine().getPossessionSnapshot();
    rs.implementation = ls.implementation;
    const auto& lb = left.engine().getBall();
    const auto& rb = right.engine().getBall();
    equal = ls == rs && left.finished() == right.finished() &&
            SandboxRecorder::stateChecksum(left.engine()) ==
                SandboxRecorder::stateChecksum(right.engine()) &&
            lb.velocity.x == rb.velocity.x && lb.velocity.y == rb.velocity.y &&
            lb.velocityZ == rb.velocityZ && lb.spin == rb.spin;
  }
  check(equal && left.finished() && right.finished(),
        "first-time/controlled drill state and motion match every step");
  check(a->report() == b->report() && sameEvents(left.engine(), right.engine()),
        "first-time/controlled reports, event order and attribution match");
  check(ar.correct && br.correct && ar.counts == br.counts,
        "drill executes selected possession implementation");
  const auto count = [&](PossessionObservation op)
  { return br.counts[static_cast<std::size_t>(op)]; };
  check(count(PossessionObservation::CONSUME_FIRST_TIME_INTENT) ==
            (firstTime ? 1U : 0U),
        "first-time intent consumed once only for a first-time strike");
  check(ar.preservesIncoming && br.preservesIncoming,
        "first-time acquisition preserves incoming motion, spin and delivery");
  // The synthetic feeds name a receiver but have no last possessor. Legacy
  // collection defers credit through touchPasser, so those feeds cannot credit
  // a collected pass. Preserve that asymmetry; the Pass drill has a real
  // passer.
  const unsigned expectedCredit =
      firstTime || std::string_view(name) == "Pass" ? 1U : 0U;
  check(count(PossessionObservation::CREDIT_PASS_COMPLETION) == expectedCredit,
        "pass credit preserves real-passer and synthetic-feed semantics");
  for (std::size_t i = 0; i < br.counts.size(); ++i)
    totalRoutes[i] += br.counts[i];
  std::cout << "Compared " << name
            << (firstTime ? " first time" : " after a touch") << " for "
            << steps << " steps\n";
}

class KeeperClaimFixture final : public Drill
{
 public:
  KeeperClaimFixture(unsigned runSeed, float rating, float gap, bool legacy)
      : rating(rating), gap(gap), legacy(legacy)
  {
    seed = runSeed;
  }
  const char* name() const override { return "Keeper claim fixture"; }
  const char* summary() const override
  {
    return "A crowded high ball in the keeper's area";
  }
  std::vector<DrillParameter> parameters() override { return {}; }
  std::vector<DrillPlayerSpec> players() const override
  {
    DrillPlayerSpec keeper{
        "Keeper", PlayerRole::GK, true, {{"Goalkeeping", rating}}, 195};
    keeper.derived[DerivedStat::HANDLING] = rating;
    keeper.derived[DerivedStat::COMMAND_OF_AREA] = 1.0f;
    keeper.derived[DerivedStat::AGILITY] = 99.0f;
    keeper.derived[DerivedStat::REFLEXES] = 99.0f;
    return {keeper, {"Opponent", PlayerRole::ST, false}};
  }
  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    keeper = ids[0];
    opponent = ids[1];
    MatchScenario scenario;
    scenario.players.push_back({keeper, {0.08f, 0.5f}, false});
    scenario.players.push_back(
        {opponent,
         {0.08f, 0.5f + gap / MatchTuning::Pitch::WIDTH_METRES},
         false});
    scenario.carrierId = 0;
    scenario.ballPosition = {0.20f, 0.5f};
    (void)engine.startDrill(scenario);
    engine.setDrillTarget(keeper, Vector2F{0.08f, 0.5f}, false);
    engine.setDrillTarget(
        opponent,
        Vector2F{0.08f, 0.5f + gap / MatchTuning::Pitch::WIDTH_METRES}, false);
    if (!legacy && rating > 90.0f)
      engine.feedBall({0.08f + 1.8f / MatchTuning::Pitch::LENGTH_METRES, 0.5f},
                      {0.0f, 0.0f}, 0.0f, 0);
    else if (legacy)
      engine.feedBall({0.08f + 6.0f / MatchTuning::Pitch::LENGTH_METRES, 0.5f},
                      {-8.0f, 0.0f}, 6.4f, 0);
    else
      engine.feedBall({0.20f, 0.5f}, {-12.0f, 0.0f}, 7.0f, 0);
  }
  bool afterStep(MatchEngine& engine) override
  {
    const auto& s = engine.getStats();
    result = {std::to_string(s.homeKeeperClaims),
              std::to_string(s.keeperPunches)};
    return engine.getSimulatedSteps() < 80;
  }
  std::vector<std::string> report() const override { return result; }
  std::vector<DrillMetric> metrics() const override { return {}; }

 private:
  float rating, gap;
  bool legacy;
  PlayerID keeper = 0, opponent = 0;
  std::vector<std::string> result;
};

void compareContactDrill(const StatsConfig& config, const char* name,
                         const std::map<std::string, float>& settings,
                         BodyId homeBody, BodyId awayBody, unsigned seed)
{
  const auto id = [](const Player* player)
  { return player ? player->getId() : 0; };
  auto make = [&]() -> std::unique_ptr<Drill>
  {
    if (std::string_view(name) == "Keeper claim fixture")
      return std::make_unique<KeeperClaimFixture>(
          seed, settings.at("Goalkeeping"), settings.at("Crowding (m)"),
          homeBody == BodyId::AARDVARK);
    for (auto& drill : makeDrills())
      if (std::string_view(drill->name()) == name)
      {
        drill->seed = seed;
        for (const auto& [key, value] : settings)
        {
          bool found = false;
          for (const auto& p : drill->parameters())
            if (key == p.name)
            {
              *p.value = value;
              found = true;
            }
          check(found, "contact fixture setting exists");
        }
        return std::move(drill);
      }
    return std::unique_ptr<Drill>{};
  };
  auto a = make(), b = make();
  check(a && b, "contact drill exists");
  if (!a || !b) return;
  Routes ar, br;
  sandboxBrains() = {BrainId::BINTURONG, BrainId::BINTURONG};
  sandboxBodies() = {homeBody, awayBody};
  sandboxBallPhysics() = BallPhysicsId::BINTURONG;
  const auto prepare = [&](MatchEngine& engine)
  {
    if (std::string_view(name) == "Shot target" &&
        settings.contains("Goalkeeping") && settings.at("Goalkeeping") == 99.0f)
      for (const auto& player : engine.getPlayers())
        if (player.isGoalkeeper && player.player)
        {
          engine.setDerivedStat(player.player->getId(), DerivedStat::HANDLING,
                                1.0f);
          engine.setDerivedStat(player.player->getId(), DerivedStat::REFLEXES,
                                1.0f);
          engine.setDerivedStat(player.player->getId(), DerivedStat::AGILITY,
                                1.0f);
        }
  };
  sandboxPossession() = testedPossession;
  DrillRun left(*a, config, false, prepare);
  sandboxPossession() = testedPossession;
  DrillRun right(*b, config, false, prepare);
  left.engine().setRecorder(&ar);
  right.engine().setRecorder(&br);
  unsigned steps = 0;
  bool equal = true;
  while (!left.finished() && !right.finished() && equal && ++steps < 2000)
  {
    (void)left.step();
    (void)right.step();
    auto ls = left.engine().getPossessionSnapshot();
    auto rs = right.engine().getPossessionSnapshot();
    rs.implementation = ls.implementation;
    const auto& lb = left.engine().getBall();
    const auto& rb = right.engine().getBall();
    const auto& lst = left.engine().getStats();
    const auto& rst = right.engine().getStats();
    equal =
        ls == rs && left.finished() == right.finished() &&
        SandboxRecorder::stateChecksum(left.engine()) ==
            SandboxRecorder::stateChecksum(right.engine()) &&
        lb.velocity.x == rb.velocity.x && lb.velocity.y == rb.velocity.y &&
        lb.velocityZ == rb.velocityZ && lb.spin == rb.spin &&
        id(lb.possessionState().nudgedBy) ==
            id(rb.possessionState().nudgedBy) &&
        id(lb.possessionState().nudgedFrom) ==
            id(rb.possessionState().nudgedFrom) &&
        id(lb.possessionState().deflectedBy) ==
            id(rb.possessionState().deflectedBy) &&
        id(lb.possessionState().deflectedAfter) ==
            id(rb.possessionState().deflectedAfter) &&
        lb.possessionState().deflectedPathSince ==
            rb.possessionState().deflectedPathSince &&
        lb.possessionState().isShot == rb.possessionState().isShot &&
        lb.possessionState().shotOnTarget ==
            rb.possessionState().shotOnTarget &&
        lb.possessionState().shotBlocked == rb.possessionState().shotBlocked &&
        lb.touchAttempts == rb.touchAttempts &&
        lb.possessionState().inKeeperHands ==
            rb.possessionState().inKeeperHands &&
        lb.possessionState().heldRelease == rb.possessionState().heldRelease &&
        lb.possessionState().heldSince == rb.possessionState().heldSince &&
        lb.possessionState().heldFrom.x == rb.possessionState().heldFrom.x &&
        lb.possessionState().heldFrom.y == rb.possessionState().heldFrom.y &&
        lb.possessionState().heldFrom.z == rb.possessionState().heldFrom.z &&
        lst.homeTackles == rst.homeTackles &&
        lst.awayTackles == rst.awayTackles &&
        lst.homePassesCompleted == rst.homePassesCompleted &&
        lst.awayPassesCompleted == rst.awayPassesCompleted &&
        lst.passesBlocked == rst.passesBlocked &&
        lst.shotDeflections == rst.shotDeflections &&
        lst.collisionFallsLosingBall == rst.collisionFallsLosingBall &&
        lst.collisionBallOffFallen == rst.collisionBallOffFallen &&
        lst.homeSaves == rst.homeSaves && lst.awaySaves == rst.awaySaves &&
        lst.homeOnTarget == rst.homeOnTarget &&
        lst.awayOnTarget == rst.awayOnTarget &&
        lst.keeperRolls == rst.keeperRolls &&
        lst.keeperThrows == rst.keeperThrows &&
        lst.keeperPunts == rst.keeperPunts &&
        lst.homePossession == rst.homePossession &&
        lst.awayPossession == rst.awayPossession;
  }
  check(equal && left.finished() && right.finished(),
        "contact fixture ownership, attribution, stats and motion match every "
        "step");
  check(a->report() == b->report() && sameEvents(left.engine(), right.engine()),
        "contact fixture reports and event order/attribution match");
  check(ar.correct && br.correct && ar.counts == br.counts &&
            ar.contactStates && br.contactStates,
        "contact fixture dispatch and intermediate claim/release states agree");
  // Catalogue shot drills stop on a catch. Continue the engines through the
  // remaining dive and gather so the held-ball path is exercised as well.
  if (std::string_view(name) == "Shot target" &&
      left.engine().getBall().possessionState().inKeeperHands)
    for (unsigned i = 0; i < 20; ++i)
    {
      left.engine().advance(0.1f);
      right.engine().advance(0.1f);
      auto ls = left.engine().getPossessionSnapshot();
      auto rs = right.engine().getPossessionSnapshot();
      rs.implementation = ls.implementation;
      check(ls == rs && SandboxRecorder::stateChecksum(left.engine()) ==
                            SandboxRecorder::stateChecksum(right.engine()),
            "caught ball stays deterministic during remaining dive and gather");
    }
  check(ar.correct && br.correct && ar.counts == br.counts,
        "continued held-ball steps execute the selected implementation");
  for (std::size_t i = 0; i < br.counts.size(); ++i)
    totalRoutes[i] += br.counts[i];
  std::cout << "Compared " << name << " seed " << seed << " bodies "
            << static_cast<unsigned>(homeBody) << '/'
            << static_cast<unsigned>(awayBody) << " for " << steps
            << " steps\n";
}

void participantLifecycle(const Lineup& home, const Lineup& away,
                          const Player* incoming, const StatsConfig& config,
                          BodyId body)
{
  auto withBench = home;
  withBench.setReserves({incoming});
  MatchEngine a(withBench, away, Strategy{}, Strategy{}, config, 11,
                HandshakeId::BINTURONG, testedPossession);
  MatchEngine b(withBench, away, Strategy{}, Strategy{}, config, 11,
                HandshakeId::BINTURONG, testedPossession);
  Routes ar, br;
  a.setRecorder(&ar);
  b.setRecorder(&br);
  for (auto* engine : {&a, &b})
  {
    engine->setBody(true, body);
    engine->setBody(false, body);
    engine->setOutOfPlay(
        OutOfPlayId::AARDVARK);  // Immediate substitution path.
    const auto* outgoing = home.getOutfieldPlayers().front().player;
    MatchScenario s;
    s.players.push_back({outgoing->getId(), {0.3f, 0.05f}, false});
    s.carrierId = outgoing->getId();
    s.ballPosition = {0.3f, 0.05f};
    check(engine->startDrill(s), "set up lifecycle owner");
    check(engine->substitutePlayer(outgoing->getId(), incoming),
          "substitute a current owner");
    check(
        engine->getBall().possessionState().possessedBy == incoming &&
            engine->getBall().possessionState().lastPossessor == incoming,
        "substitution preserves legacy incoming ownership and history rewrite");
    const auto copied = *engine;
    check(
        !copied.getRecorder() &&
            copied.getPossessionVersion() == engine->getPossessionVersion() &&
            copied.getPossessionSnapshot() == engine->getPossessionSnapshot() &&
            possessionStateKey(copied) == possessionStateKey(*engine),
        "copy retains lifecycle state/version and detaches recorder");
    check(engine->removeBeforeKickOff(incoming->getId()),
          "remove a scenario owner");
    check(!engine->getBall().possessionState().possessedBy,
          "removing owner releases claim");
    check(engine->removeBeforeKickOff(
              away.getOutfieldPlayers().front().player->getId()),
          "remove a non-owner without transferring control");
    engine->feedBall({0.4f, 0.1f}, {3.0f, 0.0f}, 0.0f,
                     away.getOutfieldPlayers()[1].player->getId());
    check(!engine->getBall().possessionState().possessedBy &&
              !engine->getBall().possessionState().lastPossessor &&
              engine->getBall().possessionState().isPass,
          "feed creates expectation without control or last player");
  }
  auto ls = a.getPossessionSnapshot(), rs = b.getPossessionSnapshot();
  rs.implementation = ls.implementation;
  check(ls == rs && sameEvents(a, b) && ar.correct && br.correct &&
            ar.counts == br.counts,
        "participant lifecycle and feed agree between possession versions");
  for (std::size_t i = 0; i < br.counts.size(); ++i)
    totalRoutes[i] += br.counts[i];
}

void persistence()
{
  check(sandboxPossession() == PossessionId::BINTURONG,
        "new sandbox default is Binturong");
  for (PossessionId version : ALL_POSSESSIONS)
  {
    MatchSetup setup;
    setup.possession = version;
    auto json = SandboxJson::toJson(setup);
    MatchSetup loaded;
    std::string error;
    check(SandboxJson::fromJson(json, loaded, error) &&
              loaded.possession == version,
          "setup round-trip preserves selected possession");
    json.erase("possession");
    loaded.possession = PossessionId::BINTURONG;
    check(SandboxJson::fromJson(json, loaded, error) &&
              loaded.possession == PossessionId::AARDVARK,
          "missing old field defaults to Aardvark despite new sandbox default");
    json["possession"] = "invalid";
    check(!SandboxJson::fromJson(json, loaded, error),
          "unknown setup version rejected");
    json["possession"] = 12;
    check(!SandboxJson::fromJson(json, loaded, error),
          "wrong-type setup version rejected");
  }
  const auto stamp =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto file = std::filesystem::temp_directory_path() /
                    ("possession-seam-" + std::to_string(stamp) + ".json");
  MatchRecording recording;
  recording.setup.possession = PossessionId::BINTURONG;
  std::string error;
  check(Recordings::save(recording, file, error), "save versioned recording");
  auto loaded = Recordings::load(file, error);
  check(loaded && loaded->setup.possession == PossessionId::BINTURONG,
        "load recorded selection");
  nlohmann::json json;
  {
    std::ifstream in(file);
    in >> json;
  }
  json["version"] = 1;
  json["setup"].erase("possession");
  {
    std::ofstream out(file);
    out << json;
  }
  loaded = Recordings::load(file, error);
  check(loaded && loaded->setup.possession == PossessionId::AARDVARK,
        "old recording uses legacy possession");
  json["version"] = MatchRecording::VERSION;
  {
    std::ofstream out(file);
    out << json;
  }
  check(!Recordings::load(file, error),
        "new recording missing required selection rejected");
  json["setup"]["possession"] = "binturong";
  json["version"] = MatchRecording::VERSION + 1;
  {
    std::ofstream out(file);
    out << json;
  }
  check(!Recordings::load(file, error), "future recording version rejected");
  std::filesystem::remove(file);
}
}  // namespace

int main(int argc, char** argv)
{
  if (argc == 3 && std::string_view(argv[1]) == "--possession")
  {
    const auto selected = possessionFromKey(argv[2]);
    if (!selected) return 2;
    testedPossession = *selected;
  }
  else if (argc != 1)
    return 2;
  persistence();
  auto config = Lab::loadStatsConfig();
  std::vector<std::unique_ptr<Player>> pool;
  const auto home = Lab::buildLabLineup(1, 65, pool);
  const auto away = Lab::buildLabLineup(2, 65, pool);
  const auto reserve = Lab::buildLabLineup(3, 65, pool);
  for (auto body : {BodyId::AARDVARK, BodyId::BINTURONG})
    participantLifecycle(
        home, away, reserve.getOutfieldPlayers().front().player, config, body);
  compareVersions(home, away, config, BrainId::AARDVARK, BrainId::AARDVARK,
                  BodyId::AARDVARK, BodyId::AARDVARK);
  compareVersions(home, away, config, BrainId::BINTURONG, BrainId::BINTURONG,
                  BodyId::BINTURONG, BodyId::BINTURONG);
  compareVersions(home, away, config, BrainId::BINTURONG, BrainId::AARDVARK,
                  BodyId::BINTURONG, BodyId::AARDVARK);
  compareVersions(home, away, config, BrainId::AARDVARK, BrainId::BINTURONG,
                  BodyId::AARDVARK, BodyId::BINTURONG);
  for (unsigned seed = 1; seed <= 8; ++seed)
    compareVersions(home, away, config, BrainId::AARDVARK, BrainId::AARDVARK,
                    BodyId::AARDVARK, BodyId::AARDVARK, false,
                    OutOfPlayId::AARDVARK, seed);
  for (auto out : {OutOfPlayId::AARDVARK, OutOfPlayId::BINTURONG})
    for (auto homeBody : {BodyId::AARDVARK, BodyId::BINTURONG})
      for (auto awayBody : {BodyId::AARDVARK, BodyId::BINTURONG})
        compareVersions(home, away, config, BrainId::BINTURONG,
                        BrainId::BINTURONG, homeBody, awayBody, true, out);
  compareFirstTimeDrill(config, "First-time pass", true);
  compareFirstTimeDrill(config, "First-time pass", false);
  compareFirstTimeDrill(config, "Shot target", true);
  compareFirstTimeDrill(config, "Shot target", false);
  compareFirstTimeDrill(config, "Pass", false);
  for (const auto homeBody : {BodyId::AARDVARK, BodyId::BINTURONG})
    for (const auto awayBody : {BodyId::AARDVARK, BodyId::BINTURONG})
      for (unsigned seed = 1; seed <= 3; ++seed)
      {
        for (unsigned intent = 0; intent < 5; ++intent)
          compareContactDrill(
              config, "Tackle",
              {{"Defender's intent", static_cast<float>(intent)},
               {"Attacker carries at", 3.0f},
               {"Dribbling", 20.0f}},
              homeBody, awayBody, seed);
        compareContactDrill(config, "Tackle",
                            {{"Defender's intent", 0.0f},
                             {"Attacker carries at", 1.0f},
                             {"Dribbling", 85.0f}},
                            homeBody, awayBody, seed);
        for (unsigned aim = 0; aim < 3; ++aim)
          compareContactDrill(config, "Header target",
                              {{"Header", static_cast<float>(aim)}}, homeBody,
                              awayBody, seed);
        for (unsigned kind = 0; kind < 2; ++kind)
          for (unsigned moving = 0; moving < 2; ++moving)
            compareContactDrill(config, "Shot block",
                                {{"Ball", static_cast<float>(kind)},
                                 {"Moving", static_cast<float>(moving * 2)},
                                 {"Aim height (m)", kind == 0 ? 1.0f : 0.0f}},
                                homeBody, awayBody, seed);
        compareContactDrill(config, "Collision",
                            {{"Run-up (m)", 30.0f},
                             {"B run-up (m)", 30.0f},
                             {"B's run against A's (deg)", 180.0f},
                             {"Ball in after (s)", 2.0f},
                             {"Ball speed (m/s)", 12.0f},
                             {"Ball from (deg)", 90.0f},
                             {"Ball lift (m/s)", 0.0f}},
                            homeBody, awayBody, seed);
        for (unsigned release = 0; release < 3; ++release)
          compareContactDrill(config, "Keeper throw",
                              {{"Range (m)", release == 0   ? 20.0f
                                             : release == 1 ? 40.0f
                                                            : 55.0f},
                               {"Release", release == 2 ? 1.0f : 0.0f}},
                              homeBody, awayBody, seed);
        compareContactDrill(config, "Shot target",
                            {{"Keeper", 1.0f}, {"Goalkeeping", 35.0f}},
                            homeBody, awayBody, seed);
        compareContactDrill(config, "Shot target",
                            {{"Keeper", 1.0f}, {"Ball comes in", 1.0f}},
                            homeBody, awayBody, seed);
        compareContactDrill(config, "Shot target", {{"Power share", 0.01f}},
                            homeBody, awayBody, seed);
      }
  check(contestedHeaders > 0,
        "full-match fixtures exercise contested aerial contacts");
  for (unsigned seed = 1; seed <= 6; ++seed)
  {
    for (auto body : {BodyId::AARDVARK, BodyId::BINTURONG})
      compareContactDrill(config, "Keeper claim fixture",
                          {{"Goalkeeping", 1.0f}, {"Crowding (m)", 2.5f}}, body,
                          body, seed);
    compareContactDrill(config, "Keeper claim fixture",
                        {{"Goalkeeping", 99.0f}, {"Crowding (m)", 1.5f}},
                        BodyId::BINTURONG, BodyId::BINTURONG, seed);
    for (float aim : {0.8f, 1.6f, 2.4f})
      compareContactDrill(config, "Shot target",
                          {{"Keeper", 1.0f},
                           {"Goalkeeping", 99.0f},
                           {"Aim across (m)", aim},
                           {"Power share", 0.4f}},
                          BodyId::BINTURONG, BodyId::BINTURONG, seed);
  }
  // A changed live policy can stop the old match seeds exercising a rare
  // legal branch. Keep explicit deterministic coverage instead of dropping it.
  if (testedPossession == PossessionId::BINTURONG)
  {
    const auto missingRare = [&]
    {
      for (auto op : {PossessionObservation::NOTE_DIRECT_FREE_KICK_SHOT,
                      PossessionObservation::MARK_WALL_STOPPED_SHOT,
                      PossessionObservation::CREDIT_WALL_BLOCKER})
        if (totalRoutes[static_cast<std::size_t>(op)] == 0) return true;
      return false;
    };
    // Bounded deterministic coverage search; all compared runs must still
    // match. Emitted result lines identify every seed used for reproduction.
    for (unsigned seed = 9; seed <= 32 && missingRare(); ++seed)
    {
      if (totalRoutes[static_cast<std::size_t>(PossessionObservation::MARK_WALL_STOPPED_SHOT)] == 0 ||
          totalRoutes[static_cast<std::size_t>(PossessionObservation::CREDIT_WALL_BLOCKER)] == 0)
        compareVersions(home, away, config, BrainId::AARDVARK, BrainId::AARDVARK,
                        BodyId::AARDVARK, BodyId::AARDVARK, false,
                        OutOfPlayId::AARDVARK, seed);
      if (totalRoutes[static_cast<std::size_t>(PossessionObservation::NOTE_DIRECT_FREE_KICK_SHOT)] == 0)
        compareVersions(home, away, config, BrainId::AARDVARK, BrainId::AARDVARK,
                        BodyId::BINTURONG, BodyId::BINTURONG, false,
                        OutOfPlayId::AARDVARK, seed);
    }
  }
  for (std::size_t i = 0; i < totalRoutes.size(); ++i)
  {
    if (i == static_cast<std::size_t>(
                 PossessionObservation::EXPIRE_TOUCH_CLAIM) &&
        testedPossession == PossessionId::AARDVARK)
    {
      check(totalRoutes[i] == 0,
            "Aardvark never expires touch claims through the new policy");
      continue;
    }
    if (totalRoutes[i] == 0)
      std::cerr << "Unexercised observation: " << i << '\n';
    check(totalRoutes[i] > 0,
          "all observable possession operations exercised across fixtures");
  }
  if (testedPossession == PossessionId::BINTURONG)
    check(controlCollections > 0 && controlReacquisitions > 0 &&
              controlLosses > 0 && controlGraceSamples > 0 &&
              retainedBeyondCollectionReach > 0,
          "integration fixtures exercise acquisition, loss, grace and retained "
          "dribbles");
  std::cout << "Control observations: collections " << controlCollections
            << ", reacquisitions " << controlReacquisitions << ", losses "
            << controlLosses << ", grace " << controlGraceSamples
            << ", retained beyond collection reach "
            << retainedBeyondCollectionReach << '\n';
  std::cout << "Claim expiry observations: " << claimExpiries << '\n';
  if (failures == 0)
    std::cout << "PASS: possession version dispatch, compatibility and copy "
                 "contracts\n";
  return failures == 0 ? 0 : 1;
}
