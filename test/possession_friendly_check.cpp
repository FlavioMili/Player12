// Offline public-API scenarios: friendly takeover and delivery eligibility.
#include <iostream>
#include <memory>
#include <vector>

#include "model/match_engine.h"
#include "model/match_pitch.h"
#include "model/player.h"
#include "tools/lab_fixtures.h"

namespace
{
// Tests play on a match pitch, placed by fractions of it.
const MatchPitch& PITCH = MatchPitch::matchSize();
}  // namespace

int main()
{
  auto config = Lab::loadStatsConfig();
  std::vector<std::unique_ptr<Player>> pool;
  const auto home = Lab::buildLabLineup(1, 85, pool);
  const auto away = Lab::buildLabLineup(2, 85, pool);
  int failures = 0;
  for (bool mirror : {false, true})
  {
    for (bool delivery : {false, true})
    {
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
        {
          if ((p.player != nullptr) && !p.isGoalkeeper &&
              p.isHomeTeam != mirror)
          {
            if (owner == 0u)
            {
              owner = p.player->getId();
            }
            else if (friendId == 0u)
            {
              friendId = p.player->getId();
            }
          }
        }
        for (const auto& p : engine.getPlayers())
        {
          if ((p.player != nullptr) && p.player->getId() != owner &&
              p.player->getId() != friendId)
          {
            engine.removeBeforeKickOff(p.player->getId());
          }
        }
        const float sign = mirror ? -1.0F : 1.0F;
        const PitchPoint at = PITCH.at(0.5F, 0.5F);
        MatchScenario scenario;
        scenario.carrierId = delivery ? 0 : owner;
        scenario.ballPosition = at;
        scenario.players = {
            {.playerId = owner, .position = at, .makingRun = false},
            {.playerId = friendId,
             .position =
                 PitchPoint{at.x + ((delivery ? sign : 1.0F) * 0.70F), at.y},
             .makingRun = false}};
        if (delivery)
        {
          scenario.players[0].position.x += sign * 10.0F;
          scenario.players[1].position.x += sign * 1.3F;
        }
        if (!engine.startDrill(scenario))
        {
          return 2;
        }
        for (const auto& p : scenario.players)
        {
          engine.setDrillTarget(
              p.playerId, p.position, false, {}, 0, 0,
              ((!delivery && p.playerId == friendId) ||
               (p.playerId == owner && mirror) || (delivery && mirror))
                  ? 3.14159265F
                  : 0.0F);
          engine.keepBall(p.playerId, true, 0);
        }
        // Hold the original carrier still, so this checks physical permission,
        // independently of which player the tactical planner would send.
        engine.setControlledPlayer(owner);
        if (delivery)
        {
          engine.feedBall(at, {.x = sign * 8.0F, .y = 0}, 0, owner);
        }
        bool touched = false;
        for (int step = 0; step < (delivery ? 7 : 4); ++step)
        {
          engine.advance(0.1F);
          if (delivery && step == 0 && policy == PossessionId::BINTURONG)
          {
            for (const auto& p : engine.getPlayers())
            {
              if ((p.player != nullptr) && p.player->getId() == friendId &&
                  p.intent != PlayerIntent::RECEIVE_PASS)
              {
                std::cerr << "FAIL: nearer non-receiver was not selected to "
                             "receive\n";
                ++failures;
              }
            }
          }
          if (const auto* stats = engine.findPlayerStats(friendId))
          {
            touched = touched || stats->touches > 0;
          }
        }
        std::cout << "Friendly "
                  << (delivery ? "delivery" : "controlled takeover")
                  << " mirror " << mirror << " policy "
                  << static_cast<int>(policy) << " touched " << touched << '\n';
        if (touched != (policy == PossessionId::BINTURONG))
        {
          ++failures;
        }
        const auto& stats = engine.getStats();
        if ((stats.homeTackles != 0) || (stats.awayTackles != 0))
        {
          ++failures;
        }
        for (const auto& p : engine.getPlayerStats())
        {
          if (p.interceptions != 0)
          {
            ++failures;
          }
        }
      }
    }
  }
  for (bool mirror : {false, true})
  {
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
      {
        if ((p.player != nullptr) && !p.isGoalkeeper)
        {
          auto& first = p.isHomeTeam != mirror ? passer : defender;
          auto& second = p.isHomeTeam != mirror ? collector : defender2;
          if (first == 0u)
          {
            first = p.player->getId();
          }
          else if (second == 0u)
          {
            second = p.player->getId();
          }
        }
      }
      for (const auto& p : engine.getPlayers())
      {
        if ((p.player != nullptr) && p.player->getId() != passer &&
            p.player->getId() != collector && p.player->getId() != defender &&
            p.player->getId() != defender2)
        {
          engine.removeBeforeKickOff(p.player->getId());
        }
      }
      const auto x = [&](float value) { return mirror ? 1.0F - value : value; };
      MatchScenario scenario;
      scenario.carrierId = passer;
      scenario.players = {
          {.playerId = passer,
           .position = PITCH.at(x(0.55F), 0.5F),
           .makingRun = false},
          {.playerId = collector,
           .position = PITCH.at(x(0.72F), 0.5F),
           .makingRun = false},
          {.playerId = defender,
           .position = PITCH.at(x(offside ? 0.68F : 0.80F), 0.1F),
           .makingRun = false},
          {.playerId = defender2,
           .position = PITCH.at(x(offside ? 0.68F : 0.80F), 0.9F),
           .makingRun = false}};
      if (!engine.startDrill(scenario))
      {
        return 2;
      }
      for (const auto& p : scenario.players)
      {
        engine.setDrillTarget(p.playerId, p.position, false, {}, 0, 0,
                              p.playerId == collector
                                  ? (mirror ? 0.0F : 3.14159265F)
                                  : (mirror ? 3.14159265F : 0.0F));
      }
      engine.forcePassTo(passer, PITCH.at(x(0.82F), 0.5F),
                         ForcedPassStyle::GROUND, std::nullopt, 8.0F);
      bool captured = false, touched = false;
      for (int step = 0; step < 100; ++step)
      {
        engine.advance(0.1F);
        const auto& possession = engine.getPossessionState();
        if (possession.isPass)
        {
          const bool recorded =
              std::find(possession.passOffsidePlayers.begin(),
                        possession.passOffsidePlayers.end(),
                        collector) != possession.passOffsidePlayers.end();
          if (recorded != offside)
          {
            ++failures;
          }
          captured = true;
        }
        if (const auto* stats = engine.findPlayerStats(collector))
        {
          touched = stats->touches > 0;
        }
        if ((engine.getStats().homeOffsides != 0) ||
            (engine.getStats().awayOffsides != 0) || touched)
        {
          break;
        }
      }
      const bool whistled = (engine.getStats().homeOffsides +
                             engine.getStats().awayOffsides) == 1;
      std::cout << "Non-receiver release judgement mirror " << mirror
                << " offside " << offside << " captured " << captured
                << " whistled " << whistled << " touched " << touched << '\n';
      if (!captured || whistled != offside || (!offside && !touched))
      {
        ++failures;
      }
    }
  }
  if (failures == 0)
  {
    std::cout << "PASS: friendly takeover, non-receiver delivery and "
                 "release-time offside on both teams\n";
  }
  return (failures != 0) ? 1 : 0;
}
