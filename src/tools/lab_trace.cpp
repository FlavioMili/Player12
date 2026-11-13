// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/lab_trace.h"

#include <format>
#include <ostream>

#include "model/match_engine.h"
#include "model/match_engine_internal.h"
#include "model/match_tuning.h"

namespace Lab
{
namespace
{
/** The ball's height in metres. */
float ballHeightMetres(const MatchEngine& engine)
{
  return engine.getBall().z * MatchTuning::Units::BALL_Z_METRES;
}
}  // namespace

void writeTraceHeader(std::ostream& out) { out << "run,tick,who,x,y,z\n"; }

void writeTraceRows(std::ostream& out, std::string_view run, int tick,
                    const MatchEngine& engine)
{
  const MatchPitch& pitch = engine.getPitch();
  const auto& players = engine.getPlayers();
  for (std::size_t index = 0; index < players.size(); ++index)
  {
    const Vector2F at =
        MatchEngineInternal::toMetres(pitch, players[index].position);
    out << std::format("{},{},p{},{:.5f},{:.5f},0\n", run, tick, index, at.x,
                       at.y);
  }
  const Vector2F ball = MatchEngineInternal::toMetres(pitch, engine.getBall().position);
  out << std::format("{},{},ball,{:.5f},{:.5f},{:.5f}\n", run, tick, ball.x,
                     ball.y, ballHeightMetres(engine));
}
}  // namespace Lab
