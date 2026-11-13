// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "global/types.h"
#include "model/match_point.h"
#include "model/match_tuning.h"

/** Machine-readable category of a match event. */
enum class MatchEventType : std::uint8_t
{
  INFO,
  KICK_OFF,
  GOAL,
  OWN_GOAL,
  SHOT,
  SAVE,
  SHOT_BLOCKED,
  SHOT_OFF_TARGET,
  WOODWORK,
  FOUL,
  ADVANTAGE,
  YELLOW_CARD,
  SECOND_YELLOW,
  RED_CARD,
  INJURY,
  SUBSTITUTION,
  OFFSIDE,
  CORNER,
  FREE_KICK,
  PENALTY,
  PENALTY_MISSED,
  THROW_IN,
  GOAL_KICK,
  ADDED_TIME,
  HALF_TIME,
  SECOND_HALF,
  FULL_TIME,
  /** A kick of the penalty shootout (detail: scored, saved or missed; none
   * when the shootout begins). Shootouts never count as goals or shots. */
  PENALTY_SHOOTOUT
};

std::string_view matchEventTypeName(MatchEventType type);

/** Variant of an event, for the commentary (MatchEvent::detail). */
enum class MatchEventDetail : std::uint8_t
{
  NONE,
  /** SHOT: a headed attempt. */
  HEADER,
  /** SAVE: parried back into play / tipped behind for a corner. */
  PARRIED,
  TIPPED_BEHIND,
  /** SHOT_BLOCKED: a direct free kick stopped by the wall. */
  WALL,
  /** INJURY: hurt in a challenge rather than on his own. */
  CONTACT,
  /** INFO: the match was abandoned (a side fell below seven players). */
  ABANDONED,
  /** PENALTY_SHOOTOUT: the outcome of a kick. */
  SCORED,
  SAVED,
  MISSED
};

/**
 * One entry of the match log. The structured fields say what happened;
 * `description` is the localised commentary line (MatchCommentary) written
 * when the event is logged, so views that know the team names or switch
 * language should call MatchCommentary::describe() instead.
 *
 * `timeMinute` is the match clock: the first half runs from 0 to 45 plus its
 * added time, the second half restarts at 45. `addedMinute` is the part of
 * the clock beyond the regulation end of the period (0 when not in added
 * time), so "45+2'" is timeMinute 47 in period 1 with addedMinute 2.
 * `hasTeam`/`isHomeTeam` describe the side of the primary player (for an
 * OWN_GOAL that is the defender's side; the goal counts for the other side).
 * For goals the secondary player is the assist provider, for fouls the
 * fouled player, for substitutions the outgoing player and for saves the
 * shooter.
 */
struct MatchEvent
{
  float timeMinute = 0.0f;
  std::string description;
  MatchEventType type = MatchEventType::INFO;
  int period = 1;
  float addedMinute = 0.0f;
  bool hasTeam = false;
  bool isHomeTeam = false;
  PlayerID primaryPlayerId = 0;
  PlayerID secondaryPlayerId = 0;
  PitchPoint position{MatchTuning::Pitch::MATCH_CENTRE_X_METRES,
                      MatchTuning::Pitch::MATCH_CENTRE_Y_METRES};
  float xg = 0.0f;
  MatchEventDetail detail = MatchEventDetail::NONE;
  /** Score once the event happened (after the goal for a goal). */
  int homeScore = 0;
  int awayScore = 0;
  /** ADDED_TIME: the announced added minutes. */
  int minutes = 0;
  /** Penalty shootout score once the event happened (0-0 before one). */
  int homeShootout = 0;
  int awayShootout = 0;
};

/**
 * Per-player match statistics. One entry exists for every player who took
 * part, including substitutes and players who left the pitch; the list is
 * never truncated, so it is the authoritative source for post-match data.
 */
