// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "match_changes.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

#include "model/match_tuning.h"
#include "model/player.h"

namespace MatchChanges
{
namespace
{
constexpr float SLIDER_TOLERANCE = 0.001f;
/** Same tolerance as the lineup's formation detection. */
constexpr float FORMATION_TOLERANCE = 0.03f;
/** Weight of the natural role against the overall when picking cover. */
constexpr double BASE_WEIGHT = 0.35;
constexpr double FIT_WEIGHT = 0.65;
/** A substitute this tired is a poor choice (condition in [0, 1]). */
constexpr float TIRED_SUBSTITUTE = 0.75f;

bool isDefensive(PlayerRole role)
{
  using enum PlayerRole;
  switch (role)
  {
    case CB:
    case LB:
    case RB:
    case CDM:
    case CM:
    case LM:
    case RM:
      return true;
    default:
      return false;
  }
}

const MatchPlayer* onPitch(const MatchEngine& engine, bool home, PlayerID id)
{
  for (const MatchPlayer& player : engine.getPlayers())
    if (player.player && player.onPitch && player.isHomeTeam == home &&
        player.player->getId() == id)
      return &player;
  return nullptr;
}

const Player* benchPlayer(std::span<const Player* const> bench, PlayerID id)
{
  for (const Player* player : bench)
    if (player && player->getId() == id) return player;
  return nullptr;
}

float metresBetween(Vector2F first, Vector2F second)
{
  return std::hypot((first.x - second.x) * MatchTuning::Pitch::LENGTH_METRES,
                    (first.y - second.y) * MatchTuning::Pitch::WIDTH_METRES);
}

}  // namespace

const char* refusalKey(Refusal refusal)
{
  using enum Refusal;
  switch (refusal)
  {
    case NONE:
      return "";
    case MATCH_OVER:
      return "SUBSTITUTION_REFUSED_FINISHED";
    case LIMIT:
      return "SUBSTITUTION_REFUSED_LIMIT";
    case WINDOWS:
      return "SUBSTITUTION_REFUSED_WINDOWS";
    case NOT_NOW:
      return "SUBSTITUTION_REFUSED_NOW";
    case NOT_ON_PITCH:
      return "SUBSTITUTION_REFUSED_OFF_PITCH";
    case NOT_ON_BENCH:
      return "SUBSTITUTION_REFUSED_NOT_BENCH";
    case ALREADY_PLAYED:
      return "SUBSTITUTION_REFUSED_PLAYED";
    case ALREADY_PLANNED:
      return "SUBSTITUTION_REFUSED_PLANNED";
  }
  return "";
}

Refusal rulesRefusal(const MatchEngine& engine, bool home, int planned)
{
  using enum MatchState;
  using enum Refusal;
  const MatchState state = engine.getState();
  if (state == FULL_TIME) return MATCH_OVER;
  if (engine.getSubstitutionsUsed(home) + planned >= maxSubstitutions(engine))
    return LIMIT;
  if (engine.canSubstitute(home)) return NONE;
  if (state != HALF_TIME &&
      engine.getSubstitutionWindowsUsed(home) >= maxWindows(engine))
    return WINDOWS;
  return NOT_NOW;
}
int maxSubstitutions(const MatchEngine& engine)
{
  return MatchTuning::Rules::MAX_SUBSTITUTIONS_PER_TEAM +
         (engine.wentToExtraTime()
              ? MatchTuning::Rules::EXTRA_TIME_SUBSTITUTIONS
              : 0);
}

int maxWindows(const MatchEngine& engine)
{
  return MatchTuning::Substitution::MAX_WINDOWS +
         (engine.wentToExtraTime()
              ? MatchTuning::Rules::EXTRA_TIME_SUBSTITUTIONS
              : 0);
}

bool atStoppage(const MatchEngine& engine)
{
  using enum MatchState;
  switch (engine.getState())
  {
    case PLAYING:
    case FULL_TIME:
    case PENALTY_SHOOTOUT:
      return false;
    default:
      return true;
  }
}

Refusal SubstitutionPlan::check(const MatchEngine& engine, bool home,
                                std::span<const Player* const> bench,
                                PlayerID out, PlayerID in) const
{
  using enum Refusal;
  if (const Refusal rules =
          rulesRefusal(engine, home, static_cast<int>(planned.size()));
      rules != NONE)
    return rules;
  if (involves(out) || involves(in)) return ALREADY_PLANNED;
  if (!onPitch(engine, home, out)) return NOT_ON_PITCH;
  if (!benchPlayer(bench, in)) return NOT_ON_BENCH;
  if (engine.findPlayerStats(in) != nullptr) return ALREADY_PLAYED;
  return NONE;
}

Refusal SubstitutionPlan::add(const MatchEngine& engine, bool home,
                              std::span<const Player* const> bench,
                              PlayerID out, PlayerID in)
{
  const Refusal refusal = check(engine, home, bench, out, in);
  if (refusal == Refusal::NONE) planned.push_back({out, in});
  return refusal;
}

bool SubstitutionPlan::remove(std::size_t index)
{
  if (index >= planned.size()) return false;
  planned.erase(planned.begin() + static_cast<std::ptrdiff_t>(index));
  if (planned.empty()) confirmed = false;
  return true;
}

void SubstitutionPlan::clear()
{
  planned.clear();
  confirmed = false;
}

bool SubstitutionPlan::involves(PlayerID player) const
{
  return player != 0 &&
         std::ranges::any_of(
             planned, [player](const PlannedSubstitution& change)
             { return change.out == player || change.in == player; });
}

PlayerID SubstitutionPlan::replacementFor(PlayerID out) const
{
  for (const PlannedSubstitution& change : planned)
    if (change.out == out) return change.in;
  return 0;
}

std::vector<SubstitutionOutcome> SubstitutionPlan::apply(
    MatchEngine& engine, bool home, std::span<const Player* const> bench)
{
  using enum Refusal;
  std::vector<SubstitutionOutcome> outcomes;
  outcomes.reserve(planned.size());
  for (const PlannedSubstitution& change : planned)
  {
    // Re-checked now: a planned player may have been sent off meanwhile.
    Refusal refusal = rulesRefusal(engine, home, 0);
    const Player* incoming = benchPlayer(bench, change.in);
    if (refusal == NONE && !onPitch(engine, home, change.out))
      refusal = NOT_ON_PITCH;
    if (refusal == NONE && !incoming) refusal = NOT_ON_BENCH;
    if (refusal == NONE && engine.findPlayerStats(change.in))
      refusal = ALREADY_PLAYED;
    if (refusal == NONE && !engine.substitutePlayer(change.out, incoming))
      refusal = NOT_NOW;
    outcomes.push_back({change, refusal});
  }
  clear();
  return outcomes;
}

std::vector<SubstitutionOutcome> SubstitutionPlan::applyIfDue(
    MatchEngine& engine, bool home, std::span<const Player* const> bench)
{
  if (!confirmed || planned.empty() || !atStoppage(engine)) return {};
  return apply(engine, home, bench);
}

float positionFit(PlayerRole substitute, PlayerRole position)
{
  return Formation::roleFit(substitute, position);
}

std::vector<Suggestion> suggestSubstitutions(
    const MatchEngine& engine, bool home, std::span<const Player* const> bench,
    const SubstitutionPlan& plan, const StatsConfig& config,
    std::size_t maximum)
{
  std::vector<Suggestion> result;
  const int changesLeft = maxSubstitutions(engine) -
                          engine.getSubstitutionsUsed(home) -
                          static_cast<int>(plan.entries().size());
  if (maximum == 0 || changesLeft <= 0 ||
      rulesRefusal(engine, home, static_cast<int>(plan.entries().size())) !=
          Refusal::NONE)
    return result;
  maximum = std::min(maximum, static_cast<std::size_t>(changesLeft));

  using Tuning = MatchTuning::Substitution;
  const float minute = engine.getMatchTimeMinutes();
  struct Need
  {
    const MatchPlayer* player;
    std::byte reasons;
    float urgency;
  };
  std::vector<Need> needs;
  for (const MatchPlayer& player : engine.getPlayers())
  {
    if (!player.player || !player.onPitch || player.isHomeTeam != home ||
        plan.involves(player.player->getId()))
      continue;
    std::byte reasons{};
    float urgency = 0.0f;
    if (player.isInjured)
    {
      reasons |= static_cast<std::byte>(SUGGEST_INJURED);
      urgency += 2.0f;
    }
    if (player.stamina < Tuning::FATIGUE_THRESHOLD)
    {
      reasons |= static_cast<std::byte>(SUGGEST_TIRED);
      urgency += Tuning::FATIGUE_THRESHOLD - player.stamina + 0.1f;
    }
    if (player.yellowCards > 0 && minute >= Tuning::CARD_RISK_MINUTE &&
        !player.isGoalkeeper && isDefensive(player.player->getRole()))
    {
      reasons |= static_cast<std::byte>(SUGGEST_BOOKED);
      urgency += Tuning::CARD_RISK_NEED;
    }
    if (reasons != std::byte{}) needs.push_back({&player, reasons, urgency});
  }
  std::ranges::sort(needs, [](const Need& left, const Need& right)
                    { return left.urgency > right.urgency; });

  std::vector<PlayerID> taken;
  for (const Need& need : needs)
  {
    if (result.size() >= maximum) break;
    const bool keeper = need.player->isGoalkeeper;
    const PlayerRole role = need.player->player->getRole();
    const Player* best = nullptr;
    double bestScore = -std::numeric_limits<double>::infinity();
    for (const Player* candidate : bench)
    {
      if (!candidate || plan.involves(candidate->getId()) ||
          engine.findPlayerStats(candidate->getId()) != nullptr ||
          std::ranges::find(taken, candidate->getId()) != taken.end())
        continue;
      if ((candidate->getRole() == PlayerRole::GK) != keeper) continue;
      const double fit =
          keeper ? 1.0
                 : static_cast<double>(positionFit(candidate->getRole(), role));
      const float condition = candidate->getDynamics().condition / 100.0f;
      const double score = candidate->getOverall(config) *
                           (BASE_WEIGHT + FIT_WEIGHT * fit) *
                           (condition < TIRED_SUBSTITUTE ? 0.85 : 1.0);
      if (score > bestScore)
      {
        bestScore = score;
        best = candidate;
      }
    }
    if (!best) continue;
    taken.push_back(best->getId());
    result.push_back({need.player->player->getId(), best->getId(),
                      std::to_integer<std::uint8_t>(need.reasons),
                      need.player->stamina});
  }
  return result;
}

bool sameSliders(const StrategySliders& left, const StrategySliders& right)
{
  return std::ranges::all_of(
      TACTIC_SLIDERS,
      [&](const SliderInfo& slider)
      {
        return std::abs(left.*slider.value - right.*slider.value) <
               SLIDER_TOLERANCE;
      });
}

int detectTacticPreset(const StrategySliders& sliders)
{
  for (std::size_t index = 0; index < TACTIC_PRESETS.size(); ++index)
    if (sameSliders(sliders, TACTIC_PRESETS[index].sliders))
      return static_cast<int>(index);
  return -1;
}

int detectFormation(std::span<const Vector2F> shape)
{
  if (shape.size() != 10) return -1;
  for (std::size_t presetIndex = 0; presetIndex < Formation::PRESETS.size();
       ++presetIndex)
  {
    const Formation::Preset& preset = Formation::PRESETS[presetIndex];
    std::array<bool, 10> taken{};
    const bool matches = std::ranges::all_of(
        shape,
        [&](const Vector2F& position)
        {
          for (std::size_t slot = 0; slot < preset.slots.size(); ++slot)
          {
            const Vector2F& target = preset.slots[slot].position;
            if (!taken[slot] &&
                std::abs(target.x - position.x) <= FORMATION_TOLERANCE &&
                std::abs(target.y - position.y) <= FORMATION_TOLERANCE)
            {
              taken[slot] = true;
              return true;
            }
          }
          return false;
        });
    if (matches) return static_cast<int>(presetIndex);
  }
  return -1;
}

std::vector<Vector2F> formationFor(const MatchEngine& engine, bool home,
                                   const Formation::Preset& preset)
{
  std::vector<Vector2F> shape = engine.getFormation(home);
  if (shape.size() != preset.slots.size()) return shape;

  // Occupant role of each slot (UNKNOWN for a slot left empty).
  std::array<PlayerRole, 10> roles{};
  roles.fill(PlayerRole::UNKNOWN);
  for (const MatchPlayer& player : engine.getPlayers())
  {
    if (!player.player || !player.onPitch || player.isHomeTeam != home ||
        player.isGoalkeeper || player.formationSlot < 0 ||
        static_cast<std::size_t>(player.formationSlot) >= roles.size())
      continue;
    roles[static_cast<std::size_t>(player.formationSlot)] =
        player.player->getRole();
  }

  // Greedy global matching: the best remaining (slot, spot) pair first, so
  // natural roles win and ties go to the nearest spot.
  std::array<bool, 10> slotDone{};
  std::array<bool, 10> spotDone{};
  std::vector<Vector2F> result(shape.size());
  for ([[maybe_unused]] const Vector2F& position : shape)
  {
    float bestScore = -std::numeric_limits<float>::infinity();
    std::size_t bestSlot = 0;
    std::size_t bestSpot = 0;
    for (std::size_t slot = 0; slot < shape.size(); ++slot)
    {
      if (slotDone[slot]) continue;
      // Occupied slots are matched before empty ones.
      const bool occupied = roles[slot] != PlayerRole::UNKNOWN;
      for (std::size_t spot = 0; spot < preset.slots.size(); ++spot)
      {
        if (spotDone[spot]) continue;
        const float fit =
            occupied ? Formation::roleFit(roles[slot], preset.slots[spot].role)
                     : 0.0f;
        const float score =
            (occupied ? 10.0f : 0.0f) + fit * 4.0f -
            metresBetween(shape[slot], preset.slots[spot].position) / 100.0f;
        if (score > bestScore)
        {
          bestScore = score;
          bestSlot = slot;
          bestSpot = spot;
        }
      }
    }
    slotDone[bestSlot] = true;
    spotDone[bestSpot] = true;
    result[bestSlot] = preset.slots[bestSpot].position;
  }
  return result;
}

float reshapeCost(std::span<const Vector2F> from, std::span<const Vector2F> to)
{
  if (from.size() != to.size() || from.empty()) return 0.0f;
  float moved = 0.0f;
  for (std::size_t slot = 0; slot < from.size(); ++slot)
  {
    const Vector2F clamped{
        std::clamp(to[slot].x, MatchTuning::Pitch::PLAYER_MIN_X,
                   MatchTuning::Pitch::PLAYER_MAX_X),
        std::clamp(to[slot].y, MatchTuning::Pitch::PLAYER_MIN_Y,
                   MatchTuning::Pitch::PLAYER_MAX_Y)};
    moved += metresBetween(clamped, from[slot]);
  }
  using T = MatchTuning::Touchline;
  return std::min(T::MAX_RESHAPE_FAMILIARITY_COST,
                  moved / static_cast<float>(from.size()) *
                      T::RESHAPE_FAMILIARITY_PER_METRE);
}

}  // namespace MatchChanges
