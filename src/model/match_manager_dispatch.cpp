// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Every touchline decision goes to that side's AI manager
// (MatchEngine::setManager). To add a manager: a ManagerId
// (match_manager.h), its members (match_manager_<name>_members.h, included in
// MatchEngine), its definitions (match_manager_<name>.cpp), and a case in
// each function here.

#include "model/match_engine.h"

void MatchEngine::runAiSubstitutionsFor(bool homeTeam)
{
  switch (managerOf(homeTeam))
  {
    case ManagerId::AARDVARK:
      break;
    case ManagerId::BINTURONG:
      runAiSubstitutionsForBinturong(homeTeam);
      return;
  }
  runAiSubstitutionsForAardvark(homeTeam);
}

const Player* MatchEngine::chooseReplacement(bool homeTeam, PlayerRole role,
                                             bool wantGoalkeeper) const
{
  switch (managerOf(homeTeam))
  {
    case ManagerId::AARDVARK:
      break;
    case ManagerId::BINTURONG:
      return chooseReplacementBinturong(homeTeam, role, wantGoalkeeper);
  }
  return chooseReplacementAardvark(homeTeam, role, wantGoalkeeper);
}

void MatchEngine::runAiTouchline(bool homeTeam)
{
  switch (managerOf(homeTeam))
  {
    case ManagerId::AARDVARK:
      break;
    case ManagerId::BINTURONG:
      runAiTouchlineBinturong(homeTeam);
      return;
  }
  runAiTouchlineAardvark(homeTeam);
}
