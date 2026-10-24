// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/mentoring.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include "database/gamedata.h"
#include "database/sqlite_rows.h"
#include "model/player.h"

namespace
{
/** Applies whole trait points once the pending share reaches one. */
std::uint8_t applyPending(std::uint8_t value, float& pending)
{
  const float step = std::trunc(pending);
  if (step == 0.0f) return value;
  pending -= step;
  return static_cast<std::uint8_t>(
      std::clamp(static_cast<int>(value) + static_cast<int>(step), 1, 100));
}
}  // namespace

const char* mentoringErrorKey(MentoringError error)
{
  using enum MentoringError;
  switch (error)
  {
    case None:
      return "MENTORING_ERROR_NONE";
    case UnknownPlayer:
      return "MENTORING_ERROR_UNKNOWN_PLAYER";
    case NotSameClub:
      return "MENTORING_ERROR_NOT_SAME_CLUB";
    case MentorTooYoung:
      return "MENTORING_ERROR_MENTOR_TOO_YOUNG";
    case MenteeTooOld:
      return "MENTORING_ERROR_MENTEE_TOO_OLD";
    case SamePlayer:
      return "MENTORING_ERROR_SAME_PLAYER";
    case AlreadyMentor:
      return "MENTORING_ERROR_ALREADY_MENTOR";
    case AlreadyMentee:
      return "MENTORING_ERROR_ALREADY_MENTEE";
    case MentorIsMentee:
      return "MENTORING_ERROR_MENTOR_IS_MENTEE";
    case GroupFull:
      return "MENTORING_ERROR_GROUP_FULL";
    case UnknownGroup:
      break;
  }
  return "MENTORING_ERROR_UNKNOWN_GROUP";
}

namespace Mentoring
{
float receptiveness(int age)
{
  if (age <= 18) return 1.0f;
  return std::clamp(1.0f - 0.12f * static_cast<float>(age - 18), 0.4f, 1.0f);
}

float influence(int mentor_age)
{
  return std::clamp(
      0.6f + 0.05f * static_cast<float>(mentor_age - MENTOR_MIN_AGE), 0.6f,
      1.0f);
}

float weeklyShift(float mentor_value, float mentee_value, float receptiveness,
                  float influence, float already_shifted)
{
  const float raw = std::clamp(
      WEEKLY_RATE * receptiveness * influence * (mentor_value - mentee_value),
      -MAX_WEEKLY_STEP, MAX_WEEKLY_STEP);
  const float total =
      std::clamp(already_shifted + raw, -MAX_TOTAL_SHIFT, MAX_TOTAL_SHIFT);
  return total - already_shifted;
}

float developmentMultiplier(std::uint8_t mentor_professionalism)
{
  return 1.0f +
         MAX_DEVELOPMENT_BONUS *
             std::clamp(
                 (static_cast<float>(mentor_professionalism) - 50.0f) / 50.0f,
                 0.0f, 1.0f);
}
}  // namespace Mentoring

MentoringGroup* MentoringSystem::find(std::uint32_t group_id)
{
  const auto found = std::ranges::find(all, group_id, &MentoringGroup::id);
  return found == all.end() ? nullptr : std::to_address(found);
}

const MentoringGroup* MentoringSystem::groupOf(PlayerID player_id) const
{
  for (const MentoringGroup& group : all)
  {
    if (group.mentor_id == player_id) return &group;
    if (std::ranges::contains(group.mentees, player_id,
                              &MenteeState::player_id))
      return &group;
  }
  return nullptr;
}

MentoringError MentoringSystem::createGroup(const GameData& gamedata,
                                            TeamID team_id, PlayerID mentor_id,
                                            std::uint32_t* id)
{
  using enum MentoringError;
  const auto mentor = gamedata.getPlayer(mentor_id);
  if (!mentor) return UnknownPlayer;
  if (team_id == FREE_AGENTS_TEAM_ID || mentor->get().getTeamId() != team_id)
    return NotSameClub;
  if (mentor->get().getAge() < Mentoring::MENTOR_MIN_AGE) return MentorTooYoung;
  if (const MentoringGroup* group = groupOf(mentor_id))
    return group->mentor_id == mentor_id ? AlreadyMentor : MentorIsMentee;
  MentoringGroup group;
  group.id = next_id++;
  group.team_id = team_id;
  group.mentor_id = mentor_id;
  all.push_back(group);
  if (id) *id = group.id;
  return None;
}

