// -----------------------------------------------------------------------------
// Football Management Project
// Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
// See the LICENSE file in the project root.
// -----------------------------------------------------------------------------
#include "tools/match_sandbox_possession_trace.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <ostream>
#include <string>

#include "model/match_engine.h"
#include "model/match_recorder.h"
#include "tools/match_sandbox_names.h"
#include "tools/match_sandbox_recorder.h"

namespace
{
using Json = nlohmann::json;

Json snapshot(const PossessionSnapshot& state)
{
  return {{"implementation", possessionKey(state.implementation)},
          {"mode", possessionModeName(state.mode)},
          {"tick", state.tick},
          {"seconds", state.seconds},
          {"open_play", state.openPlay},
          {"owner", state.legacyOwner},
          {"controller", state.controller},
          {"claimant", state.claimant},
          {"receiver", state.expectedReceiver},
          {"pending_passer", state.pendingPasser},
          {"legacy_last_possessor", state.legacyLastPossessor},
          {"legacy_kicker", state.legacyKicker},
          {"legacy_deflector", state.legacyDeflector},
          {"last_physical_touch", nullptr},
          {"last_deliberate_play", nullptr},
          {"last_controlled_home", state.lastControlledTeamHome
                                       ? Json(*state.lastControlledTeamHome)
                                       : Json(nullptr)},
          {"in_touch", state.inTouch},
          {"collected", state.collected},
          {"dropping", state.dropping},
          {"hands", state.inHands},
          {"pass", state.pass},
          {"shot", state.shot},
          {"meet_for", state.meetFor},
          {"path_since", state.pathSince},
          {"owner_gap_m", state.ownerGapMetres}};
}

Json playerState(const MatchEngine& engine, const MatchPlayer& player)
{
  const MatchBall& ball = engine.getBall();
  const float dx =
      (player.position.x - ball.position.x) * MatchTuning::Pitch::LENGTH_METRES;
  const float dy =
      (player.position.y - ball.position.y) * MatchTuning::Pitch::WIDTH_METRES;
  return {{"id", player.player ? player.player->getId() : 0},
          {"home", player.isHomeTeam},
          {"position", {player.position.x, player.position.y}},
          {"velocity_mps", {player.velocity.x, player.velocity.y}},
          {"facing_rad", player.facingAngle},
          {"gap_m", std::hypot(dx, dy)},
          {"intent", SandboxNames::intent(player.intent)},
          {"target", {player.tacticalTarget.x, player.tacticalTarget.y}},
          {"chasing", player.chasingLooseBall},
          {"meet_seconds", player.looseBallMeetSeconds},
          {"foot_cooldown", player.footCooldown},
          {"trap_timer", player.trapTimer},
          {"fallen", player.fallen},
          {"airborne", player.airborne()}};
}

class PossessionTrace final : public MatchRecorder
{
 public:
  explicit PossessionTrace(std::ostream& output) : out(output) {}
  bool wantsPossession() const override { return true; }
  void onPossessionObservation(const MatchEngine& engine,
                               PossessionObservation kind,
                               const PossessionSnapshot& state) override
  {
    static constexpr const char* NAMES[] = {"assign-owner", "acquire",
                                            "clear-owner", "clear-flight"};
    emit({{"type", "helper"},
          {"operation", NAMES[static_cast<unsigned>(kind)]},
          {"state", snapshot(state)},
          {"match_state", static_cast<int>(engine.getState())},
          {"intermediate", true}});
    ++helperEvents;
  }
  void onPossessionContactGate(const MatchEngine& engine,
                               const MatchPlayer& player,
                               PossessionContactGate gate) override
  {
    const Json actor = playerState(engine, player);
    // Gates can precede reach testing. Keep nearby cases, not every distant
    // teammate skipped by an early friendly-pass gate on every substep.
    if (actor.at("gap_m").get<float>() > 6.0f) return;
    const std::string reason(possessionGateName(gate));
    ++gates[reason];
    emit({{"type", "contact-gate"},
          {"reason", reason},
          {"reach_confirmed",
           gate == PossessionContactGate::WAIT_FOR_CLOSER ||
               gate == PossessionContactGate::CONTACT_NOT_COMMITTED},
          {"actor", actor},
          {"state", snapshot(engine.getPossessionSnapshot())},
          {"ball_position",
           {engine.getBall().position.x, engine.getBall().position.y}},
          {"ball_velocity_mps",
           {engine.getBall().velocity.x, engine.getBall().velocity.y}}});
  }
  void onTeamPlan(const MatchEngine& engine, const MatchTeamPlan& plan) override
  {
    emit({{"type", "team-plan"},
          {"tick", engine.getSimulatedSteps()},
          {"home", plan.homeTeam},
          {"candidates", plan.pressers},
          {"arrival_seconds", plan.pressArrivalSeconds},
          {"note",
           "Actual selected pair/ranking keys; receiver priority may override "
           "rank."}});
  }
  void onStepEnd(const MatchEngine& engine) override
  {
    const auto state = engine.getPossessionSnapshot();
    const auto issues = possessionIssues(state);
    if (issues != 0) ++issueSteps;
    Json actors = Json::array();
    for (const MatchPlayer& player : engine.getPlayers())
      if (player.player && player.onPitch)
        actors.push_back(playerState(engine, player));
    const auto& ball = engine.getBall();
    emit({{"type", "step"},
          {"state", snapshot(state)},
          {"issue_mask", issues},
          {"match_state", static_cast<int>(engine.getState())},
          {"ball_position", {ball.position.x, ball.position.y}},
          {"ball_height_m", ball.z * MatchTuning::Units::BALL_Z_METRES},
          {"ball_velocity_mps",
           {ball.velocity.x, ball.velocity.y, ball.velocityZ}},
          {"actors", std::move(actors)}});
  }
  void emit(const Json& record) { out << record.dump() << '\n'; }
  std::uint64_t helperEvents = 0;
  std::uint64_t issueSteps = 0;
  std::map<std::string, std::uint64_t> gates;

