// Explicit Binturong foot-control policy scenarios. No mutable engine test
// seam.
#include <cmath>
#include <iostream>

#include "model/match_pitch.h"
#include "model/match_possession_control.h"

namespace
{
// Tests play on a match pitch, placed by fractions of it.
const MatchPitch& PITCH = MatchPitch::matchSize();
}  // namespace

namespace
{
int failures = 0;
void check(bool condition, const char* message)
{
  if (!condition)
  {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
}  // namespace

int main()
{
  using namespace PossessionControl;
  const PitchPoint partlyOverLine = PITCH.at(0.36F, -0.00037F);
  const auto physical = retainingTouchPosition(
      MatchPitch{}, PossessionId::BINTURONG, partlyOverLine);
  const auto legacy = retainingTouchPosition(
      MatchPitch{}, PossessionId::AARDVARK, partlyOverLine);
  check(physical.x == partlyOverLine.x && physical.y == partlyOverLine.y &&
            legacy.x == partlyOverLine.x && legacy.y == PITCH.atY(0.001F),
        "playable boundary touch retains its actual point; Aardvark keeps its "
        "legacy inset");
  const auto ordinary = retainingTouchPosition(
      MatchPitch{}, PossessionId::BINTURONG, PITCH.at(0.4F, 0.6F));
  check(ordinary.x == PITCH.atX(0.4F) && ordinary.y == PITCH.atY(0.6F),
        "ordinary retaining touch leaves the contact point unchanged");
  Evidence atFeet{.playerAvailable = true,
                  .gapMetres = 0.4F,
                  .predictedGapMetres = 0.5F,
                  .relativeSpeed = 0.4F,
                  .heightMetres = 0.0F,
                  .feetHeightMetres = 0.35F,
                  .collectionReachMetres = 0.9F,
                  .collectionRelativeSpeed = 1.5F};
  const auto collection = evaluate(false, 0.0F, atFeet, 0.1F);
  check(
      collection.collected && collection.newlyCollected,
      "claimed ball at foot reach and manageable relative speed is collected");
  check(!evaluate(true, 0.0F, atFeet, 0.1F).newlyCollected,
        "retention does not repeat collection credit");

  auto fast = atFeet;
  fast.relativeSpeed = 4.0F;
  check(
      !evaluate(false, 0.0F, fast, 0.1F).collected,
      "nearby ball travelling too fast relative to receiver is not collected");
  auto dropping = atFeet;
  dropping.heightMetres = 0.8F;
  check(!evaluate(false, 0.0F, dropping, 0.1F).collected,
        "chest/thigh touch must drop to foot height before collection");
  auto normalDribble = atFeet;
  normalDribble.gapMetres = 1.8F;
  normalDribble.predictedGapMetres = 2.2F;
  normalDribble.relativeSpeed = 3.0F;
  check(evaluate(true, 0.1F, normalDribble, 0.1F).collected &&
            evaluate(true, 0.1F, normalDribble, 0.1F).separationSeconds == 0.0F,
        "normal dribble retains control beyond strict collection reach and "
        "resets tolerance");
  check(!evaluate(false, 0.0F, normalDribble, 0.1F).collected,
        "being inside the retention envelope cannot establish fresh control");

  auto marginal = normalDribble;
  marginal.gapMetres = 3.2F;
  marginal.predictedGapMetres = 3.4F;
  const auto brief = evaluate(true, 0.0F, marginal, 0.1F);
  check(brief.collected && std::abs(brief.separationSeconds - 0.1F) < 1e-6F,
        "one marginal sample retains control under brief tolerance");
  check(!evaluate(true, brief.separationSeconds, marginal, 0.1F).collected,
        "sustained marginal separation ends control at the tolerance limit");
  const auto recovered =
      evaluate(true, brief.separationSeconds, normalDribble, 0.1F);
  check(
      recovered.collected && recovered.separationSeconds == 0.0F &&
          evaluate(true, recovered.separationSeconds, marginal, 0.1F).collected,
      "regaining the envelope resets tolerance instead of accumulating "
      "unrelated excursions");
  check(evaluate(true, 0.0F, marginal, 0.0F).collected &&
            evaluate(true, 0.0F, marginal, -0.1F).separationSeconds == 0.0F,
        "zero/negative elapsed time does not age a marginal separation");
  check(!evaluate(true, 0.0F, marginal, 0.2F).collected,
        "split and unsplit samples reach the same tolerance boundary");

  auto far = normalDribble;
  far.gapMetres = 5.0F;
  check(!evaluate(true, 0.0F, far, 0.0F).collected,
        "clear current separation ends control immediately");
  auto escaping = atFeet;
  escaping.predictedGapMetres = 6.0F;
  escaping.relativeSpeed = 16.0F;
  check(!evaluate(true, 0.0F, escaping, 0.0F).collected,
        "fast outward trajectory beyond the control horizon ends control "
        "immediately");
  auto rising = atFeet;
  rising.heightMetres = 1.2F;
  check(!evaluate(true, 0.0F, rising, 0.0F).collected,
        "ball clearly above foot-control height ends control immediately");
  auto unavailable = atFeet;
  unavailable.playerAvailable = false;
  check(!evaluate(true, 0.0F, unavailable, 0.0F).collected &&
            !evaluate(false, 0.0F, unavailable, 0.1F).collected,
        "fallen/inactive actor cannot retain or establish foot control");
  check(
      evaluate(false, 0.2F, atFeet, 0.1F).newlyCollected &&
          evaluate(false, 0.2F, atFeet, 0.1F).separationSeconds == 0.0F,
      "actual recollection establishes control with a fresh tolerance window");
  if (failures == 0)
  {
    std::cout << "PASS: foot-control acquisition, retention, loss and "
                 "recollection scenarios\n";
  }
  return (failures != 0) ? 1 : 0;
}
