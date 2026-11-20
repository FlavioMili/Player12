// Football Management Project — Copyright (c) 2025 - 2026 Flavio Milinanni.
// See LICENSE in the project root.
#include <iostream>

#include "model/match_possession_coordination.h"

int main()
{
  using namespace PossessionCoordination;
  int failures = 0;
  const auto check = [&](bool good, const char* message)
  {
    if (!good)
    {
      ++failures;
      std::cerr << "FAIL: " << message << '\n';
    }
  };
  const float forward = quality({.x = 7, .y = 0}, {.x = 10, .y = 0}, true, 8);
  const float backward = quality({.x = -7, .y = 0}, {.x = 10, .y = 0}, true, 8);
  check(forward > backward &&
            forward > quality({.x = 0, .y = 0}, {.x = 10, .y = 0}, true, 8),
        "running into attack with the ball beats standing or chasing backward");
  check(forward == quality({.x = -7, .y = 0}, {.x = -10, .y = 0}, false, 8),
        "attacking direction mirrors across teams");
  check(forward > quality({.x = 7, .y = 0}, {.x = 10, .y = 0}, true, 0),
        "attacking into open space beats the same run into an opponent");
  const Candidate early{.arrivalSeconds = 1.0F,
                        .collectionQuality = 0.0F,
                        .intendedReceiver = false,
                        .id = 10};
  const Candidate attacking{.arrivalSeconds = 1.05F,
                            .collectionQuality = 1.0F,
                            .intendedReceiver = false,
                            .id = 20};
  check(better(attacking, early),
        "fractionally earlier player yields to better collection");
  check(better(early, {.arrivalSeconds = 1.2F,
                       .collectionQuality = 1.0F,
                       .intendedReceiver = true,
                       .id = 20}),
        "neither quality nor receiver overrides a clearly earlier arrival");
  check(better({.arrivalSeconds = 1,
                .collectionQuality = 0,
                .intendedReceiver = true,
                .id = 20},
               early),
        "receiver breaks an otherwise equal choice");
  check(better(early, {.arrivalSeconds = 1.02F,
                       .collectionQuality = 0,
                       .intendedReceiver = true,
                       .id = 20}),
        "receiver preference is only a tie break");
  check(better(early, {.arrivalSeconds = 1,
                       .collectionQuality = 0,
                       .intendedReceiver = false,
                       .id = 20}),
        "stable player identity resolves the final tie");
  check(offsideCollector(false, false, {20}, 20) &&
            !offsideCollector(true, false, {20}, 30) &&
            offsideCollector(true, true, {}, 10) &&
            !offsideCollector(false, false, {20}, 40),
        "release judgement follows collector identity, not receiver or "
        "substitute identity");

  struct Contact
  {
    Candidate candidate;
    bool home;
    bool physicalBody = false;
  };
  auto eligible = [](const Contact& c) { return !c.physicalBody; };
  auto sameTeam = [](const Contact& a, const Contact& b)
  { return a.home == b.home; };
  auto candidate = [](const Contact& c) { return c.candidate; };
  std::array<Contact, 4> contacts{{{.candidate = early, .home = true},
                                   {.candidate = attacking, .home = true},
                                   {.candidate = {.arrivalSeconds = 1.06F,
                                                  .collectionQuality = 0,
                                                  .intendedReceiver = false,
                                                  .id = 30},
                                    .home = false},
                                   {.candidate = {.arrivalSeconds = 1.07F,
                                                  .collectionQuality = 1,
                                                  .intendedReceiver = true,
                                                  .id = 40},
                                    .home = true}}};
  prioritise(contacts, contacts.size(), eligible, sameTeam, candidate);
  check(contacts[0].candidate.id == 20 && contacts[1].candidate.id == 10,
        "playable teammates coordinate; declined preferred touch has fallback");
  check(contacts[2].candidate.id == 30 && contacts[3].candidate.id == 40,
        "coordination cannot move a teammate ahead of an earlier opponent");
  std::array<Contact, 3> collision{{{.candidate = early, .home = true},
                                    {.candidate = {.arrivalSeconds = 1.01F,
                                                   .collectionQuality = 0,
                                                   .intendedReceiver = false,
                                                   .id = 30},
                                     .home = true,
                                     .physicalBody = true},
                                    {.candidate = attacking, .home = true}}};
  prioritise(collision, collision.size(), eligible, sameTeam, candidate);
  check(collision[0].candidate.id == 10 && collision[1].candidate.id == 30,
        "physical body collision remains before a later preferred touch");
  if (failures == 0)
  {
    std::cout << "PASS: teammate arrival, collection quality, receiver ties "
                 "and physical ordering\n";
  }
  return (failures != 0) ? 1 : 0;
}
