// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <imgui.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "global/stats_config.h"
#include "global/types.h"
#include "model/match_engine.h"
#include "model/player.h"
#include "tools/match_sandbox_recorder.h"

enum class DrillCategory
{
  Movement, Passing, Shooting, Control, Duels, Aerial, Goalkeeping, Count
};

inline const char* drillCategoryName(DrillCategory category)
{
  switch (category)
  {
    case DrillCategory::Movement: return "MOVEMENT";
    case DrillCategory::Passing: return "PASSING";
    case DrillCategory::Shooting: return "SHOOTING";
    case DrillCategory::Control: return "CONTROL";
    case DrillCategory::Duels: return "DUELS";
    case DrillCategory::Aerial: return "AERIAL";
    case DrillCategory::Goalkeeping: return "GOALKEEPING";
    case DrillCategory::Count: break;
  }
  return "DRILLS";
}

class IMatchRenderer;
class Player;

/**
 * Drills: minigames that isolate one behaviour of the match engine (see
 * Notes and plans/match_engine_drills_plan.md). A drill is an ordinary
 * engine run with drill rules, only its own players on the pitch and a
 * script that steers it after every step, so the debugger tools work on it
 * unchanged.
 */

/** One player a drill needs. */
struct DrillPlayerSpec
{
  std::string name;
  PlayerRole role = PlayerRole::CM;
  bool homeTeam = true;
  /** Engine attributes by name ("Pace", "Shooting"…), 0-100; any not given
   * take the reference value. */
  std::map<std::string, float> stats;
  /** Height in centimetres. */
  int height = 180;
  /** Derived stats overridden for this player, 0-100 (match_derived_stats.h);
   * any not given come from their recipes. */
  std::map<DerivedStat, float> derived;
  Foot foot = Foot::Right;
  /** Condition at the start, 0-100 (100: fresh). */
  float condition = 100.0f;
};

/** One setting of a drill (also what a measure sweep can vary). */
struct DrillParameter
{
  const char* name = "";
  float* value = nullptr;
  float min = 0.0f;
  float max = 1.0f;
  /** Shown and stored as whole numbers. */
  bool integer = false;
  const char* tooltip = nullptr;
  /** Named choices (the value is the index), e.g. who runs. */
  std::vector<const char*> choices;
  /** A heading shown above this setting (and the ones after it). */
  const char* section = nullptr;
  /** Its section is a header that opens and closes (closed at first),
   * for long lists such as tuning knobs. */
  bool collapsible = false;
  /** Shown instead of the name, which stays what presets, sweeps and
   * --set use (e.g. a knob shown without its kind). */
  const char* label = nullptr;
  /** When set, a value of 0 means set automatically: the slider shows what
   * this returns, in the warning colour, and moving it gives the setting
   * its own value (back to 0 is automatic again). */
  std::function<float()> automatic;
};

/** A named set of a drill's settings (a situation to start from); settings
 * it leaves out keep their values. */
struct DrillPreset
{
  std::string name;
  std::string summary;
  std::map<std::string, float> settings;
};

/** One numeric result of a drill run, e.g. "Finish (s)". */
struct DrillMetric
{
  std::string name;
  double value = 0.0;
};

/** A drill: its settings, its players, and the script of one run. */
class Drill
{
 public:
  virtual ~Drill() = default;
  // Catalogue metadata assigned explicitly by makeDrills, independent of names.
  DrillCategory category = DrillCategory::Movement;

  [[nodiscard]] virtual const char* name() const = 0;
  [[nodiscard]] virtual const char* summary() const = 0;
  /** The drill's settings, read and written through their pointers. */
  [[nodiscard]] virtual std::vector<DrillParameter> parameters() = 0;
  /** Named situations to start from (Watch: a picker; Measure: --preset). */
  [[nodiscard]] virtual std::vector<DrillPreset> presets() const { return {}; }
  /** Sets the preset's settings; false for an unknown preset name. */
  bool applyPreset(const std::string& name);
  /** Settings widgets: presets, then parameters as sliders or choices.
   * Two columns place labels above controls and retain section headers.
   * includeSeed adds seed entry; stackedLabels also puts the situation picker
   * and single-column labels above controls. Other views retain their default
   * layout (ctrl+click a slider to type a value beyond its range). */
  void renderSettings(int columns = 1, bool includeSeed = false,
                      bool stackedLabels = false);
  /** Shown below the settings (after the knobs): e.g. how hard the last
   * run's strike was. */
  virtual void renderAfterSettings() const {}
  /** The players this drill needs (their ids follow, in this order). */
  [[nodiscard]] virtual std::vector<DrillPlayerSpec> players() const = 0;
  /** Sets a run up on a fresh engine: positions, ball, first commands. */
  virtual void begin(MatchEngine& engine,
                     const std::vector<PlayerID>& ids) = 0;
  /** Before every step: input for a scripted controlled player. */
  virtual void beforeStep(MatchEngine& /*engine*/) {}
  /** After every step: steers the run; false once it is over. */
  virtual bool afterStep(MatchEngine& engine) = 0;
  /** Whether the drill has a shot map (a grid of spots, heatmap). */
  [[nodiscard]] virtual bool offersShotMap() const { return false; }
  /** Whether you play the drill's controlled player (keyboard, gamepad). */
  [[nodiscard]] virtual bool humanControl() const { return false; }
  /** The pitch the drill is played on: the match pitch, or a training pitch
   * (MatchPitch::training) for a drill on its own patch of grass. */
  [[nodiscard]] virtual MatchPitch pitch() const { return MatchPitch{}; }
  /** Markers on the pitch (cordon, lines, flags) in the view's projection. */
  virtual void drawOverlay(const IMatchRenderer& /*renderer*/,
                           ImDrawList& /*draw*/) const
  {
  }
  /** Progress and results of the current run, one line each. */
  [[nodiscard]] virtual std::vector<std::string> report() const = 0;
  /** Results of a finished run as numbers (Measure mode); empty while the
   * run is unfinished or did not complete. */
  [[nodiscard]] virtual std::vector<DrillMetric> metrics() const = 0;

