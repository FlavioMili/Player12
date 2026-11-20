#include <iostream>

#include "model/match_possession_claim.h"
#include "model/match_restart_approach.h"

namespace
{
int failures = 0;
void check(bool value, const char* message)
{
  if (!value)
  {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
}  // namespace

int main()
{
  using namespace PossessionClaim;
  const Vector2F stuck{
      .x = 9.3F,
      .y = MatchGround::Match::BOARDS_Y + MatchTuning::Body::RADIUS_METRES};
  const auto gapTarget = RestartApproach::target(
      MatchPitch{}, stuck, {.x = 9.4F, .y = 65.28F}, 0.6F);
  bool throughGap = false;
  for (std::size_t i = 1; i < MatchGround::Match::BOTTOM_BOARDS.size(); ++i)
  {
    throughGap = throughGap ||
                 (gapTarget.x > MatchGround::Match::BOTTOM_BOARDS[i - 1].to &&
                  gapTarget.x < MatchGround::Match::BOTTOM_BOARDS[i].from);
  }
  check(throughGap && gapTarget.y < MatchGround::Match::BOARDS_Y,
        "restart taker behind a solid board approaches through an actual gap");
  const auto approach = RestartApproach::target(
      MatchPitch{}, {.x = 50.0F, .y = 50.0F}, {.x = 51.0F, .y = 50.0F}, 0.6F);
  check(approach.x > 51.0F && approach.y == 50.0F,
        "restart approach aims through the arrival dead zone without moving "
        "the taker");
  check(
      canRecoverAt(2.0F, 0.0F, 7.0F, 4.0F, 1.0F, 0.0F, 0.9F, 0.0F, 0.3F, true),
      "stationary nearby ball is reachable with acceleration");
  check(
      !canRecoverAt(8.0F, 0.0F, 7.0F, 4.0F, 0.5F, 0.0F, 0.9F, 0.0F, 0.3F, true),
      "ball escaping beyond available travel is not reachable");
  check(canRecoverAt(3.0F, 5.0F, 7.0F, 4.0F, 0.5F, 0.0F, 0.9F, 0.0F, 0.3F,
                     true) &&
            !canRecoverAt(3.0F, -5.0F, 7.0F, 4.0F, 0.5F, 0.0F, 0.9F, 0.0F, 0.3F,
                          true),
        "arrival estimate distinguishes moving toward and away from the ball");
  check(
      !canRecoverAt(0.5F, 0.0F, 7.0F, 4.0F, 0.1F, 0.5F, 0.9F, 0.0F, 0.3F, true),
      "foot recovery prevents an impossible immediate repeat contact");
  check(!canRecoverAt(0.5F, 0.0F, 7.0F, 4.0F, 1.0F, 0.0F, 0.9F, 1.0F, 0.3F,
                      true) &&
            !canRecoverAt(0.5F, 0.0F, 7.0F, 4.0F, 1.0F, 0.0F, 0.9F, 0.0F, 0.3F,
                          false),
        "airborne or off-pitch samples cannot establish foot recovery");
  auto held = evaluate(true, false, true, 0.0F, 0.0F, 0.1F);
  check(held.expiry == Expiry::NONE && held.uncontrolledSeconds > 0.0F,
        "recoverable uncollected claim continues but still ages");
  auto marginal = evaluate(true, false, false, 0.0F, 0.0F, 0.1F);
  check(marginal.expiry == Expiry::NONE,
        "brief forecast miss retains the claim");
  auto regained = evaluate(true, false, true, marginal.uncontrolledSeconds,
                           marginal.unrecoverableSeconds, 0.1F);
  check(regained.unrecoverableSeconds == 0.0F &&
            regained.uncontrolledSeconds > marginal.uncontrolledSeconds,
        "recovery clears only forecast grace, not the continuous uncollected "
        "age");
  auto expired = evaluate(true, false, false, 1.0F, 0.2F, 0.1F);
  check(expired.expiry == Expiry::UNRECOVERABLE,
        "sustained loss of recovery expires the claim");
  check(
      evaluate(true, false, true, 5.9F, 0.0F, 0.1F).expiry == Expiry::AGE_LIMIT,
      "maximum duration bounds a claim even while recovery remains plausible");
  check(evaluate(false, false, true, 0.0F, 0.0F, 0.0F).expiry ==
            Expiry::UNAVAILABLE,
        "unavailable claimant expires without waiting for a timer");
  const auto collected = evaluate(true, true, true, 5.9F, 0.2F, 0.1F);
  check(
      collected.expiry == Expiry::NONE &&
          collected.uncontrolledSeconds == 0.0F &&
          collected.unrecoverableSeconds == 0.0F,
      "actual control resets both clocks and wins over a same-frame deadline");
  check(evaluate(true, false, false, 0.0F, 0.0F, -1.0F).uncontrolledSeconds ==
            0.0F,
        "negative elapsed time cannot age a claim");
  check(travelBudget(5.0F, 5.0F, 4.0F, 2.0F) == 10.0F &&
            travelBudget(0.0F, 0.0F, 4.0F, 2.0F) == 0.0F,
        "travel budget respects top-speed caps and an immobile actor");
  if (failures == 0)
  {
    std::cout
        << "PASS: recovery, grace, age-limit and unavailable claim scenarios\n";
  }
  return (failures != 0) ? 1 : 0;
}
