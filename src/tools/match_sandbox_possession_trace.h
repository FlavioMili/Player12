#pragma once

#include <iosfwd>

class MatchEngine;

/** JSONL diagnostics plus per-tick recorder-on/off equivalence. The supplied
 * engines must start from the same setup and have no attached recorder. */
int tracePossessionPair(MatchEngine& observed, MatchEngine& reference,
                        double seconds, std::ostream& output);