  /** Seed of the engine (and of anything random in the drill). */
  std::uint32_t seed = 1;
};

/**
 * One run of a drill: synthetic squads (uniform reference players plus the
 * drill's own), an engine with drill rules and everyone else removed, and a
 * recorder for the debugger. Owns the players the engine borrows.
 */
class DrillRun
{
 public:
  /** Reference attribute value of the players that fill the squads. */
  static constexpr float REFERENCE_RATING = 70.0f;

  /** `record`: attach the recorder (the debugger needs it; Measure runs
   * do without). `prepare`: called on the engine once the drill has set the
   * run up (the technique bench sets its tuning there, over the drill's). */
  DrillRun(Drill& drill, const StatsConfig& config, bool record = true,
           const std::function<void(MatchEngine&)>& prepare = {});
  ~DrillRun();
  DrillRun(const DrillRun&) = delete;
  DrillRun& operator=(const DrillRun&) = delete;

  /** One fixed step and the drill's script; false once the run is over. */
  bool step();
  [[nodiscard]] bool finished() const { return over; }

  [[nodiscard]] MatchEngine& engine() { return *match; }
  [[nodiscard]] SandboxRecorder& recorder() { return *log; }
  [[nodiscard]] const std::vector<PlayerID>& ids() const { return drillIds; }

 private:
  Drill& drill;
  std::vector<std::unique_ptr<Player>> pool;
  std::vector<PlayerID> drillIds;
  std::unique_ptr<SandboxRecorder> log;
  std::unique_ptr<MatchEngine> match;
  bool over = false;
};

/** The drills offered by the sandbox, in menu order. */
std::vector<std::unique_ptr<Drill>> makeDrills();

/** Drawing helpers for drill overlays (pitch coordinates → screen). */
namespace DrillDraw
{
bool project(const IMatchRenderer& renderer, PitchPoint pitch, ImVec2& screen);
void line(const IMatchRenderer& renderer, ImDrawList& draw, PitchPoint from,
          PitchPoint to, ImU32 color, float thickness);
void label(const IMatchRenderer& renderer, ImDrawList& draw, PitchPoint at,
           const char* text, ImU32 color);
/** An area of the pitch (a cordon), as a rectangle. */
void area(const IMatchRenderer& renderer, ImDrawList& draw, PitchPoint min,
          PitchPoint max, ImU32 color, float thickness);
/** A round marker (flag, spot) with an optional label beside it. */
void marker(const IMatchRenderer& renderer, ImDrawList& draw, PitchPoint at,
            float radius, ImU32 color, const char* text = nullptr);
/** A training cone's radius (m): 20 cm across. */
inline constexpr float CONE_RADIUS_METRES = 0.1f;
/** A training cone, drawn to scale (CONE_RADIUS_METRES), with an optional
 * label beside it. */
void cone(const IMatchRenderer& renderer, ImDrawList& draw, PitchPoint at,
          ImU32 color, const char* text = nullptr);
/** A path through pitch points. */
void path(const IMatchRenderer& renderer, ImDrawList& draw,
          const std::vector<PitchPoint>& points, ImU32 color, float thickness);
/** A circle on the ground round `centre`, `radiusMetres` across the pitch
 * as drawn: filled with `fill` (0: not filled) and outlined in `outline`
 * (0: no outline). */
void circle(const IMatchRenderer& renderer, ImDrawList& draw, PitchPoint centre,
            float radiusMetres, ImU32 fill, ImU32 outline, float thickness);
}  // namespace DrillDraw
