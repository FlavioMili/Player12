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
      9.3f, MatchGround::BOARDS_Y + MatchTuning::Body::RADIUS_METRES};
  const auto gapTarget = RestartApproach::target(stuck, {9.4f, 65.28f}, 0.6f);
  bool throughGap = false;
  for (std::size_t i = 1; i < MatchGround::BOTTOM_BOARDS.size(); ++i)
    throughGap =
        throughGap || (gapTarget.x > MatchGround::BOTTOM_BOARDS[i - 1].to &&
                       gapTarget.x < MatchGround::BOTTOM_BOARDS[i].from);
  check(throughGap && gapTarget.y < MatchGround::BOARDS_Y,
        "restart taker behind a solid board approaches through an actual gap");
  const auto approach =
      RestartApproach::target({50.0f, 50.0f}, {51.0f, 50.0f}, 0.6f);
  check(approach.x > 51.0f && approach.y == 50.0f,
        "restart approach aims through the arrival dead zone without moving "
        "the taker");
  check(
      canRecoverAt(2.0f, 0.0f, 7.0f, 4.0f, 1.0f, 0.0f, 0.9f, 0.0f, 0.3f, true),
      "stationary nearby ball is reachable with acceleration");
  check(
      !canRecoverAt(8.0f, 0.0f, 7.0f, 4.0f, 0.5f, 0.0f, 0.9f, 0.0f, 0.3f, true),
      "ball escaping beyond available travel is not reachable");
  check(canRecoverAt(3.0f, 5.0f, 7.0f, 4.0f, 0.5f, 0.0f, 0.9f, 0.0f, 0.3f,
                     true) &&
            !canRecoverAt(3.0f, -5.0f, 7.0f, 4.0f, 0.5f, 0.0f, 0.9f, 0.0f, 0.3f,
                          true),
        "arrival estimate distinguishes moving toward and away from the ball");
  check(
      !canRecoverAt(0.5f, 0.0f, 7.0f, 4.0f, 0.1f, 0.5f, 0.9f, 0.0f, 0.3f, true),
      "foot recovery prevents an impossible immediate repeat contact");
  check(!canRecoverAt(0.5f, 0.0f, 7.0f, 4.0f, 1.0f, 0.0f, 0.9f, 1.0f, 0.3f,
                      true) &&
            !canRecoverAt(0.5f, 0.0f, 7.0f, 4.0f, 1.0f, 0.0f, 0.9f, 0.0f, 0.3f,
                          false),
        "airborne or off-pitch samples cannot establish foot recovery");
  auto held = evaluate(true, false, true, 0.0f, 0.0f, 0.1f);
  check(held.expiry == Expiry::NONE && held.uncontrolledSeconds > 0.0f,
        "recoverable uncollected claim continues but still ages");
  auto marginal = evaluate(true, false, false, 0.0f, 0.0f, 0.1f);
  check(marginal.expiry == Expiry::NONE,
        "brief forecast miss retains the claim");
  auto regained = evaluate(true, false, true, marginal.uncontrolledSeconds,
                           marginal.unrecoverableSeconds, 0.1f);
  check(regained.unrecoverableSeconds == 0.0f &&
            regained.uncontrolledSeconds > marginal.uncontrolledSeconds,
        "recovery clears only forecast grace, not the continuous uncollected "
        "age");
  auto expired = evaluate(true, false, false, 1.0f, 0.2f, 0.1f);
  check(expired.expiry == Expiry::UNRECOVERABLE,
        "sustained loss of recovery expires the claim");
  check(
      evaluate(true, false, true, 5.9f, 0.0f, 0.1f).expiry == Expiry::AGE_LIMIT,
      "maximum duration bounds a claim even while recovery remains plausible");
  check(evaluate(false, false, true, 0.0f, 0.0f, 0.0f).expiry ==
            Expiry::UNAVAILABLE,
        "unavailable claimant expires without waiting for a timer");
  const auto collected = evaluate(true, true, true, 5.9f, 0.2f, 0.1f);
  check(
      collected.expiry == Expiry::NONE &&
          collected.uncontrolledSeconds == 0.0f &&
          collected.unrecoverableSeconds == 0.0f,
      "actual control resets both clocks and wins over a same-frame deadline");
  check(evaluate(true, false, false, 0.0f, 0.0f, -1.0f).uncontrolledSeconds ==
            0.0f,
        "negative elapsed time cannot age a claim");
  check(travelBudget(5.0f, 5.0f, 4.0f, 2.0f) == 10.0f &&
            travelBudget(0.0f, 0.0f, 4.0f, 2.0f) == 0.0f,
        "travel budget respects top-speed caps and an immobile actor");
  if (!failures)
    std::cout
        << "PASS: recovery, grace, age-limit and unavailable claim scenarios\n";
  return failures ? 1 : 0;
}
