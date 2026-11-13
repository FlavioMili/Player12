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
void writeTraceHeader(std::ostream& out)
{
  out << "run,tick,who,x,y,z,facing\n";
}

void writeTraceRows(std::ostream& out, std::string_view run, int tick,
                    const MatchEngine& engine)
{
  const auto& players = engine.getPlayers();
  for (std::size_t index = 0; index < players.size(); ++index)
  {
    const PitchPoint at = players[index].position;
    out << std::format("{},{},p{},{:.5f},{:.5f},0,{:.5f}\n", run, tick, index,
                       at.x, at.y, players[index].facingAngle);
  }
  const PitchPoint ball = engine.getBall().position;
  out << std::format("{},{},ball,{:.5f},{:.5f},{:.5f},0\n", run, tick, ball.x,
                     ball.y, engine.getBall().z);
}
}  // namespace Lab
