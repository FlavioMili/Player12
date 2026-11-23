// Football Management Project - see LICENSE in the project root.
#pragma once

#include <stop_token>

/** Unwinds an unpublished world and its database transaction. */
struct WorldGenerationCancelled
{
};

inline void checkWorldGenerationStop(std::stop_token stop)
{
  if (stop.stop_requested()) throw WorldGenerationCancelled{};
}