struct PlayerMatchStats
{
  PlayerID playerId = 0;
  bool isHomeTeam = false;
  PlayerRole role = PlayerRole::UNKNOWN;
  bool started = false;
  bool substitutedOn = false;
  bool substitutedOff = false;
  bool sentOff = false;
  bool injured = false;
  float minutesPlayed = 0.0f;
  int goals = 0;
  int ownGoals = 0;
  int assists = 0;
  int shots = 0;
  int shotsOnTarget = 0;
  float expectedGoals = 0.0f;
  int passesAttempted = 0;
  int passesCompleted = 0;
  int keyPasses = 0;
  int tacklesAttempted = 0;
  int tacklesWon = 0;
  int interceptions = 0;
  int clearances = 0;
  /** Opponents' shots his body stopped (shot block plan step 4). */
  int blockedShots = 0;
  /** Opponents' passes his body stopped (pass block plan). */
  int blockedPasses = 0;
  int aerialDuelsWon = 0;
  int aerialDuelsLost = 0;
  int saves = 0;
  int goalsConceded = 0;
  int foulsCommitted = 0;
  int foulsSuffered = 0;
  int yellowCards = 0;
  int redCards = 0;
  /** Times he took control of the ball (receptions, wins, restarts). */
  int touches = 0;
  /** Times he started closing down the ball carrier. */
  int pressures = 0;
  /** Distance covered in metres (the simulation runs in real time). */
  float distanceMetres = 0.0f;
  float secondHalfDistanceMetres = 0.0f;
  /** Distance above 5.5 m/s (19.8 km/h) and above 7 m/s (25.2 km/h). */
  float highIntensityMetres = 0.0f;
  float secondHalfHighIntensityMetres = 0.0f;
  float sprintMetres = 0.0f;
  /** Distance below 2 m/s (walking), 2-4 m/s (jogging) and 4-5.5 m/s
   * (running); above that is high intensity. */
  float walkMetres = 0.0f;
  float jogMetres = 0.0f;
  float runMetres = 0.0f;
  /** Distance above 4 m/s (14.4 km/h: the research's "high-intensity
   * running", Mohr 2003, Bradley 2009) by 5-minute window of the clock: 0-5
   * ... 40-45 of the first half, then of the second (added and extra time
   * left out). */
  static constexpr std::size_t FIVE_MINUTE_WINDOWS = 18;
  std::array<float, FIVE_MINUTE_WINDOWS> runningByFive{};
  /** Metabolic energy of his movement (J/kg; bodies with an energy model),
   * and the part above his critical speed's power (anaerobic). */
  float metabolicEnergy = 0.0f;
  float anaerobicEnergy = 0.0f;
  /** The same two while he was slowing down. */
  float brakingEnergy = 0.0f;
  float brakingAnaerobicEnergy = 0.0f;
  /** Time (s) and distance (m) in each movement mode by his own bands now
   * (walk, jog, run, sprint: MovementMode). */
  std::array<float, 4> modeSeconds{};
  std::array<float, 4> modeMetres{};
  /** Elapsed match minutes when his fuel first ran out (-1: never). */
  float emptyAtMinute = -1.0f;
  /** Efforts above 7 m/s. */
  int sprints = 0;
  /** Highest speed reached (m/s). */
  float topSpeed = 0.0f;
  /** Physical condition in [0, 1] when the player last left or at the end. */
  float condition = 1.0f;
  /** Event-driven rating, 6.0 baseline, bounded to [3, 10]. */
  float rating = MatchTuning::Rating::BASELINE;
};

enum class SubstitutionReason : std::uint8_t
{
  MANUAL,
  FATIGUE,
  INJURY,
  TACTICAL,
  CARD_RISK,
  GOALKEEPER_REPLACEMENT
};

struct MatchSubstitution
{
  float timeMinute = 0.0f;
  int period = 1;
  bool isHomeTeam = false;
  PlayerID outgoingPlayerId = 0;
  PlayerID incomingPlayerId = 0;
  SubstitutionReason reason = SubstitutionReason::MANUAL;
};
