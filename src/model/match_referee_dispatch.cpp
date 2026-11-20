// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Every judgement goes to the match's referee (MatchEngine::setReferee). To
// add a referee: a RefereeId (match_referee.h), its members
// (match_referee_<name>_members.h, included in MatchEngine), its definitions
// (match_referee_<name>.cpp), and a case in each function here.

#include "model/match_engine.h"

void MatchEngine::commitFoul(MatchPlayer& offender, MatchPlayer& victim,
                             bool ballWon, bool reckless)
{
  switch (refereeId)
  {
    case RefereeId::AARDVARK:
      break;
    case RefereeId::BINTURONG:
      commitFoulBinturong(offender, victim, ballWon, reckless);
      return;
  }
  commitFoulAardvark(offender, victim, ballWon, reckless);
}

void MatchEngine::updatePendingAdvantage(float dt)
{
  switch (refereeId)
  {
    case RefereeId::AARDVARK:
      break;
    case RefereeId::BINTURONG:
      updatePendingAdvantageBinturong(dt);
      return;
  }
  updatePendingAdvantageAardvark(dt);
}

bool MatchEngine::judgeOffside(const MatchPlayer& passer,
                               const PassOption& option,
                               float defenderLine) const
{
  switch (refereeId)
  {
    case RefereeId::AARDVARK:
      break;
    case RefereeId::BINTURONG:
      return judgeOffsideBinturong(passer, option, defenderLine);
  }
  return judgeOffsideAardvark(passer, option, defenderLine);
}

int MatchEngine::addedMinutesDue() const
{
  switch (refereeId)
  {
    case RefereeId::AARDVARK:
      break;
    case RefereeId::BINTURONG:
      return addedMinutesDueBinturong();
  }
  return addedMinutesDueAardvark();
}
