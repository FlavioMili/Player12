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
#include <vector>

#include "model/match_engine.h"
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
constexpr auto OBSERVATION_COUNT =
    static_cast<std::size_t>(PossessionObservation::COUNT);
std::array<unsigned, OBSERVATION_COUNT> totalRoutes{};
unsigned contestedHeaders = 0;
void check(bool value, const char* message)
{
  if (!value)
  {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
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
    const auto& ball = engine.getBall();
    const std::array<float, 8> motion{
        ball.position.x, ball.position.y, ball.z,         ball.velocity.x,
        ball.velocity.y, ball.velocityZ,  ball.spin.side, ball.spin.top};
    if (kind == PossessionObservation::RECORD_PASS_COMPLETION)
    {
      incoming = motion;
      incomingDelivery = ball.isAerialDelivery;
      haveIncoming = true;
    }
    if (kind == PossessionObservation::CONSUME_FIRST_TIME_INTENT)
      preservesIncoming = preservesIncoming && haveIncoming &&
                          motion == incoming &&
                          incomingDelivery == ball.isAerialDelivery;
    if (kind == PossessionObservation::NOTE_CUSHIONED_HEADER)
      contactStates = contactStates && ball.inTouch && !ball.touchCollected &&
                      ball.touchDropping && ball.possessedBy;
    if (kind == PossessionObservation::NOTE_TACKLE_NUDGE)
      contactStates = contactStates && ball.inTouch && ball.nudgedBy &&
                      ball.nudgedFrom && ball.nudgedBy == ball.possessedBy;
    if (kind == PossessionObservation::BEGIN_POKED_TACKLE)
      contactStates = contactStates && !ball.possessedBy && !ball.inTouch &&
                      !ball.isPass && !ball.isShot;
    if (kind == PossessionObservation::NOTE_FALLEN_DEFLECTION)
      contactStates = contactStates && ball.deflectedBy &&
                      ball.deflectedPathSince == ball.pathSince &&
                      ball.deflectedAfter == ball.lastPossessor;
    if (kind == PossessionObservation::BEGIN_BODY_DEFLECTION)
      contactStates = contactStates && !ball.isPass && !ball.isShot;
    if (kind == PossessionObservation::END_DEFLECTED_SHOT_FLIGHT)
      contactStates = contactStates && !ball.isShot && !ball.shotOnTarget;
    if (kind == PossessionObservation::BEGIN_KEEPER_HOLD)
      contactStates = contactStates && ball.inKeeperHands && ball.possessedBy &&
                      !ball.inTouch;
    if (kind == PossessionObservation::END_KEEPER_HOLD)
      contactStates = contactStates && !ball.inKeeperHands;
    if (kind == PossessionObservation::BEGIN_KEEPER_HAND_REBOUND)
      contactStates =
          contactStates && !ball.isPass && !ball.isShot && ball.kicker;
  }
  bool correct = true;
  std::array<unsigned, OBSERVATION_COUNT> counts{};
  bool preservesIncoming = true;
  bool contactStates = true;

 private:
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
  check(copied.getPossessionVersion() == PossessionId::BINTURONG &&
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
      check(!developing.setPossessionVersion(PossessionId::AARDVARK),
            "reject version change during play after the first step");
    auto left = legacy.getPossessionSnapshot(),
         right = developing.getPossessionSnapshot();
    right.implementation = left.implementation;
    equal = left == right && legacy.getState() == developing.getState() &&
            legacy.getEvents().size() == developing.getEvents().size() &&
            SandboxRecorder::stateChecksum(legacy) ==
                SandboxRecorder::stateChecksum(developing);
  }
  check(equal && legacy.getState() == MatchState::FULL_TIME &&
            developing.getState() == MatchState::FULL_TIME,
        "both versions reach full time with matching projections, checksums "
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
  check(!developing.setPossessionVersion(PossessionId::AARDVARK),
        "reject version change after the match");
  check(developing.setPossessionVersion(PossessionId::BINTURONG),
        "same-version query remains harmless");
  legacy.setRecorder(nullptr);
  developing.setRecorder(nullptr);
  std::cout << "Compared versions for " << ticks << " steps\n";
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
  sandboxPossession() = PossessionId::AARDVARK;
  DrillRun left(*a, config, false);
  sandboxPossession() = PossessionId::BINTURONG;
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
  sandboxPossession() = PossessionId::AARDVARK;
  DrillRun left(*a, config, false, prepare);
  sandboxPossession() = PossessionId::BINTURONG;
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
    equal = ls == rs && left.finished() == right.finished() &&
            SandboxRecorder::stateChecksum(left.engine()) ==
                SandboxRecorder::stateChecksum(right.engine()) &&
            lb.velocity.x == rb.velocity.x && lb.velocity.y == rb.velocity.y &&
            lb.velocityZ == rb.velocityZ && lb.spin == rb.spin &&
            id(lb.nudgedBy) == id(rb.nudgedBy) &&
            id(lb.nudgedFrom) == id(rb.nudgedFrom) &&
            id(lb.deflectedBy) == id(rb.deflectedBy) &&
            id(lb.deflectedAfter) == id(rb.deflectedAfter) &&
            lb.deflectedPathSince == rb.deflectedPathSince &&
            lb.isShot == rb.isShot && lb.shotOnTarget == rb.shotOnTarget &&
            lb.shotBlocked == rb.shotBlocked &&
            lb.touchAttempts == rb.touchAttempts &&
            lb.inKeeperHands == rb.inKeeperHands &&
            lb.heldRelease == rb.heldRelease && lb.heldSince == rb.heldSince &&
            lb.heldFrom.x == rb.heldFrom.x && lb.heldFrom.y == rb.heldFrom.y &&
            lb.heldFrom.z == rb.heldFrom.z &&
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
      left.engine().getBall().inKeeperHands)
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
                HandshakeId::BINTURONG, PossessionId::AARDVARK);
  MatchEngine b(withBench, away, Strategy{}, Strategy{}, config, 11,
                HandshakeId::BINTURONG, PossessionId::BINTURONG);
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
        engine->getBall().possessedBy == incoming &&
            engine->getBall().lastPossessor == incoming,
        "substitution preserves legacy incoming ownership and history rewrite");
    const auto copied = *engine;
    check(!copied.getRecorder() &&
              copied.getPossessionVersion() == engine->getPossessionVersion() &&
              copied.getPossessionSnapshot() == engine->getPossessionSnapshot(),
          "copy retains lifecycle state/version and detaches recorder");
    check(engine->removeBeforeKickOff(incoming->getId()),
          "remove a scenario owner");
    check(!engine->getBall().possessedBy, "removing owner releases claim");
    check(engine->removeBeforeKickOff(
              away.getOutfieldPlayers().front().player->getId()),
          "remove a non-owner without transferring control");
    engine->feedBall({0.4f, 0.1f}, {3.0f, 0.0f}, 0.0f,
                     away.getOutfieldPlayers()[1].player->getId());
    check(!engine->getBall().possessedBy && !engine->getBall().lastPossessor &&
              engine->getBall().isPass,
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
  const bool legacyProbe =
      argc == 2 && std::string_view(argv[1]) == "--legacy-probe";
  const bool lifecycleProbe =
      argc == 2 && std::string_view(argv[1]) == "--pre-lifecycle-core";
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
  for (std::size_t i = 0; i < totalRoutes.size(); ++i)
  {
    if (legacyProbe && i >= 51)
      continue;  // The saved pre-restart core has 51 labels.
    if (lifecycleProbe && i >= 60) continue;
    if (totalRoutes[i] == 0)
      std::cerr << "Unexercised observation: " << i << '\n';
    check(totalRoutes[i] > 0,
          "all observable possession operations exercised across fixtures");
  }
  if (failures == 0)
    std::cout << "PASS: possession version dispatch, compatibility and copy "
                 "contracts\n";
  return failures == 0 ? 0 : 1;
}
