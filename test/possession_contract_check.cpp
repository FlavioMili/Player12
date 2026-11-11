// Dependency-free Stage 1 contract checks; also runnable when GTest is
// unavailable.
#include <iostream>

#include "model/match_possession.h"

namespace
{
int failures = 0;
void check(bool condition, const char* message)
{
  if (!condition)
  {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}
bool has(const PossessionSnapshot& state, PossessionIssue issue)
{
  return (possessionIssues(state) & static_cast<std::uint32_t>(issue)) != 0;
}
}  // namespace

int main()
{
  PossessionSnapshot pass;
  pass.openPlay = true;
  pass.pass = true;
  pass.expectedReceiver = 21;
  pass.legacyLastPossessor = 12;
  auto view = interpretLegacyPossession(pass);
  check(
      view.controller == 0 && view.claimant == 0 && view.expectedReceiver == 21,
      "receiver expectation and last possessor must not create control");
  check(
      view.mode == PossessionMode::UNCONTROLLED && possessionIssues(view) == 0,
      "a travelling pass is a valid uncontrolled ball");
  check(!view.lastPhysicalTouch && !view.lastDeliberatePlay,
        "ambiguous legacy provenance must remain unknown");

  PossessionSnapshot touch;
  touch.openPlay = true;
  touch.legacyOwner = 21;
  touch.ownerKnown = touch.ownerActive = true;
  touch.inTouch = true;
  touch.pendingPasser = 12;
  view = interpretLegacyPossession(touch);
  check(view.claimant == 21 && view.controller == 0 &&
            view.mode == PossessionMode::TOUCH_CLAIM,
        "an uncollected touch claim must not become confirmed control");
  touch.collected = true;
  view = interpretLegacyPossession(touch);
  check(view.controller == 21 && view.claimant == 21 &&
            view.mode == PossessionMode::FOOT_CONTROL,
        "legacy collection evidence supports control while retaining claim "
        "identity");
  check(pass.legacyOwner == 0, "projection must not mutate the source state");

  PossessionSnapshot attached = touch;
  attached.inTouch = attached.collected = false;
  view = interpretLegacyPossession(attached);
  check(view.mode == PossessionMode::LEGACY_ATTACHED && view.controller == 0,
        "an attached legacy owner does not prove physical foot control");
  attached.openPlay = false;
  view = interpretLegacyPossession(attached);
  check(view.mode == PossessionMode::RESTART_OWNER && view.controller == 0,
        "restart entitlement must not become live control");

  auto separated = touch;
  separated.collected = true;
  separated.footControlled = false;
  view = interpretLegacyPossession(separated);
  check(view.mode == PossessionMode::TOUCH_CLAIM && view.controller == 0 &&
            view.claimant == separated.legacyOwner && view.collected,
        "Binturong control loss preserves completed receipt and claimant");
  separated.footControlled = true;
  view = interpretLegacyPossession(separated);
  check(view.mode == PossessionMode::FOOT_CONTROL &&
            view.controller == separated.legacyOwner,
        "Binturong current foot control is independent evidence");

  PossessionSnapshot keeper;
  keeper.openPlay = true;
  keeper.legacyOwner = 1;
  keeper.ownerKnown = keeper.ownerActive = keeper.ownerGoalkeeper = true;
  keeper.ownerInOwnArea = keeper.inHands = true;
  view = interpretLegacyPossession(keeper);
  check(view.mode == PossessionMode::KEEPER_HANDS && view.controller == 1 &&
            possessionIssues(view) == 0,
        "a valid keeper catch supports hand control");
  keeper.ownerInOwnArea = false;
  check(has(keeper, PossessionIssue::HANDS_OUTSIDE_AREA),
        "illegal hand-area state is observable");
  keeper.ownerGoalkeeper = false;
  check(has(keeper, PossessionIssue::HANDS_NOT_KEEPER),
        "outfielder hand control is observable");
  keeper.inTouch = true;
  check(has(keeper, PossessionIssue::HANDS_AND_TOUCH),
        "hands and rolling touch are incompatible");

  touch.legacyOwner = 0;
  check(has(touch, PossessionIssue::TOUCH_WITHOUT_OWNER),
        "ownerless claim is observable");
  touch.inTouch = false;
  touch.dropping = true;
  check(has(touch, PossessionIssue::COLLECTION_WITHOUT_TOUCH) &&
            has(touch, PossessionIssue::DROPPING_WITHOUT_TOUCH),
        "stale collection/drop flags are observable");
  pass.meetFor = 21;
  check(has(pass, PossessionIssue::MEET_OWNER_MISMATCH),
        "stale meet plan is observable");
  pass.shot = true;
  check(has(pass, PossessionIssue::PASS_AND_SHOT),
        "conflicting action flags are observable");
  attached.ownerActive = false;
  check(has(attached, PossessionIssue::OWNER_INACTIVE),
        "inactive owner is observable");

  if (failures == 0)
    std::cout << "PASS: possession projection and invariant contracts\n";
  return failures == 0 ? 0 : 1;
}