 private:
  std::ostream& out;
};
}  // namespace

int tracePossessionPair(MatchEngine& observed, MatchEngine& reference,
                        double seconds, std::ostream& output)
{
  if (!std::isfinite(seconds) || seconds <= 0.0 || seconds > 3600.0 ||
      observed.getRecorder() || reference.getRecorder())
    return 2;
  PossessionTrace trace(output);
  observed.setRecorder(&trace);
  // Detach even if a stream/allocation exception escapes: no dangling recorder.
  struct Detach
  {
    MatchEngine& engine;
    ~Detach() { engine.setRecorder(nullptr); }
  } detach{observed};
  const MatchEngine copied(observed);
  const bool copyDetached = copied.getRecorder() == nullptr &&
                            copied.getPossessionSnapshot() == observed.getPossessionSnapshot();
  trace.emit({{"type", "metadata"},
              {"schema", 1},
              {"seconds_requested", seconds},
              {"possession", "aardvark"},
              {"projection", "legacy-read-only"},
              {"brains", {brainKey(observed.getBrain(true)), brainKey(observed.getBrain(false))}},
              {"bodies", {bodyKey(observed.getBody(true)), bodyKey(observed.getBody(false))}},
              {"handshake", handshakeKey(observed.getHandshake())},
              {"referee", refereeKey(observed.getReferee())},
              {"managers", {managerKey(observed.getManager(true)), managerKey(observed.getManager(false))}},
              {"ball_physics", ballPhysicsKey(observed.getBallPhysics())},
              {"out_of_play", outOfPlayKey(observed.getOutOfPlay())},
              {"gate_radius_m", 6},
              {"note",
               "No new possession dispatcher yet. Helper snapshots may be "
               "partial; issues sampled at step end. Gate log is not an "
               "exhaustive eligibility judgement."}});
  const auto ticks = static_cast<std::uint64_t>(
      std::ceil(seconds / MatchTuning::Timing::FIXED_STEP_SECONDS));
  bool identical = copyDetached &&
      observed.getPossessionSnapshot() == reference.getPossessionSnapshot();
  std::uint64_t advanced = 0;
  for (; advanced < ticks && identical &&
         observed.getState() != MatchState::FULL_TIME;
       ++advanced)
  {
    observed.advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
    reference.advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
    identical =
        observed.getState() == reference.getState() &&
        observed.getPossessionSnapshot() == reference.getPossessionSnapshot() &&
        SandboxRecorder::stateChecksum(observed) ==
            SandboxRecorder::stateChecksum(reference);
    if (!output) return 2;
  }
  trace.emit({{"type", "summary"},
              {"ticks_compared", advanced},
              {"observed_matches_reference", identical},
              {"copy_detaches_recorder", copyDetached},
              {"helper_events", trace.helperEvents},
              {"issue_steps", trace.issueSteps},
              {"nearby_gates", trace.gates}});
  return output ? (identical ? 0 : 1) : 2;
}
