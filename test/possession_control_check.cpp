// Explicit Binturong foot-control policy scenarios. No mutable engine test
// seam.
#include <cmath>
#include <iostream>

#include "model/match_possession_control.h"

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
  const PitchPoint partlyOverLine{0.36f, -0.00037f};
  const auto physical =
      retainingTouchPosition(PossessionId::BINTURONG, partlyOverLine);
  const auto legacy =
      retainingTouchPosition(PossessionId::AARDVARK, partlyOverLine);
  check(physical.x == partlyOverLine.x && physical.y == partlyOverLine.y &&
            legacy.x == partlyOverLine.x && legacy.y == 0.001f,
        "playable boundary touch retains its actual point; Aardvark keeps its "
        "legacy inset");
  const auto ordinary =
      retainingTouchPosition(PossessionId::BINTURONG, PitchPoint{0.4f, 0.6f});
  check(ordinary.x == 0.4f && ordinary.y == 0.6f,
        "ordinary retaining touch leaves the contact point unchanged");
  Evidence atFeet{true, 0.4f, 0.5f, 0.4f, 0.0f, 0.35f, 0.9f, 1.5f};
  const auto collection = evaluate(false, 0.0f, atFeet, 0.1f);
  check(
      collection.collected && collection.newlyCollected,
      "claimed ball at foot reach and manageable relative speed is collected");
  check(!evaluate(true, 0.0f, atFeet, 0.1f).newlyCollected,
        "retention does not repeat collection credit");

  auto fast = atFeet;
  fast.relativeSpeed = 4.0f;
  check(
      !evaluate(false, 0.0f, fast, 0.1f).collected,
      "nearby ball travelling too fast relative to receiver is not collected");
  auto dropping = atFeet;
  dropping.heightMetres = 0.8f;
  check(!evaluate(false, 0.0f, dropping, 0.1f).collected,
        "chest/thigh touch must drop to foot height before collection");
  auto normalDribble = atFeet;
  normalDribble.gapMetres = 1.8f;
  normalDribble.predictedGapMetres = 2.2f;
  normalDribble.relativeSpeed = 3.0f;
  check(evaluate(true, 0.1f, normalDribble, 0.1f).collected &&
            evaluate(true, 0.1f, normalDribble, 0.1f).separationSeconds == 0.0f,
        "normal dribble retains control beyond strict collection reach and "
        "resets tolerance");
  check(!evaluate(false, 0.0f, normalDribble, 0.1f).collected,
        "being inside the retention envelope cannot establish fresh control");

  auto marginal = normalDribble;
  marginal.gapMetres = 3.2f;
  marginal.predictedGapMetres = 3.4f;
  const auto brief = evaluate(true, 0.0f, marginal, 0.1f);
  check(brief.collected && std::abs(brief.separationSeconds - 0.1f) < 1e-6f,
        "one marginal sample retains control under brief tolerance");
  check(!evaluate(true, brief.separationSeconds, marginal, 0.1f).collected,
        "sustained marginal separation ends control at the tolerance limit");
  const auto recovered =
      evaluate(true, brief.separationSeconds, normalDribble, 0.1f);
  check(
      recovered.collected && recovered.separationSeconds == 0.0f &&
          evaluate(true, recovered.separationSeconds, marginal, 0.1f).collected,
      "regaining the envelope resets tolerance instead of accumulating "
      "unrelated excursions");
  check(evaluate(true, 0.0f, marginal, 0.0f).collected &&
            evaluate(true, 0.0f, marginal, -0.1f).separationSeconds == 0.0f,
        "zero/negative elapsed time does not age a marginal separation");
  check(!evaluate(true, 0.0f, marginal, 0.2f).collected,
        "split and unsplit samples reach the same tolerance boundary");

  auto far = normalDribble;
  far.gapMetres = 5.0f;
  check(!evaluate(true, 0.0f, far, 0.0f).collected,
        "clear current separation ends control immediately");
  auto escaping = atFeet;
  escaping.predictedGapMetres = 6.0f;
  escaping.relativeSpeed = 16.0f;
  check(!evaluate(true, 0.0f, escaping, 0.0f).collected,
        "fast outward trajectory beyond the control horizon ends control "
        "immediately");
  auto rising = atFeet;
  rising.heightMetres = 1.2f;
  check(!evaluate(true, 0.0f, rising, 0.0f).collected,
        "ball clearly above foot-control height ends control immediately");
  auto unavailable = atFeet;
  unavailable.playerAvailable = false;
  check(!evaluate(true, 0.0f, unavailable, 0.0f).collected &&
            !evaluate(false, 0.0f, unavailable, 0.1f).collected,
        "fallen/inactive actor cannot retain or establish foot control");
  check(
      evaluate(false, 0.2f, atFeet, 0.1f).newlyCollected &&
          evaluate(false, 0.2f, atFeet, 0.1f).separationSeconds == 0.0f,
      "actual recollection establishes control with a fresh tolerance window");
  if (!failures)
    std::cout << "PASS: foot-control acquisition, retention, loss and "
                 "recollection scenarios\n";
  return failures ? 1 : 0;
}
