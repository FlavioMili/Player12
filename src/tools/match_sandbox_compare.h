// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include "tools/match_sandbox_detail.h"
#include "tools/match_sandbox_recording.h"

class DebugInspector;
class MatchReview;
class SandboxRecorder;

/**
 * The match sandbox's embedded recording comparison (debugger phase 5): how a
 * recording replays on the current engine. When it plays differently, the first
 * tick and the first decision that differ, and that decision's scores side by
 * side, term by term (when the recording was saved with full detail).
 */
class RecordingComparison
{
 public:
  /** `replayed` is the replayed match (the review's recorder). */
  /** Returns true when a comparison action seeks the review pitch. */
  bool render(const MatchRecording& recording, const Recordings::Replay& replay,
              const SandboxRecorder* replayed, MatchReview& review,
              DebugInspector& inspector);
  void reset() { replayDetail.reset(); }

 private:
  void renderDecisionDiff(const MatchRecording& recording,
                          const Recordings::Replay& replay,
                          const SandboxRecorder* replayed);

  /** Full detail of the replay around its first different decision. */
  DetailSession replayDetail;
};
