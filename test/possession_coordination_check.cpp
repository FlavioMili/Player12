// Football Management Project — Copyright (c) 2025 - 2026 Flavio Milinanni.
// See LICENSE in the project root.
#include <iostream>
#include "model/match_possession_coordination.h"

int main()
{
  using namespace PossessionCoordination;
  int failures = 0;
  const auto check = [&](bool good, const char* message)
  { if (!good) { ++failures; std::cerr << "FAIL: " << message << '\n'; } };
  const float forward = quality({7, 0}, {10, 0}, true, 8);
  const float backward = quality({-7, 0}, {10, 0}, true, 8);
  check(forward > backward && forward > quality({0, 0}, {10, 0}, true, 8),
        "running into attack with the ball beats standing or chasing backward");
  check(forward == quality({-7, 0}, {-10, 0}, false, 8),
        "attacking direction mirrors across teams");
  check(forward > quality({7, 0}, {10, 0}, true, 0),
        "attacking into open space beats the same run into an opponent");
  const Candidate early{1.0f, 0.0f, false, 10};
  const Candidate attacking{1.05f, 1.0f, false, 20};
  check(better(attacking, early), "fractionally earlier player yields to better collection");
  check(better(early, {1.2f, 1.0f, true, 20}),
        "neither quality nor receiver overrides a clearly earlier arrival");
  check(better({1, 0, true, 20}, early), "receiver breaks an otherwise equal choice");
  check(better(early, {1.02f, 0, true, 20}), "receiver preference is only a tie break");
  check(better(early, {1, 0, false, 20}), "stable player identity resolves the final tie");
  check(offsideCollector(false, false, {20}, 20) &&
            !offsideCollector(true, false, {20}, 30) &&
            offsideCollector(true, true, {}, 10) &&
            !offsideCollector(false, false, {20}, 40),
        "release judgement follows collector identity, not receiver or substitute identity");

  struct Contact { Candidate candidate; bool home; bool physicalBody = false; };
  auto eligible = [](const Contact& c) { return !c.physicalBody; };
  auto sameTeam = [](const Contact& a, const Contact& b) { return a.home == b.home; };
  auto candidate = [](const Contact& c) { return c.candidate; };
  std::array<Contact, 4> contacts{{{early, true}, {attacking, true},
                                 {{1.06f, 0, false, 30}, false},
                                 {{1.07f, 1, true, 40}, true}}};
  prioritise(contacts, contacts.size(), eligible, sameTeam, candidate);
  check(contacts[0].candidate.id == 20 && contacts[1].candidate.id == 10,
        "playable teammates coordinate; declined preferred touch has fallback");
  check(contacts[2].candidate.id == 30 && contacts[3].candidate.id == 40,
        "coordination cannot move a teammate ahead of an earlier opponent");
  std::array<Contact, 3> collision{{{early, true},
                                  {{1.01f, 0, false, 30}, true, true},
                                  {attacking, true}}};
  prioritise(collision, collision.size(), eligible, sameTeam, candidate);
  check(collision[0].candidate.id == 10 && collision[1].candidate.id == 30,
        "physical body collision remains before a later preferred touch");
  if (!failures) std::cout << "PASS: teammate arrival, collection quality, receiver ties and physical ordering\n";
  return failures ? 1 : 0;
}