MentoringError MentoringSystem::addMentee(const GameData& gamedata,
                                          std::uint32_t group_id,
                                          PlayerID mentee_id)
{
  using enum MentoringError;
  MentoringGroup* group = find(group_id);
  if (!group) return UnknownGroup;
  const auto mentee = gamedata.getPlayer(mentee_id);
  if (!mentee) return UnknownPlayer;
  if (mentee_id == group->mentor_id) return SamePlayer;
  if (mentee->get().getTeamId() != group->team_id) return NotSameClub;
  if (mentee->get().getAge() > Mentoring::MENTEE_MAX_AGE) return MenteeTooOld;
  if (const MentoringGroup* other = groupOf(mentee_id))
    return other->mentor_id == mentee_id ? AlreadyMentor : AlreadyMentee;
  if (group->mentees.size() >= Mentoring::MAX_MENTEES) return GroupFull;
  MenteeState state;
  state.player_id = mentee_id;
  group->mentees.push_back(state);
  return None;
}

MentoringError MentoringSystem::removeMentee(std::uint32_t group_id,
                                             PlayerID mentee_id)
{
  MentoringGroup* group = find(group_id);
  if (!group) return MentoringError::UnknownGroup;
  if (std::erase_if(group->mentees, [mentee_id](const MenteeState& state)
                    { return state.player_id == mentee_id; }) == 0)
    return MentoringError::UnknownPlayer;
  return MentoringError::None;
}

MentoringError MentoringSystem::dissolve(std::uint32_t group_id)
{
  return std::erase_if(all, [group_id](const MentoringGroup& group)
                       { return group.id == group_id; }) == 0
             ? MentoringError::UnknownGroup
             : MentoringError::None;
}

void MentoringSystem::prune(const GameData& gamedata)
{
  std::erase_if(all,
                [&gamedata](const MentoringGroup& group)
                {
                  const auto mentor = gamedata.getPlayer(group.mentor_id);
                  return !mentor || mentor->get().getTeamId() != group.team_id;
                });
  for (MentoringGroup& group : all)
  {
    std::erase_if(group.mentees,
                  [&gamedata, &group](const MenteeState& state)
                  {
                    const auto mentee = gamedata.getPlayer(state.player_id);
                    return !mentee ||
                           mentee->get().getTeamId() != group.team_id ||
                           mentee->get().getAge() > Mentoring::MENTEE_MAX_AGE;
                  });
  }
}

void MentoringSystem::onWeek(GameData& gamedata)
{
  prune(gamedata);
  auto& players = gamedata.getPlayers();
  for (MentoringGroup& group : all)
  {
    const auto mentor_it = players.find(group.mentor_id);
    if (mentor_it == players.end()) continue;
    const Player& mentor = mentor_it->second;
    const PlayerTraits mentor_traits = mentor.getTraits();
    const float influence = Mentoring::influence(mentor.getAge());
    const bool gloomy =
        mentor.getDynamics().morale < Mentoring::UNHAPPY_MENTOR_MORALE;
    for (MenteeState& state : group.mentees)
    {
      const auto mentee_it = players.find(state.player_id);
      if (mentee_it == players.end()) continue;
      Player& mentee = mentee_it->second;
      const float receptiveness = Mentoring::receptiveness(mentee.getAge());
      PlayerTraits traits = mentee.getTraits();
      const float professionalism = Mentoring::weeklyShift(
          mentor_traits.professionalism,
          static_cast<float>(traits.professionalism) +
              state.professionalism_pending,
          receptiveness, influence, state.professionalism_shift);
      const float temperament = Mentoring::weeklyShift(
          mentor_traits.temperament,
          static_cast<float>(traits.temperament) + state.temperament_pending,
          receptiveness, influence, state.temperament_shift);
      state.professionalism_shift += professionalism;
      state.temperament_shift += temperament;
      state.professionalism_pending += professionalism;
      state.temperament_pending += temperament;
      traits.professionalism =
          applyPending(traits.professionalism, state.professionalism_pending);
      traits.temperament =
          applyPending(traits.temperament, state.temperament_pending);
      mentee.setTraits(traits);
      if (gloomy)
      {
        PlayerDynamics& dynamics = mentee.mutableDynamics();
        dynamics.morale =
            std::max(0.0f, dynamics.morale - Mentoring::GLOOM_PER_WEEK);
      }
    }
  }
}

