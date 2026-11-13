// Offline public-API scenarios: friendly takeover and delivery eligibility.
#include <iostream>
#include <memory>
#include <vector>
#include "model/match_engine.h"
#include "model/player.h"
#include "tools/lab_fixtures.h"

int main()
{
  auto config = Lab::loadStatsConfig();
  std::vector<std::unique_ptr<Player>> pool;
  const auto home = Lab::buildLabLineup(1, 85, pool);
  const auto away = Lab::buildLabLineup(2, 85, pool);
  int failures = 0;
  for (bool mirror : {false, true})
    for (bool delivery : {false, true})
      for (auto policy : {PossessionId::AARDVARK, PossessionId::BINTURONG})
      {
        MatchEngine engine(home, away, Strategy{}, Strategy{}, config, 27,
                           HandshakeId::BINTURONG, policy);
        engine.setBrain(true, BrainId::BINTURONG);
        engine.setBrain(false, BrainId::BINTURONG);
        engine.setBody(true, BodyId::BINTURONG);
        engine.setBody(false, BodyId::BINTURONG);
        engine.setBallPhysics(BallPhysicsId::BINTURONG);
        PlayerID owner = 0, friendId = 0;
        for (const auto& p : engine.getPlayers())
          if (p.player && !p.isGoalkeeper && p.isHomeTeam != mirror)
          { if (!owner) owner = p.player->getId(); else if (!friendId) friendId = p.player->getId(); }
        for (const auto& p : engine.getPlayers())
          if (p.player && p.player->getId() != owner && p.player->getId() != friendId)
            engine.removeBeforeKickOff(p.player->getId());
        const float sign = mirror ? -1.0f : 1.0f;
        const PitchPoint at{0.5f, 0.5f};
        MatchScenario scenario;
        scenario.carrierId = delivery ? 0 : owner;
        scenario.ballPosition = at;
        scenario.players = {{owner, at, false},
                            {friendId, PitchPoint{at.x + (delivery ? sign : 1.0f) * 0.70f / MatchTuning::Pitch::MATCH_LENGTH_METRES, at.y}, false}};
        if (delivery)
        {
          scenario.players[0].position.x += sign * 10.0f / MatchTuning::Pitch::MATCH_LENGTH_METRES;
          scenario.players[1].position.x += sign * 1.3f / MatchTuning::Pitch::MATCH_LENGTH_METRES;
        }
        if (!engine.startDrill(scenario)) return 2;
        for (const auto& p : scenario.players)
        {
          engine.setDrillTarget(p.playerId, p.position, false, {}, 0, 0,
                                ((!delivery && p.playerId == friendId) ||
                                 (p.playerId == owner && mirror) || (delivery && mirror))
                                    ? 3.14159265f : 0.0f);
          engine.keepBall(p.playerId, true, 0);
        }
        // Hold the original carrier still, so this checks physical permission,
        // independently of which player the tactical planner would send.
        engine.setControlledPlayer(owner);
        if (delivery) engine.feedBall(at, {sign * 8.0f, 0}, 0, owner);
        bool touched = false;
        for (int step = 0; step < (delivery ? 7 : 4); ++step)
        {
          engine.advance(0.1f);
          if (delivery && step == 0 && policy == PossessionId::BINTURONG)
            for (const auto& p : engine.getPlayers())
              if (p.player && p.player->getId() == friendId &&
                  p.intent != PlayerIntent::RECEIVE_PASS)
              {
                std::cerr << "FAIL: nearer non-receiver was not selected to receive\n";
                ++failures;
              }
          if (const auto* stats = engine.findPlayerStats(friendId))
            touched = touched || stats->touches > 0;
        }
        std::cout << "Friendly " << (delivery ? "delivery" : "controlled takeover")
                  << " mirror " << mirror << " policy " << static_cast<int>(policy)
                  << " touched " << touched << '\n';
        if (touched != (policy == PossessionId::BINTURONG)) ++failures;
        const auto& stats = engine.getStats();
        if (stats.homeTackles || stats.awayTackles) ++failures;
        for (const auto& p : engine.getPlayerStats())
          if (p.interceptions) ++failures;
      }
  for (bool mirror : {false, true})
    for (bool offside : {false, true})
    {
      MatchEngine engine(home, away, Strategy{}, Strategy{}, config, 37,
                         HandshakeId::BINTURONG, PossessionId::BINTURONG);
      engine.setBrain(true, BrainId::BINTURONG);
      engine.setBrain(false, BrainId::BINTURONG);
      engine.setBody(true, BodyId::BINTURONG);
      engine.setBody(false, BodyId::BINTURONG);
      PlayerID passer = 0, collector = 0, defender = 0, defender2 = 0;
      for (const auto& p : engine.getPlayers())
        if (p.player && !p.isGoalkeeper)
        {
          auto& first = p.isHomeTeam != mirror ? passer : defender;
          auto& second = p.isHomeTeam != mirror ? collector : defender2;
          if (!first) first = p.player->getId(); else if (!second) second = p.player->getId();
        }
      for (const auto& p : engine.getPlayers())
        if (p.player && p.player->getId() != passer && p.player->getId() != collector &&
            p.player->getId() != defender && p.player->getId() != defender2)
          engine.removeBeforeKickOff(p.player->getId());
      const auto x = [&](float value) { return mirror ? 1.0f - value : value; };
      MatchScenario scenario;
      scenario.carrierId = passer;
      scenario.players = {{passer, PitchPoint{x(0.55f), 0.5f}, false},
                          {collector, PitchPoint{x(0.72f), 0.5f}, false},
                          {defender, PitchPoint{x(offside ? 0.68f : 0.80f), 0.1f}, false},
                          {defender2, PitchPoint{x(offside ? 0.68f : 0.80f), 0.9f}, false}};
      if (!engine.startDrill(scenario)) return 2;
      for (const auto& p : scenario.players)
        engine.setDrillTarget(p.playerId, p.position, false, {}, 0, 0,
                              p.playerId == collector ? (mirror ? 0.0f : 3.14159265f)
                                                      : (mirror ? 3.14159265f : 0.0f));
      engine.forcePassTo(passer, PitchPoint{x(0.82f), 0.5f}, ForcedPassStyle::GROUND,
                         std::nullopt, 8.0f);
      bool captured = false, touched = false;
      for (int step = 0; step < 100; ++step)
      {
        engine.advance(0.1f);
        const auto& possession = engine.getPossessionState();
        if (possession.isPass)
        {
          const bool recorded = std::find(possession.passOffsidePlayers.begin(),
              possession.passOffsidePlayers.end(), collector) != possession.passOffsidePlayers.end();
          if (recorded != offside) ++failures;
          captured = true;
        }
        if (const auto* stats = engine.findPlayerStats(collector)) touched = stats->touches > 0;
        if (engine.getStats().homeOffsides || engine.getStats().awayOffsides || touched) break;
      }
      const bool whistled = (engine.getStats().homeOffsides + engine.getStats().awayOffsides) == 1;
      std::cout << "Non-receiver release judgement mirror " << mirror << " offside " << offside
                << " captured " << captured << " whistled " << whistled << " touched " << touched << '\n';
      if (!captured || whistled != offside || (!offside && !touched)) ++failures;
    }
  if (!failures) std::cout << "PASS: friendly takeover, non-receiver delivery and release-time offside on both teams\n";
  return failures ? 1 : 0;
}