float MentoringSystem::developmentMultiplier(const GameData& gamedata,
                                             PlayerID player_id) const
{
  for (const MentoringGroup& group : all)
  {
    if (!std::ranges::contains(group.mentees, player_id,
                               &MenteeState::player_id))
      continue;
    const auto mentor = gamedata.getPlayer(group.mentor_id);
    return mentor ? Mentoring::developmentMultiplier(
                        mentor->get().getTraits().professionalism)
                  : 1.0f;
  }
  return 1.0f;
}

std::vector<MentoringGroup> MentoringSystem::groupsFor(TeamID team_id) const
{
  std::vector<MentoringGroup> result;
  for (const MentoringGroup& group : all)
    if (group.team_id == team_id) result.push_back(group);
  return result;
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

void MentoringSystem::clear()
{
  all.clear();
  next_id = 1;
}

void MentoringSystem::load(const std::shared_ptr<DatabaseConnection>& db_conn)
{
  using namespace SqliteRows;
  clear();
  forEach(*db_conn,
          "SELECT id, team_id, mentor_id FROM MentoringGroups ORDER BY id;",
          [this](sqlite3_stmt* stmt)
          {
            MentoringGroup group;
            group.id = column<std::uint32_t>(stmt, 0);
            group.team_id = column<TeamID>(stmt, 1);
            group.mentor_id = column<PlayerID>(stmt, 2);
            next_id = std::max(next_id, group.id + 1);
            all.push_back(std::move(group));
          });
  forEach(*db_conn,
          "SELECT group_id, player_id, professionalism_shift, "
          "temperament_shift, professionalism_pending, temperament_pending "
          "FROM MentoringMentees ORDER BY rowid;",
          [this](sqlite3_stmt* stmt)
          {
            MentoringGroup* group = find(column<std::uint32_t>(stmt, 0));
            if (!group) return;
            MenteeState state;
            state.player_id = column<PlayerID>(stmt, 1);
            state.professionalism_shift = columnFloat(stmt, 2);
            state.temperament_shift = columnFloat(stmt, 3);
            state.professionalism_pending = columnFloat(stmt, 4);
            state.temperament_pending = columnFloat(stmt, 5);
            group->mentees.push_back(state);
          });
}

void MentoringSystem::save(
    const std::shared_ptr<DatabaseConnection>& db_conn) const
{
  using namespace SqliteRows;
  const DatabaseConnection& db = *db_conn;
  clearTable(db, "MentoringGroups");
  clearTable(db, "MentoringMentees");
  insertAll(db,
            "INSERT INTO MentoringGroups (id, team_id, mentor_id) VALUES (?, "
            "?, ?);",
            all,
            [](sqlite3_stmt* row, const MentoringGroup& group)
            {
              sqlite3_bind_int64(row, 1, group.id);
              sqlite3_bind_int(row, 2, group.team_id);
              sqlite3_bind_int64(row, 3, group.mentor_id);
            });
  std::vector<std::pair<std::uint32_t, MenteeState>> mentees;
  for (const MentoringGroup& group : all)
    for (const MenteeState& state : group.mentees)
      mentees.emplace_back(group.id, state);
  insertAll(db,
            "INSERT INTO MentoringMentees (group_id, player_id, "
            "professionalism_shift, temperament_shift, "
            "professionalism_pending, temperament_pending) VALUES (?, ?, ?, "
            "?, ?, ?);",
            mentees,
            [](sqlite3_stmt* row, const auto& entry)
            {
              sqlite3_bind_int64(row, 1, entry.first);
              sqlite3_bind_int64(row, 2, entry.second.player_id);
              sqlite3_bind_double(row, 3, entry.second.professionalism_shift);
              sqlite3_bind_double(row, 4, entry.second.temperament_shift);
              sqlite3_bind_double(row, 5, entry.second.professionalism_pending);
              sqlite3_bind_double(row, 6, entry.second.temperament_pending);
            });
}
