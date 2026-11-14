// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/render/match_renderer_3d.h"

#include <fmt/printf.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <span>
#include <string>
#include <vector>

#include "global/language_manager.h"
#include "gui/render/match_camera_3d.h"
#include "gui/render/match_kit_colors.h"
#include "gui/render/match_player_rig.h"
#include "gui/render/match_render_3d_tuning.h"
#include "gui/render/match_render_math.h"
#include "gui/render/match_renderer_2d.h"
#include "gui/render/match_shirt_numbers.h"
#include "gui/render/match_stadium_3d.h"
#include "gui/view_models/match_clock.h"
#include "gui/widgets/theme.h"
#include "model/player.h"
#include "model/role_utils.h"

namespace
{
using RenderMath::ScreenPoint;
using RenderMath::Vec3;
using RenderMath::Vec4;
using Tuning = MatchRender3DTuning;

constexpr float LENGTH = MatchTuning::Pitch::MATCH_LENGTH_METRES;
constexpr float WIDTH = MatchTuning::Pitch::MATCH_WIDTH_METRES;
constexpr Vec3 UP{0.0f, 0.0f, 1.0f};
constexpr float TWO_PI = 2.0f * std::numbers::pi_v<float>;
constexpr std::size_t MAX_POLYGON_POINTS = 13;
constexpr int BATCH_CHUNK_VERTICES = 2048;
/** Spectator heads: a hexagon (across, up) on the unit circle. */
constexpr std::size_t HEAD_SIDES = 6;
constexpr std::array<std::array<float, 2>, HEAD_SIDES> HEAD_SHAPE{{
    {1.0f, 0.0f},
    {0.5f, 0.866f},
    {-0.5f, 0.866f},
    {-1.0f, 0.0f},
    {-0.5f, -0.866f},
    {0.5f, -0.866f},
}};

enum class PrimitiveKind : std::uint8_t
{
  POLYGON,
  LINE,
  HEAD,
  BALL,
  NET,
  BOARD,
  TEXT,
};

/** Screen-space primitive waiting for the back-to-front sort. */
struct Primitive
{
  std::uint32_t firstPoint = 0;
  std::uint16_t pointCount = 0;
  PrimitiveKind kind = PrimitiveKind::POLYGON;
  ImU32 color = 0;
  ImU32 secondaryColor = 0;
  float size = 0.0f;
  std::uint32_t reference = 0;
};

struct SortKey
{
  float objectDepth = 0.0f;
  float localDepth = 0.0f;
  std::uint32_t primitive = 0;
};

struct ClipVertex
{
  Vec4 clip;
  ImU32 color = 0;
};

/** Hair styles drawn on the head disc. */
enum class HairStyle : std::uint8_t
{
  SHORT,
  BUZZ,
  VOLUME,
  LONG,
  SHAVED,
};

/** Head primitives: low bits hold the hair style, then a beard flag; the
 * facing towards the camera (0..255) sits in bits 8-15. */
constexpr std::uint32_t HEAD_STYLE_MASK = 0x0FU;
constexpr std::uint32_t HEAD_BEARD_BIT = 0x10U;
constexpr std::uint32_t HEAD_FACING_SHIFT = 8U;

/** A player's looks, fixed by his id. */
struct PlayerLook
{
  ImU32 skin = 0;
  ImU32 hair = 0;
  ImU32 boots = 0;
  HairStyle hairStyle = HairStyle::SHORT;
  bool beard = false;
  /** Width factor of the build. */
  float bulk = 1.0f;
  std::uint32_t seed = 0;
};

/** How much of a player is modelled, by projected height. */
enum class PlayerLod : std::uint8_t
{
  FAR,
  MID,
  NEAR,
};

/** Per-snapshot-slot animation memory. */
struct AnimationSlot
{
  const Player* player = nullptr;
  PlayerLook looks;
  Vec3 lastPosition;
  float phase = 0.0f;
  float stride = 0.0f;
  /** Ground speed seen last frame and the smoothed acceleration (m/s^2). */
  float lastSpeed = 0.0f;
  float acceleration = 0.0f;
  /** Smoothed ground speed choosing the walk / jog / sprint blend. */
  float gaitSpeed = 0.0f;
  float lastYaw = 0.0f;
  bool hasYaw = false;
  /** Smoothed turn rate (rad per simulated second, positive = left). */
  float turnRate = 0.0f;
  /** Smoothed direction of travel (unit, horizontal). */
  Vec3 moveDirection{1.0f, 0.0f, 0.0f};
  /** Planted or swinging feet (index 0 is the left foot). */
  std::array<PlayerRig::FootState, 2> feet{};
  /** Gait of the last update: stance share and stance length (m). */
  float duty = 0.5f;
  float stanceLength = 0.0f;
  /** Timed ball action, simulated seconds into it, where the ball was met
   * and where it went, and the acting leg. */
  PlayerRig::Event event = PlayerRig::Event::NONE;
  float eventSeconds = 0.0f;
  Vec3 contact;
  Vec3 eventDirection{1.0f, 0.0f, 0.0f};
  std::size_t eventLeg = 1;
  float lastTackleCooldown = 0.0f;
  /** Smoothed jump towards a high ball (m). */
  float jump = 0.0f;
  bool leftFooted = false;
  /** Hands to the head when the other side scores. */
  bool handsOnHead = false;
  /** Keeper pose blends (0..1) and the side of the current dive. */
  float setBlend = 0.0f;
  float holdBlend = 0.0f;
  float diveBlend = 0.0f;
  float diveSide = 1.0f;
  /** Printed on the back: shirt number and upper-case surname. */
  int number = 0;
  std::string backName;
};

/**
 * Joint angles of one frame, blended from the run cycle and the overlays
 * (kick, keeper stances, celebrations). Index 0 is the player's left side.
 */
struct Pose
{
  float lean = 0.0f;
  /** Shoulders turned against the hips (radians, positive = left). */
  float twist = 0.0f;
  float turnRoll = 0.0f;
  float bodyRoll = 0.0f;
  float hipDrop = 0.0f;
  float lift = 0.0f;
  std::array<float, 2> thigh{};
  std::array<float, 2> knee{};
  std::array<float, 2> legSpread{};
  std::array<float, 2> armSwing{};
  std::array<float, 2> armSpread{};
  std::array<float, 2> elbow{};
};

Pose mixPose(const Pose& from, const Pose& to, float t)
{
  if (t <= 0.0f) return from;
  if (t >= 1.0f) return to;
  const auto mix = [t](float a, float b) { return a + (b - a) * t; };
  Pose result;
  result.lean = mix(from.lean, to.lean);
  result.twist = mix(from.twist, to.twist);
  result.turnRoll = mix(from.turnRoll, to.turnRoll);
  result.bodyRoll = mix(from.bodyRoll, to.bodyRoll);
  result.hipDrop = mix(from.hipDrop, to.hipDrop);
  result.lift = mix(from.lift, to.lift);
  for (std::size_t index = 0; index < 2; ++index)
  {
    result.thigh[index] = mix(from.thigh[index], to.thigh[index]);
    result.knee[index] = mix(from.knee[index], to.knee[index]);
    result.legSpread[index] = mix(from.legSpread[index], to.legSpread[index]);
    result.armSwing[index] = mix(from.armSwing[index], to.armSwing[index]);
    result.armSpread[index] = mix(from.armSpread[index], to.armSpread[index]);
    result.elbow[index] = mix(from.elbow[index], to.elbow[index]);
  }
  return result;
}

/** Orthonormal body axes a limb swings in. */
struct BodyFrame
{
  Vec3 up;
  Vec3 forward;
  Vec3 side;
};

/** How the team of a player reacts to the goal being celebrated. */
enum class GoalMood : std::uint8_t
{
  NONE,
  CELEBRATING,
  SCORER,
  DEJECTED,
};

/** The goal currently being celebrated. */
struct GoalMomentState
{
  bool active = false;
  bool byHome = false;
  /** Real seconds since the goal was first shown. */
  float shownSeconds = 0.0f;
  /** Simulated seconds of celebration so far. */
  float celebrationSeconds = 0.0f;
  PlayerID scorer = 0;
  bool ownGoal = false;
  std::string scorerName;
  std::string minute;
  /** Which goal the ball went in and where it hit the net (world). */
  std::size_t goalIndex = 0;
  Vec3 impact;
};

/** "0" to "99" for the shirts. */
constexpr auto NUMBER_TEXT = []
{
  std::array<std::array<char, 3>, 100> table{};
  for (int number = 0; number < 100; ++number)
  {
    const auto digit = [](int value) { return static_cast<char>('0' + value); };
    table[static_cast<std::size_t>(number)] =
        number < 10
            ? std::array<char, 3>{digit(number), '\0', '\0'}
            : std::array<char, 3>{digit(number / 10), digit(number % 10), '\0'};
  }
  return table;
}();

/** Where a player landed on screen, for labels and hover tooltips. */
struct PlayerOnScreen
{
  const MatchRenderPlayer* player = nullptr;
  ImVec2 headTop;
  ImVec2 feet;
  float depth = 0.0f;
};

/** Skin tones from light to dark; the darker half gets darker hair. */
constexpr std::array<ImU32, 8> SKIN_TONES{
    IM_COL32(244, 210, 184, 255), IM_COL32(232, 190, 154, 255),
    IM_COL32(214, 166, 126, 255), IM_COL32(190, 138, 98, 255),
    IM_COL32(160, 110, 74, 255),  IM_COL32(128, 86, 58, 255),
    IM_COL32(100, 66, 44, 255),   IM_COL32(74, 50, 36, 255)};
constexpr std::array<ImU32, 6> HAIR_COLORS{
    IM_COL32(22, 17, 14, 255),   IM_COL32(40, 40, 44, 255),
    IM_COL32(64, 40, 24, 255),   IM_COL32(110, 72, 38, 255),
    IM_COL32(196, 154, 92, 255), IM_COL32(150, 70, 36, 255)};

float signedArea(std::span<const ImVec2> points)
{
  float area = 0.0f;
  for (std::size_t index = 0; index < points.size(); ++index)
  {
    const ImVec2& a = points[index];
    const ImVec2& b = points[(index + 1) % points.size()];
    area += a.x * b.y - b.x * a.y;
  }
  return area;
}

/** Key light of the scene: floodlights at night, the sun by day. */
struct LightRig
{
  Vec3 direction;
  float ambient = 0.0f;
  float diffuse = 0.0f;
  float sky = 0.0f;
};

LightRig nightLight()
{
  using L = Tuning::Light;
  return {
      RenderMath::normalize({L::DIRECTION_X, L::DIRECTION_Y, L::DIRECTION_Z}),
      L::AMBIENT, L::DIFFUSE, L::SKY};
}

LightRig dayLight()
{
  using D = Tuning::Day;
  return {RenderMath::normalize({D::SUN_X, D::SUN_Y, D::SUN_Z}), D::AMBIENT,
          D::DIFFUSE, D::SKY};
}

float lighting(Vec3 normal, const LightRig& light)
{
  return light.ambient +
         light.diffuse *
             std::max(0.0f, RenderMath::dot(normal, light.direction)) +
         light.sky * std::max(0.0f, normal.z);
}

/** Boot colours: mostly black, some white and the usual bright ones. */
constexpr std::array<ImU32, 9> BOOT_COLORS{
    IM_COL32(26, 26, 30, 255),   IM_COL32(26, 26, 30, 255),
    IM_COL32(26, 26, 30, 255),   IM_COL32(236, 236, 232, 255),
    IM_COL32(236, 222, 40, 255), IM_COL32(246, 112, 42, 255),
    IM_COL32(44, 124, 232, 255), IM_COL32(228, 62, 124, 255),
    IM_COL32(60, 200, 150, 255)};

constexpr std::array<HairStyle, 8> HAIR_STYLES{
    HairStyle::SHORT, HairStyle::SHORT, HairStyle::BUZZ,   HairStyle::VOLUME,
    HairStyle::LONG,  HairStyle::SHORT, HairStyle::SHAVED, HairStyle::BUZZ};

/** Blends the walk, jog and sprint shapes for a ground speed. */
Tuning::Gait::Shape gaitShape(float speed)
{
  using G = Tuning::Gait;
  const auto mix = [](const G::Shape& a, const G::Shape& b, float t)
  {
    const auto lerp = [t](float x, float y) { return x + (y - x) * t; };
    return G::Shape{lerp(a.speed, b.speed), lerp(a.thigh, b.thigh),
                    lerp(a.knee, b.knee),   lerp(a.arm, b.arm),
                    lerp(a.elbow, b.elbow), lerp(a.lean, b.lean),
                    lerp(a.bob, b.bob)};
  };
  if (speed <= G::WALK.speed) return G::WALK;
  if (speed <= G::JOG.speed)
    return mix(G::WALK, G::JOG,
               (speed - G::WALK.speed) / (G::JOG.speed - G::WALK.speed));
  return mix(G::JOG, G::SPRINT,
             std::min(1.0f, (speed - G::JOG.speed) /
                                (G::SPRINT.speed - G::JOG.speed)));
}

/** Skin, hair, boots and build, derived deterministically from the id. */
PlayerLook lookFor(std::uint32_t playerId)
{
  const std::uint32_t looks = cosmeticHash(playerId + 17U);
  PlayerLook look;
  look.seed = looks;
  const std::size_t tone = looks % SKIN_TONES.size();
  look.skin = SKIN_TONES[tone];
  // Fair hair and red hair only with fairer skin.
  const std::size_t hairChoices =
      tone < SKIN_TONES.size() / 2 ? HAIR_COLORS.size() : 2U;
  look.hair = HAIR_COLORS[(looks >> 8) % hairChoices];
  look.boots = BOOT_COLORS[(looks >> 16) % BOOT_COLORS.size()];
  look.hairStyle = HAIR_STYLES[(looks >> 20) % HAIR_STYLES.size()];
  look.beard = (looks >> 24) % 4U == 0U;
  look.bulk = Tuning::Rig::MIN_BULK +
              (Tuning::Rig::MAX_BULK - Tuning::Rig::MIN_BULK) *
                  static_cast<float>((looks >> 26) % 16U) / 15.0f;
  return look;
}

/**
 * Direction of a limb in a body frame: `swing` turns it from straight down
 * towards forward, `spread` out to the side.
 */
Vec3 limbDirection(const BodyFrame& frame, float swing, float spread)
{
  return (frame.up * -std::cos(swing) + frame.forward * std::sin(swing)) *
             std::cos(spread) +
         frame.side * std::sin(spread);
}

PlayerRig::KickTiming kickTiming()
{
  using Kk = Tuning::Kick;
  return {Kk::CONTACT_HOLD_SECONDS, Kk::FOLLOW_THROUGH_SECONDS,
          Kk::RECOVER_SECONDS};
}

/** Simulated seconds a timed ball action plays for. */
float eventDuration(PlayerRig::Event event)
{
  using E = PlayerRig::Event;
  using R = Tuning::Rig;
  switch (event)
  {
    case E::PASS:
    case E::SHOT:
    case E::CROSS:
      return kickTiming().total();
    case E::HEADER:
      return Tuning::Kick::HEADER_SECONDS;
    case E::TACKLE:
      return R::TACKLE_SECONDS;
    case E::SLIDE:
      return R::SLIDE_SECONDS;
    case E::THROW:
      return R::THROW_SECONDS;
    case E::NONE:
      break;
  }
  return 0.0f;
}

/**
 * How much a timed action owns the body `seconds` into it: in at once for
 * strikes (the ball has already gone), eased in for challenges, eased out
 * at the end.
 */
float eventEnvelope(PlayerRig::Event event, float seconds)
{
  using E = PlayerRig::Event;
  const float total = eventDuration(event);
  if (total <= 0.0f || seconds >= total) return 0.0f;
  float in = 0.0f;
  float hold = total * 0.55f;
  switch (event)
  {
    case E::PASS:
    case E::SHOT:
    case E::CROSS:
      hold = Tuning::Kick::CONTACT_HOLD_SECONDS +
             Tuning::Kick::FOLLOW_THROUGH_SECONDS;
      break;
    case E::HEADER:
      in = 0.05f;
      break;
    case E::TACKLE:
      in = 0.06f;
      break;
    case E::SLIDE:
      in = 0.12f;
      hold = total * 0.65f;
      break;
    case E::THROW:
    case E::NONE:
      break;
  }
  const float rise = in > 0.0f ? std::clamp(seconds / in, 0.0f, 1.0f) : 1.0f;
  const float fall =
      1.0f - PlayerRig::smoothStep((seconds - hold) / (total - hold));
  return std::min(rise, fall);
}

bool outsideClipVolume(std::span<const ClipVertex> vertices)
{
  bool left = true;
  bool right = true;
  bool bottom = true;
  bool top = true;
  for (const ClipVertex& vertex : vertices)
  {
    left = left && vertex.clip.x < -vertex.clip.w;
    right = right && vertex.clip.x > vertex.clip.w;
    bottom = bottom && vertex.clip.y < -vertex.clip.w;
    top = top && vertex.clip.y > vertex.clip.w;
  }
  return left || right || bottom || top;
}

ImVec2 toImVec(const ScreenPoint& point) { return {point.x, point.y}; }

/// Interpolated ball height above the grass in metres.
float ballHeightMetres(const MatchRenderBall& ball, float alpha)
{
  return std::max(
      0.0f, ball.previousHeightMetres +
                (ball.currentHeightMetres - ball.previousHeightMetres) * alpha);
}

/// The player's standing height in metres (engine value, else his data).
float playerHeightMetres(const MatchRenderPlayer& player)
{
  using P = Tuning::Player;
  float height = player.heightMetres;
  if (height <= 0.0f && player.player && player.player->getHeight() > 0)
    height = static_cast<float>(player.player->getHeight()) * 0.01f;
  if (height <= 0.0f) height = P::REFERENCE_HEIGHT_METRES;
  return std::clamp(height, P::MIN_HEIGHT_METRES, P::MAX_HEIGHT_METRES);
}

/// The rig's heading: his facing; down after a collision, a quarter turn
/// from the way he lies, so that rolling the body onto its side lays it
/// along that way (head first).
float rigYaw(const MatchRenderPlayer& player, float alpha)
{
  if (player.fallen)
  {
    // Getting up, he turns from lying to the way he will face.
    const float lying = RenderMath::worldYawFromFacing(player.lyingAngle) -
                        0.5f * std::numbers::pi_v<float>;
    return RenderMath::lerpAngle(lying, RenderMath::worldYawFromFacing(player.upFacingAngle),
                                 player.getUpShare);
  }
  return RenderMath::worldYawFromFacing(RenderMath::lerpAngle(
      player.previousFacingAngle, player.currentFacingAngle, alpha));
}
}  // namespace

struct MatchRenderer3D::State
{
  Stadium3D::Geometry geometry;
  bool geometryBuilt = false;
  TeamID homeTeam = 0;
  TeamID awayTeam = 0;
  MatchKits kits;
  MatchCamera3D camera;
  RenderMath::Projection projection;
  /** False until a frame was projected (input maps through it). */
  bool hasProjection = false;
  /** This frame draws no player figures or their shadows (the sandbox's
   * true view draws the engine's shapes in their place). */
  bool hideFigures = false;
  ImDrawList* drawList = nullptr;
  ImVec2 whitePixel;
  float elapsedSeconds = 0.0f;
  /** Real seconds of the current frame. */
  float frameSeconds = 0.0f;
  float attackDirection = 1.0f;
  float directionChangeSeconds = 0.0f;
  /** The play jumped this frame (playback skip): cameras cut, not ease. */
  bool jumped = false;
  /** Daylight preset this frame (kick-off time or the view toggle). */
  bool day = false;
  LightRig light = nightLight();
  /** Ball panel rotation and its on-screen roll direction. */
  float ballSpin = 0.0f;
  Vec3 lastBallWorld;
  ImVec2 ballRoll{1.0f, 0.0f};

  /** Kits indexed home, home keeper, away, away keeper. */
  std::array<KitColors, 4> kitList{};
  std::array<ImU32, 4> numberColors{};
  std::array<ImU32, 4> gloveColors{};
  MatchShirtNumbers numbers;

  // --- per-frame match context --------------------------------------------
  /** Simulated seconds since the previous frame (0 while paused). */
  float simSeconds = 0.0f;
  float lastMatchMinutes = -1.0f;
  bool reducedMotion = false;
  bool livePlay = false;
  Vec3 ballWorld;
  GoalkeeperState homeKeeperState = GoalkeeperState::SET_POSITION;
  GoalkeeperState awayKeeperState = GoalkeeperState::SET_POSITION;
  const std::vector<PlayerMatchStats>* playerStats = nullptr;

  // --- touches and the held ball -----------------------------------------
  /** The kicker's lockout last frame (a rise marks a strike). */
  float lastKickerLockout = 0.0f;
  /** Holding the ball in his hands (keeper, throw-in taker), and where the
   * hands put it this frame. */
  const MatchRenderPlayer* heldBy = nullptr;
  Vec3 heldBallWorld;
  bool heldBallPlaced = false;

  // --- goal presentation --------------------------------------------------
  GoalMomentState goal;
  /** 0..1 weight of the players' goal reactions this frame. */
  float celebrationWeight = 0.0f;
  /** 0..1 weight of the crowd's goal celebration this frame. */
  float crowdCelebration = 0.0f;
  /** Idle sway and celebration hop offsets per crowd phase (metres). */
  std::array<float, Stadium3D::CROWD_PHASES> crowdSway{};
  std::array<float, Stadium3D::CROWD_PHASES> crowdHop{};
  /** 0..1 build-up of the crowd rising for a shot or a chance. */
  float crowdTension = 0.0f;
  std::array<float, Stadium3D::CROWD_PHASES> crowdRise{};
  /** CHEERS_* bit of the celebrating supporters (0: nobody). */
  std::uint8_t cheeringMask = 0;
  int batchVertices = 0;
  int batchIndices = 0;

  /** Clip-space pitch grid vertices of this frame. */
  std::vector<Vec4> pitchClip;
  std::vector<ImVec2> pitchScreen;
  /** Grid colours with the current stripe tint (rebuilt on change). */
  std::vector<ImU32> pitchColors;
  float pitchSwing = 0.0f;
  bool pitchDay = false;
  std::vector<AnimationSlot> slots;
  std::vector<ImVec2> points;
  std::vector<Primitive> primitives;
  std::vector<SortKey> keys;
  std::vector<const char*> texts;
  std::vector<std::pair<float, std::uint32_t>> sectionOrder;
  std::vector<PlayerOnScreen> playersOnScreen;

  State()
  {
    points.reserve(8192);
    primitives.reserve(2048);
    keys.reserve(2048);
    texts.reserve(64);
    sectionOrder.reserve(64);
    playersOnScreen.reserve(32);
    slots.reserve(32);
  }

  // --- frame setup -------------------------------------------------------
  void prepareMatch(const MatchRenderSnapshot& snapshot);
  MatchCameraFocus computeFocus(const MatchRenderSnapshot& snapshot,
                                float deltaSeconds);
  /** Turns the view's mouse input into world-space camera control. */
  MatchCameraControl cameraControl(const MatchCameraInput& input) const;

  // --- immediate (unsorted) layers ----------------------------------------
  void drawSky();
  void emitRaw(std::span<const ImVec2> screen, std::span<const ImU32> colors);
  void drawWorldPolygon(std::span<const Vec3> world,
                        std::span<const ImU32> colors, bool antiAliased);
  void drawGroundEllipse(Vec3 centre, Vec3 axisA, Vec3 axisB, ImU32 color);
  /** Radial-gradient ellipse (dark centre, clear rim) for soft shadows. */
  void drawSoftEllipse(Vec3 centre, Vec3 axisA, Vec3 axisB, ImU32 color);
  /** The mown playing surface (vertex grid, per-frame stripe tint). */
  void drawPitch();
  /** A training pitch (training ground plan section 18): plain grass with a
   * margin round it, a netting fence for its walls, and the kit. */
  void drawTrainingGround();
  void addKit(const MatchRenderSnapshot& snapshot);
  void addTrainingGoals(const MatchRenderSnapshot& snapshot);
  /** The world point of an engine pitch point: a training pitch sits
   * centred where the match pitch is, so the cameras frame it unchanged. */
  Vec3 worldOf(PitchPoint pitch, float heightMetres = 0.0f) const
  {
    return RenderMath::worldFromPitch(
        PitchPoint{pitch.x + zoneOffsetX, pitch.y + zoneOffsetY}, heightMetres);
  }
  bool training = false;
  float pitchLength = LENGTH;
  float pitchWidth = WIDTH;
  float zoneOffsetX = 0.0f;
  float zoneOffsetY = 0.0f;
  /** Half the goal mouth's width on a training pitch with goals (the fence
   * is open there); 0 without goals. */
  float mouthHalf = 0.0f;
  void drawShadows(const MatchRenderSnapshot& snapshot, float alpha);
  void drawStadium();
  /** Crowd and pitch geometry go through chunked reservations (16-bit
   * indices); release before any other draw-list call. */
  void reserveBatch(int vertices, int indices);
  void releaseBatch();
  void batchFan(std::span<const ImVec2> screen, std::span<const ImU32> colors);
  void batchQuad(ImVec2 bottomLeft, ImVec2 bottomRight, ImVec2 topRight,
                 ImVec2 topLeft, ImU32 bottom, ImU32 top);
  /** Fills the idle-sway and goal-hop tables for this frame. */
  void updateCrowdMotion();
  void drawCrowd(const Stadium3D::Face& face);
  void drawCrowdFlags(const Stadium3D::Face& face, float upX, float upY);
  void drawVignette();

  // --- sorted layer -------------------------------------------------------
  void addPolygon(std::span<const ScreenPoint> screen, ImU32 color,
                  float objectDepth, float localDepth);
  void addLine(Vec3 from, Vec3 to, ImU32 color, float thickness,
               float objectDepth);
  void addBox(const std::array<Vec3, 8>& corners, ImU32 color,
              float objectDepth);
  /** Lit prism between two rings of the same size (up to 8 sides). */
  void addPrism(std::span<const Vec3> bottom, std::span<const Vec3> top,
                ImU32 color, float objectDepth, bool capBottom, bool capTop);
  /** Oriented box from its centre and three half axes. */
  void orientedBox(Vec3 centre, Vec3 axisA, Vec3 axisB, Vec3 axisC, ImU32 color,
                   float objectDepth);
  /** Limb segment between two joints, tapering from `fromHalf` to `toHalf`
   * (a prism up close, a box further out, a stroke far away). */
  void addSegment(Vec3 from, Vec3 to, float fromHalf, float toHalf, ImU32 color,
                  Vec3 hint, float objectDepth, PlayerLod lod);
  /** Advances a player's gait, planted feet, actions and keeper stances. */
  void updateMotion(const MatchRenderPlayer& player, AnimationSlot& slot,
                    float alpha, float deltaSeconds);
  /** Who holds the ball in his hands this frame (keeper, throw-in). */
  void findBallHolder(const MatchRenderSnapshot& snapshot, float alpha);
  int shirtNumberFor(const MatchRenderPlayer& player);
  GoalMood goalMoodFor(const MatchRenderPlayer& player) const;
  void addPlayer(const MatchRenderPlayer& player, AnimationSlot& slot,
                 float alpha);
  void addBackPrint(const AnimationSlot& slot, std::size_t kitIndex, Vec3 waist,
                    Vec3 torsoUp, Vec3 torsoForward, Vec3 bodySide, float scale,
                    float objectDepth);
  void drawBackText(const Primitive& primitive);
  /** Starts kick, header and tackle poses from the engine's touches. */
  void detectTouches(const MatchRenderSnapshot& snapshot);
  /** Tracks the goal being celebrated (sting, net ripple, reactions). */
  void updateGoalMoment(const MatchRenderSnapshot& snapshot);
  void addGoals();
  void addCornerFlags();
  void addBoards();
  void addFixtures();
  /** Someone round the touchline: a plain figure, seated, standing,
   * walking or lying (no kit numbers, no actions). */
  void addTouchlinePerson(const MatchRenderTouchlinePerson& person, std::size_t index,
                          float alpha);
  void addBall(const MatchRenderSnapshot& snapshot, float alpha);
  void flushSorted();
  void drawHead(const Primitive& primitive);
  void drawBoard(const Primitive& primitive);
  void drawBoardLogo(const Stadium3D::AdBoard& board,
                     const Stadium3D::SponsorStyle& style, Vec3 origin,
                     float width, float height);

  // --- overlays -----------------------------------------------------------
  void drawLabelsAndHover(const MatchRenderOptions& options);
  void drawScoreBug(const MatchRenderSnapshot& snapshot,
                    const MatchRenderOptions& options);
  void drawGoalSting(const MatchRenderSnapshot& snapshot);
};

void MatchRenderer3D::State::prepareMatch(const MatchRenderSnapshot& snapshot)
{
  TeamID home = snapshot.homeTeam;
  TeamID away = snapshot.awayTeam;
  for (const MatchRenderPlayer& player : snapshot.players)
  {
    if (!player.player) continue;
    if (player.isHomeTeam && home == 0) home = player.player->getTeamId();
    if (!player.isHomeTeam && away == 0) away = player.player->getTeamId();
  }
  if (geometryBuilt && home == homeTeam && away == awayTeam)
  {
    // The lighting is baked into the stadium: rebuild on a day/night switch.
    if (geometry.day != day) geometry.build(kits, day);
    return;
  }
  homeTeam = home;
  awayTeam = away;
  kits = chooseMatchKits(home, away);
  kitList = {kits.home, kits.homeGoalkeeper, kits.away, kits.awayGoalkeeper};
  for (std::size_t index = 0; index < kitList.size(); ++index)
  {
    numberColors[index] = kitNumberColor(kitList[index]);
    gloveColors[index] = goalkeeperGloveColor(kitList[index]);
  }
  numbers.reset(snapshot);
  slots.clear();
  goal = GoalMomentState{};
  // A lockout already running at the first frame is no new strike.
  lastKickerLockout = snapshot.ball.kickerLockout;
  geometry.build(kits, day);
  geometryBuilt = true;
}

MatchCameraFocus MatchRenderer3D::State::computeFocus(
    const MatchRenderSnapshot& snapshot, float deltaSeconds)
{
  const float alpha = snapshot.interpolationAlpha;
  MatchCameraFocus focus;
  focus.ball = worldOf(lerpRenderPosition(
      snapshot.ball.previousPosition, snapshot.ball.currentPosition, alpha));
  focus.ballVelocity =
      (worldOf(snapshot.ball.currentPosition) -
       worldOf(snapshot.ball.previousPosition)) *
      (1.0f / MatchTuning::Timing::FIXED_STEP_SECONDS);

  const MatchRenderPlayer* carrier = nullptr;
  for (const MatchRenderPlayer& player : snapshot.players)
    if (player.possessesBall) carrier = &player;
  if (carrier)
  {
    focus.hasCarrier = true;
    focus.carrier = worldOf(lerpRenderPosition(
        carrier->previousPosition, carrier->currentPosition, alpha));
    focus.carrierYaw = RenderMath::worldYawFromFacing(RenderMath::lerpAngle(
        carrier->previousFacingAngle, carrier->currentFacingAngle, alpha));
    // Attack direction comes from where the carrier's own keeper stands, so
    // it stays right even if the engine ever swaps ends.
    float direction = carrier->isHomeTeam ? 1.0f : -1.0f;
    for (const MatchRenderPlayer& player : snapshot.players)
    {
      if (player.isHomeTeam == carrier->isHomeTeam && player.isGoalkeeper &&
          player.onPitch)
      {
        direction = player.currentPosition.x <
                            MatchPitch::matchSize().atX(MatchTuning::Pitch::CENTRE)
                        ? 1.0f
                        : -1.0f;
      }
    }
    // Brief turnovers must not swing the end camera around the pitch.
    directionChangeSeconds = direction != attackDirection
                                 ? directionChangeSeconds + deltaSeconds
                                 : 0.0f;
    if (directionChangeSeconds >= Tuning::Camera::ATTACK_SWITCH_SECONDS ||
        (jumped && direction != attackDirection))
    {
      attackDirection = direction;
      directionChangeSeconds = 0.0f;
    }
  }
  focus.attackDirection = attackDirection;
  focus.livePlay = snapshot.state == MatchState::PLAYING;
  focus.shotInFlight = focus.livePlay && snapshot.ball.isShot;
  focus.goalCelebration = goal.active;
  focus.reducedMotion = reducedMotion;
  if (goal.active && !goal.ownGoal && goal.scorer != 0)
  {
    for (const MatchRenderPlayer& player : snapshot.players)
    {
      if (!player.player || player.player->getId() != goal.scorer) continue;
      focus.celebration = worldOf(lerpRenderPosition(
          player.previousPosition, player.currentPosition, alpha));
      focus.hasCelebration = true;
    }
  }
  return focus;
}

MatchCameraControl MatchRenderer3D::State::cameraControl(
    const MatchCameraInput& input) const
{
  MatchCameraControl control;
  control.zoomSteps = input.zoomSteps;
  control.orbitYaw = -input.orbitX * Tuning::Free::ORBIT_RADIANS_PER_PIXEL;
  control.orbitPitch = input.orbitY * Tuning::Free::ORBIT_RADIANS_PER_PIXEL;
  control.followBall = input.followBall;
  control.reset = input.reset;
  if (!hasProjection) return control;
  if (input.pan)
  {
    // Grab the ground: the point under the cursor follows the cursor. Near
    // the horizon (or over the sky) fall back to a distance-scaled pan.
    Vec3 from;
    Vec3 to;
    const float limit = camera.distance() * 0.5f;
    if (projection.groundPointAt(input.panFromX, input.panFromY, from) &&
        projection.groundPointAt(input.panToX, input.panToY, to) &&
        RenderMath::length(from - to) <= limit)
    {
      control.pan = from - to;
    }
    else
    {
      const float metresPerPixel = camera.distance() / projection.focalPixels;
      const Vec3 flatSide =
          RenderMath::normalize({projection.side.x, projection.side.y, 0.0f});
      const Vec3 flatForward = RenderMath::normalize(
          {projection.forward.x, projection.forward.y, 0.0f});
      control.pan =
          flatSide * ((input.panFromX - input.panToX) * metresPerPixel) +
          flatForward * ((input.panToY - input.panFromY) * metresPerPixel);
    }
    control.pan.z = 0.0f;
  }
  if (input.retarget)
  {
    control.retarget = projection.groundPointAt(
        input.retargetX, input.retargetY, control.retargetPoint);
  }
  return control;
}

void MatchRenderer3D::State::emitRaw(std::span<const ImVec2> screen,
                                     std::span<const ImU32> colors)
{
  const auto count = static_cast<int>(screen.size());
  drawList->PrimReserve((count - 2) * 3, count);
  const auto base = static_cast<ImDrawIdx>(drawList->_VtxCurrentIdx);
  for (int index = 0; index < count; ++index)
    drawList->PrimWriteVtx(screen[static_cast<std::size_t>(index)], whitePixel,
                           colors[static_cast<std::size_t>(index)]);
  for (int index = 2; index < count; ++index)
  {
    drawList->PrimWriteIdx(base);
    drawList->PrimWriteIdx(static_cast<ImDrawIdx>(base + index - 1));
    drawList->PrimWriteIdx(static_cast<ImDrawIdx>(base + index));
  }
}

void MatchRenderer3D::State::drawWorldPolygon(std::span<const Vec3> world,
                                              std::span<const ImU32> colors,
                                              bool antiAliased)
{
  std::array<ClipVertex, MAX_POLYGON_POINTS> input{};
  std::array<ClipVertex, MAX_POLYGON_POINTS> clipped{};
  const std::size_t count = std::min(world.size(), MAX_POLYGON_POINTS - 1);
  bool allInFront = true;
  bool anyInFront = false;
  for (std::size_t index = 0; index < count; ++index)
  {
    input[index] = {projection.toClip(world[index]), colors[index]};
    const bool inFront = input[index].clip.w >= projection.nearPlane;
    allInFront = allInFront && inFront;
    anyInFront = anyInFront || inFront;
  }
  if (!anyInFront) return;
  std::span<const ClipVertex> vertices(input.data(), count);
  if (!allInFront)
  {
    const std::size_t clippedCount = RenderMath::clipConvexToNearPlane(
        std::span<const ClipVertex>(input.data(), count),
        std::span<ClipVertex>(clipped), projection.nearPlane,
        [](const ClipVertex& vertex) { return vertex.clip.w; },
        [](const ClipVertex& a, const ClipVertex& b, float t)
        {
          return ClipVertex{RenderMath::lerp(a.clip, b.clip, t),
                            mixColor(a.color, b.color, t)};
        });
    if (clippedCount < 3) return;
    vertices = std::span<const ClipVertex>(clipped.data(), clippedCount);
  }
  if (outsideClipVolume(vertices)) return;

  std::array<ImVec2, MAX_POLYGON_POINTS> screen{};
  std::array<ImU32, MAX_POLYGON_POINTS> screenColors{};
  for (std::size_t index = 0; index < vertices.size(); ++index)
  {
    screen[index] = toImVec(projection.clipToScreen(vertices[index].clip));
    screenColors[index] = vertices[index].color;
  }
  const std::span<ImVec2> screenSpan(screen.data(), vertices.size());
  if (antiAliased)
  {
    if (signedArea(screenSpan) < 0.0f)
      std::reverse(screenSpan.begin(), screenSpan.end());
    drawList->AddConvexPolyFilled(
        screen.data(), static_cast<int>(vertices.size()), screenColors[0]);
    return;
  }
  emitRaw(screenSpan,
          std::span<const ImU32>(screenColors.data(), vertices.size()));
}

void MatchRenderer3D::State::drawSky()
{
  const ImVec2 minimum{projection.rect.x, projection.rect.y};
  const ImVec2 maximum{projection.rect.x + projection.rect.width,
                       projection.rect.y + projection.rect.height};
  const Vec3 flatForward =
      RenderMath::normalize({projection.forward.x, projection.forward.y, 0.0f});
  float horizon = minimum.y;
  ScreenPoint horizonPoint;
  if (projection.project(
          projection.eye + flatForward * Tuning::Sky::HORIZON_DISTANCE,
          horizonPoint))
  {
    horizon = std::clamp(horizonPoint.y, minimum.y, maximum.y);
  }
  drawList->AddRectFilled({minimum.x, horizon}, maximum,
                          Tuning::Sky::GROUND_COLOR);
  if (horizon <= minimum.y) return;
  const float middle = minimum.y + (horizon - minimum.y) * 0.55f;
  const ImU32 topColor = day ? Tuning::Day::TOP_COLOR : Tuning::Sky::TOP_COLOR;
  const ImU32 middleColor =
      day ? Tuning::Day::MIDDLE_COLOR : Tuning::Sky::MIDDLE_COLOR;
  const ImU32 horizonColor =
      day ? Tuning::Day::HORIZON_COLOR : Tuning::Sky::HORIZON_COLOR;
  const std::array<ImU32, 4> upper{topColor, topColor, middleColor,
                                   middleColor};
  const std::array<ImVec2, 4> upperQuad{
      ImVec2{minimum.x, minimum.y}, ImVec2{maximum.x, minimum.y},
      ImVec2{maximum.x, middle}, ImVec2{minimum.x, middle}};
  emitRaw(upperQuad, upper);
  const std::array<ImU32, 4> lower{middleColor, middleColor, horizonColor,
                                   horizonColor};
  const std::array<ImVec2, 4> lowerQuad{
      ImVec2{minimum.x, middle}, ImVec2{maximum.x, middle},
      ImVec2{maximum.x, horizon}, ImVec2{minimum.x, horizon}};
  emitRaw(lowerQuad, lower);
}

void MatchRenderer3D::State::drawGroundEllipse(Vec3 centre, Vec3 axisA,
                                               Vec3 axisB, ImU32 color)
{
  std::array<Vec3, Tuning::Shadow::SEGMENTS> world{};
  std::array<ImU32, Tuning::Shadow::SEGMENTS> colors{};
  for (std::size_t index = 0; index < world.size(); ++index)
  {
    const float angle =
        TWO_PI * static_cast<float>(index) / static_cast<float>(world.size());
    world[index] = centre + axisA * std::cos(angle) + axisB * std::sin(angle);
    colors[index] = color;
  }
  drawWorldPolygon(world, colors, true);
}

void MatchRenderer3D::State::drawSoftEllipse(Vec3 centre, Vec3 axisA,
                                             Vec3 axisB, ImU32 color)
{
  // A dark core fading through a ring to a clear rim: a soft shadow with a
  // feathered edge that needs no anti-aliasing fringe. Written into the
  // chunked batch; callers release it before other draw-list calls.
  using Sh = Tuning::Shadow;
  constexpr std::size_t RIM = Sh::SEGMENTS;
  const float alpha = static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFFU);
  const ImU32 core =
      withAlpha(color, static_cast<std::uint8_t>(alpha * Sh::CORE_ALPHA_SHARE));
  const ImU32 clear = withAlpha(color, 0);
  const auto rim = [&](std::size_t index, float share)
  {
    const float angle =
        TWO_PI * static_cast<float>(index % RIM) / static_cast<float>(RIM);
    return centre + (axisA * std::cos(angle) + axisB * std::sin(angle)) * share;
  };
  std::array<ClipVertex, RIM> outer{};
  std::array<Vec4, RIM> inner{};
  const Vec4 middle = projection.toClip(centre);
  bool allInFront = middle.w >= projection.nearPlane;
  for (std::size_t index = 0; index < RIM; ++index)
  {
    outer[index].clip = projection.toClip(rim(index, 1.0f));
    inner[index] = projection.toClip(rim(index, Sh::CORE_RADIUS_SHARE));
    allInFront = allInFront && outer[index].clip.w >= projection.nearPlane &&
                 inner[index].w >= projection.nearPlane;
  }
  if (!allInFront)
  {
    // Rare (a low free camera): clip every piece against the near plane.
    releaseBatch();
    std::array<Vec3, RIM + 2> world{};
    std::array<ImU32, RIM + 2> colors{};
    world[0] = centre;
    colors[0] = color;
    for (std::size_t index = 0; index <= RIM; ++index)
    {
      world[index + 1] = rim(index, Sh::CORE_RADIUS_SHARE);
      colors[index + 1] = core;
    }
    drawWorldPolygon(world, colors, false);
    for (std::size_t index = 0; index < RIM; ++index)
    {
      const std::array<Vec3, 4> quad{rim(index, Sh::CORE_RADIUS_SHARE),
                                     rim(index, 1.0f), rim(index + 1, 1.0f),
                                     rim(index + 1, Sh::CORE_RADIUS_SHARE)};
      const std::array<ImU32, 4> quadColors{core, clear, clear, core};
      drawWorldPolygon(quad, quadColors, false);
    }
    return;
  }
  if (outsideClipVolume(outer)) return;
  std::array<ImVec2, RIM + 1> fan{};
  std::array<ImU32, RIM + 1> fanColors{};
  std::array<ImVec2, RIM> edge{};
  fan[0] = toImVec(projection.clipToScreen(middle));
  fanColors[0] = color;
  for (std::size_t index = 0; index < RIM; ++index)
  {
    fan[index + 1] = toImVec(projection.clipToScreen(inner[index]));
    fanColors[index + 1] = core;
    edge[index] = toImVec(projection.clipToScreen(outer[index].clip));
  }
  // The fan closes on itself: centre, ring 0..RIM-1, ring 0 again.
  reserveBatch(static_cast<int>(RIM) + 1, static_cast<int>(RIM) * 3);
  const auto first = static_cast<ImDrawIdx>(drawList->_VtxCurrentIdx);
  for (std::size_t index = 0; index <= RIM; ++index)
    drawList->PrimWriteVtx(fan[index], whitePixel, fanColors[index]);
  for (std::size_t index = 0; index < RIM; ++index)
  {
    drawList->PrimWriteIdx(first);
    drawList->PrimWriteIdx(static_cast<ImDrawIdx>(first + 1 + index));
    drawList->PrimWriteIdx(
        static_cast<ImDrawIdx>(first + 1 + (index + 1) % RIM));
  }
  batchVertices -= static_cast<int>(RIM) + 1;
  batchIndices -= static_cast<int>(RIM) * 3;
  for (std::size_t index = 0; index < RIM; ++index)
  {
    const std::size_t next = (index + 1) % RIM;
    const std::array<ImVec2, 4> quad{fan[index + 1], edge[index], edge[next],
                                     fan[next + 1]};
    const std::array<ImU32, 4> quadColors{core, clear, clear, core};
    batchFan(quad, quadColors);
  }
}

void MatchRenderer3D::State::drawPitch()
{
  using G = Tuning::Grass;
  const Stadium3D::PitchGrid& grid = geometry.pitch;
  if (grid.points.empty()) return;
  // Blades leaning away from the eye catch more light: the stripe pattern
  // follows the view along the mowing direction (across the pitch).
  const float flat =
      std::max(std::hypot(projection.forward.x, projection.forward.y), 1e-3f);
  const float across = projection.forward.y / flat;
  const float contrast =
      G::STRIPE_CONTRAST *
      (G::STRIPE_SIDE_SHARE + (1.0f - G::STRIPE_SIDE_SHARE) * std::abs(across));
  // Quantised, so the tinted colours are only rebuilt when it changes.
  const float swing =
      std::round(contrast * (across >= 0.0f ? 1.0f : -1.0f) * 500.0f) / 500.0f;
  if (swing != pitchSwing || day != pitchDay ||
      pitchColors.size() != grid.colors.size())
  {
    pitchSwing = swing;
    pitchDay = day;
    pitchColors.resize(grid.colors.size());
    const float sunlit = day ? Tuning::Day::GRASS_LIGHT : 1.0f;
    for (int stripe = 0; stripe < grid.stripes; ++stripe)
    {
      const float stripeLight =
          (stripe % 2 == 0 ? 1.0f + swing : 1.0f - swing) * sunlit;
      for (int column = 0; column <= grid.columnsPerStripe; ++column)
      {
        for (int row = 0; row <= grid.rows; ++row)
        {
          const std::size_t index = grid.index(stripe, column, row);
          pitchColors[index] = shadeColor(grid.colors[index], stripeLight);
        }
      }
    }
  }
  pitchClip.resize(grid.points.size());
  pitchScreen.resize(grid.points.size());
  for (std::size_t index = 0; index < grid.points.size(); ++index)
  {
    pitchClip[index] = projection.toClip(grid.points[index]);
    if (pitchClip[index].w >= projection.nearPlane)
      pitchScreen[index] = toImVec(projection.clipToScreen(pitchClip[index]));
  }

  for (int stripe = 0; stripe < grid.stripes; ++stripe)
  {
    for (int column = 0; column < grid.columnsPerStripe; ++column)
    {
      for (int row = 0; row < grid.rows; ++row)
      {
        const std::array<std::size_t, 4> corner{
            grid.index(stripe, column, row),
            grid.index(stripe, column + 1, row),
            grid.index(stripe, column + 1, row + 1),
            grid.index(stripe, column, row + 1)};
        std::array<ClipVertex, 4> vertices{};
        bool allInFront = true;
        for (std::size_t index = 0; index < 4; ++index)
        {
          vertices[index] = {pitchClip[corner[index]],
                             pitchColors[corner[index]]};
          allInFront =
              allInFront && vertices[index].clip.w >= projection.nearPlane;
        }
        if (!allInFront)
        {
          // Rare: the cell crosses the near plane (low free camera).
          releaseBatch();
          std::array<Vec3, 4> world{};
          std::array<ImU32, 4> colors{};
          for (std::size_t index = 0; index < 4; ++index)
          {
            world[index] = grid.points[corner[index]];
            colors[index] = vertices[index].color;
          }
          drawWorldPolygon(world, colors, false);
          continue;
        }
        if (outsideClipVolume(vertices)) continue;
        std::array<ImVec2, 4> screen{};
        std::array<ImU32, 4> colors{};
        for (std::size_t index = 0; index < 4; ++index)
        {
          screen[index] = pitchScreen[corner[index]];
          colors[index] = vertices[index].color;
        }
        batchFan(screen, colors);
      }
    }
  }
  releaseBatch();
}

void MatchRenderer3D::State::drawShadows(const MatchRenderSnapshot& snapshot,
                                         float alpha)
{
  // Four floodlight towers cast four faint blades per player plus a darker
  // contact shadow: the classic night-match look. By day the sun casts one.
  using F = Tuning::Floodlight;
  using Sh = Tuning::Shadow;
  const std::array<Vec3, 4> lights{
      Vec3{-F::CORNER_OFFSET_X, -F::CORNER_OFFSET_Y, 0.0f},
      Vec3{LENGTH + F::CORNER_OFFSET_X, -F::CORNER_OFFSET_Y, 0.0f},
      Vec3{-F::CORNER_OFFSET_X, WIDTH + F::CORNER_OFFSET_Y, 0.0f},
      Vec3{LENGTH + F::CORNER_OFFSET_X, WIDTH + F::CORNER_OFFSET_Y, 0.0f}};
  const Vec3 sunAway =
      RenderMath::normalize({-light.direction.x, -light.direction.y, 0.0f});
  const float sunLength = Sh::SUN_LENGTH_SHARE *
                          std::hypot(light.direction.x, light.direction.y) /
                          std::max(light.direction.z, 0.2f);
  for (const MatchRenderPlayer& player : snapshot.players)
  {
    if (hideFigures) break;
    if (player.hidden) continue;
    const Vec3 root = worldOf(lerpRenderPosition(
        player.previousPosition, player.currentPosition, alpha));
    ScreenPoint screen;
    if (!projection.project(root, screen)) continue;
    // Far away, a soft disc in the shirt colour keeps the teams readable.
    const float pixelHeight =
        playerHeightMetres(player) * projection.focalPixels / screen.depth;
    if (pixelHeight < Tuning::Player::MARKER_MAX_PIXELS)
    {
      releaseBatch();
      const KitColors& kit =
          player.isHomeTeam
              ? (player.isGoalkeeper ? kits.homeGoalkeeper : kits.home)
              : (player.isGoalkeeper ? kits.awayGoalkeeper : kits.away);
      const float fade =
          std::clamp((Tuning::Player::MARKER_MAX_PIXELS - pixelHeight) /
                         (Tuning::Player::MARKER_MAX_PIXELS * 0.5f),
                     0.0f, 1.0f);
      const float radius = Tuning::Player::MARKER_RADIUS;
      // A shirt close to the grass colour would vanish: use the trim.
      const ImU32 marker =
          kitColorDistance(kit.shirt, Tuning::Grass::PITCH_COLOR) <
                  KIT_CLASH_DISTANCE
              ? kit.trim
              : kit.shirt;
      drawGroundEllipse(
          root, {radius, 0.0f, 0.0f}, {0.0f, radius, 0.0f},
          withAlpha(
              marker,
              static_cast<std::uint8_t>(
                  static_cast<float>(Tuning::Player::MARKER_ALPHA) * fade)));
    }
    const float standing = playerHeightMetres(player);
    if (day && geometry.inStandShadow(root))
    {
      // In a stand's shadow only the contact shadow remains.
    }
    else if (day)
    {
      const float bladeLength = standing * sunLength;
      const Vec3 across{-sunAway.y, sunAway.x, 0.0f};
      drawSoftEllipse(root + sunAway * (bladeLength * 0.45f),
                      sunAway * (bladeLength * 0.55f + Sh::PENUMBRA),
                      across * (Sh::BLADE_HALF_WIDTH + Sh::PENUMBRA),
                      IM_COL32(0, 0, 0, Sh::SUN_ALPHA));
    }
    else
    {
      // Each mast throws a blade as long as its geometry dictates, darker
      // for the nearer (brighter) lamps. Up close the blades fade so the
      // four of them never read as a hard "X" under the boots.
      const float close = std::clamp(
          (pixelHeight - Sh::CLOSE_FADE_START_PIXELS) /
              (Sh::CLOSE_FADE_FULL_PIXELS - Sh::CLOSE_FADE_START_PIXELS),
          0.0f, 1.0f);
      const float closeShare = 1.0f - (1.0f - Sh::CLOSE_SHARE) * close;
      std::array<float, 4> weight{};
      float weightSum = 0.0f;
      for (std::size_t index = 0; index < lights.size(); ++index)
      {
        const Vec3 offset = root - lights[index];
        weight[index] = 1.0f / std::max(RenderMath::dot(offset, offset), 1.0f);
        weightSum += weight[index];
      }
      for (std::size_t index = 0; index < lights.size(); ++index)
      {
        Vec3 away = root - lights[index];
        const float reach = std::hypot(away.x, away.y);
        away = RenderMath::normalize(away);
        const Vec3 across{-away.y, away.x, 0.0f};
        const float bladeLength = std::clamp(standing * reach / F::MAST_HEIGHT,
                                             Sh::MIN_BLADE, Sh::MAX_BLADE);
        const float share = std::clamp(
            weight[index] / weightSum * static_cast<float>(lights.size()),
            Sh::MIN_LAMP_SHARE, Sh::MAX_LAMP_SHARE);
        const auto darkness = static_cast<std::uint8_t>(
            static_cast<float>(Sh::BLADE_ALPHA) * share * closeShare);
        drawSoftEllipse(root + away * (bladeLength * 0.45f),
                        away * (bladeLength * 0.55f + Sh::PENUMBRA),
                        across * (Sh::BLADE_HALF_WIDTH + Sh::PENUMBRA),
                        IM_COL32(0, 0, 0, darkness));
      }
    }
    drawSoftEllipse(root, {Sh::CONTACT_RADIUS, 0.0f, 0.0f},
                    {0.0f, Sh::CONTACT_RADIUS, 0.0f}, Sh::CONTACT_COLOR);
    if (player.possessesBall)
    {
      releaseBatch();
      const float radius =
          Sh::RING_RADIUS *
          (1.0f + (reducedMotion
                       ? 0.0f
                       : Sh::RING_PULSE *
                             std::sin(elapsedSeconds * Sh::RING_PULSE_SPEED)));
      std::array<ImVec2, Sh::RING_SEGMENTS> ring{};
      bool visible = true;
      for (std::size_t index = 0; index < ring.size() && visible; ++index)
      {
        const float angle = TWO_PI * static_cast<float>(index) /
                            static_cast<float>(ring.size());
        ScreenPoint point;
        visible =
            projection.project(root + Vec3{std::cos(angle) * radius,
                                           std::sin(angle) * radius, 0.0f},
                               point);
        ring[index] = toImVec(point);
      }
      if (visible)
      {
        drawList->AddPolyline(
            ring.data(), static_cast<int>(ring.size()), Sh::RING_COLOR,
            ImDrawFlags_Closed,
            Sh::RING_THICKNESS * ImGui::GetStyle().FontScaleDpi);
      }
    }
  }

  // A ball held in the hands sits in the holder's own shadow.
  if (heldBy || snapshot.ball.outOfSight)
  {
    releaseBatch();
    return;
  }
  const PitchPoint ball = lerpRenderPosition(
      snapshot.ball.previousPosition, snapshot.ball.currentPosition, alpha);
  const float height = ballHeightMetres(snapshot.ball, alpha);
  const float radius = Tuning::Ball::SHADOW_RADIUS *
                       (1.0f + height * Tuning::Ball::SHADOW_GROWTH);
  const auto shadowAlpha = static_cast<std::uint8_t>(
      110.0f / (1.0f + height * Tuning::Ball::SHADOW_FADE));
  // By day the lifted ball's shadow slides away from the sun.
  const Vec3 slide = day ? sunAway * (height * sunLength) : Vec3{};
  drawSoftEllipse(worldOf(ball) + slide,
                  {radius, 0.0f, 0.0f}, {0.0f, radius, 0.0f},
                  IM_COL32(0, 0, 0, shadowAlpha));
  releaseBatch();
}

void MatchRenderer3D::State::reserveBatch(int vertices, int indices)
{
  if (vertices <= batchVertices && indices <= batchIndices) return;
  releaseBatch();
  // Chunks stay far below the 16-bit index limit of one reservation.
  batchVertices = std::max(vertices, BATCH_CHUNK_VERTICES);
  batchIndices = std::max(indices, BATCH_CHUNK_VERTICES * 3 / 2);
  drawList->PrimReserve(batchIndices, batchVertices);
}

void MatchRenderer3D::State::releaseBatch()
{
  if (batchVertices > 0 || batchIndices > 0)
    drawList->PrimUnreserve(batchIndices, batchVertices);
  batchVertices = 0;
  batchIndices = 0;
}

void MatchRenderer3D::State::batchFan(std::span<const ImVec2> screen,
                                      std::span<const ImU32> colors)
{
  const auto count = static_cast<int>(screen.size());
  reserveBatch(count, (count - 2) * 3);
  const auto first = static_cast<ImDrawIdx>(drawList->_VtxCurrentIdx);
  for (int index = 0; index < count; ++index)
    drawList->PrimWriteVtx(screen[static_cast<std::size_t>(index)], whitePixel,
                           colors[static_cast<std::size_t>(index)]);
  for (int index = 2; index < count; ++index)
  {
    drawList->PrimWriteIdx(first);
    drawList->PrimWriteIdx(static_cast<ImDrawIdx>(first + index - 1));
    drawList->PrimWriteIdx(static_cast<ImDrawIdx>(first + index));
  }
  batchVertices -= count;
  batchIndices -= (count - 2) * 3;
}

void MatchRenderer3D::State::batchQuad(ImVec2 bottomLeft, ImVec2 bottomRight,
                                       ImVec2 topRight, ImVec2 topLeft,
                                       ImU32 bottom, ImU32 top)
{
  const std::array<ImVec2, 4> quad{bottomLeft, bottomRight, topRight, topLeft};
  const std::array<ImU32, 4> colors{bottom, bottom, top, top};
  batchFan(quad, colors);
}

void MatchRenderer3D::State::updateCrowdMotion()
{
  using C = Tuning::Crowd;
  // One small table per frame; every spectator reads its phase from it.
  for (std::size_t phase = 0; phase < Stadium3D::CROWD_PHASES; ++phase)
  {
    const float offset = TWO_PI * static_cast<float>(phase) /
                         static_cast<float>(Stadium3D::CROWD_PHASES);
    const float rate =
        0.75f + 0.5f * static_cast<float>((phase * 7U) % 16U) / 16.0f;
    crowdSway[phase] =
        reducedMotion
            ? 0.0f
            : C::SWAY *
                  std::sin(elapsedSeconds * C::SWAY_SPEED * rate + offset);
    // Everyone half rises out of the seat while a shot is on its way.
    crowdRise[phase] =
        reducedMotion
            ? 0.0f
            : C::RISE * crowdTension *
                  (0.7f +
                   0.3f * static_cast<float>((phase * 5U) % 16U) / 16.0f);
    crowdHop[phase] =
        reducedMotion ? 0.0f
                      : C::HOP * crowdCelebration *
                            std::abs(std::sin(elapsedSeconds * C::HOP_SPEED *
                                                  (0.85f + 0.3f * rate) +
                                              offset * 3.0f));
  }
  cheeringMask = 0;
  if (goal.active && crowdCelebration > 0.0f)
  {
    cheeringMask = goal.byHome ? Stadium3D::CrowdFlags::CHEERS_HOME
                               : Stadium3D::CrowdFlags::CHEERS_AWAY;
  }
}

void MatchRenderer3D::State::drawCrowd(const Stadium3D::Face& face)
{
  using C = Tuning::Crowd;
  namespace Flags = Stadium3D::CrowdFlags;
  if (face.clumpEnd <= face.clumpBegin) return;
  {
    // Skip the whole tier when it (heads and flags included) is off screen.
    std::array<ClipVertex, 8> bounds{};
    bool anyInFront = false;
    for (std::size_t corner = 0; corner < 4; ++corner)
    {
      bounds[corner].clip = projection.toClip(face.corners[corner]);
      bounds[corner + 4].clip = projection.toClip(
          face.corners[corner] + UP * (C::DOT_HEIGHT + C::POLE_HEIGHT));
      anyInFront = anyInFront ||
                   bounds[corner].clip.w >= projection.nearPlane ||
                   bounds[corner + 4].clip.w >= projection.nearPlane;
    }
    bool allInFront = true;
    for (const ClipVertex& vertex : bounds)
      allInFront = allInFront && vertex.clip.w >= projection.nearPlane;
    if (!anyInFront || (allInFront && outsideClipVolume(bounds))) return;
  }
  const Vec3 centre = (face.corners[0] + face.corners[2]) * 0.5f;
  ScreenPoint centreScreen;
  ScreenPoint aboveScreen;
  if (!projection.project(centre, centreScreen) ||
      !projection.project(centre + UP, aboveScreen))
    return;
  // Screen-space "up" per metre at the face centre, rescaled per depth.
  const float upX = (aboveScreen.x - centreScreen.x) * centreScreen.depth;
  const float upY = (aboveScreen.y - centreScreen.y) * centreScreen.depth;
  const float upLength = std::max(std::hypot(upX, upY), 1e-3f);
  // Screen "across" is perpendicular to up, so tilted views stay upright.
  const float acrossX = -upY / upLength;
  const float acrossY = upX / upLength;
  const float left = projection.rect.x;
  const float right = projection.rect.x + projection.rect.width;
  const float top = projection.rect.y;
  const float bottom = projection.rect.y + projection.rect.height;
  const float seatFocal = C::SEAT_SPACING * projection.focalPixels;
  const float halfDotFocal = C::DOT_WIDTH * 0.5f * projection.focalPixels;
  const bool cheering = cheeringMask != 0U;
  const auto point =
      [&](float x, float y, float across, float up, float dx, float dy)
  {
    return ImVec2{x + acrossX * across + dx * up,
                  y + acrossY * across + dy * up};
  };

  for (std::uint32_t clumpIndex = face.clumpBegin; clumpIndex < face.clumpEnd;
       ++clumpIndex)
  {
    const Stadium3D::CrowdClump& clump = geometry.clumps[clumpIndex];
    const Vec4 clumpClip = projection.toClip(clump.base);
    if (clumpClip.w < projection.nearPlane) continue;
    const float clumpInverse = 1.0f / clumpClip.w;
    const float seatPixels = seatFocal * clumpInverse;
    if (seatPixels < C::CLUMP_PIXELS)
    {
      // Far: one quad up the tier in the members' average colour.
      const float halfWidth =
          clump.halfWidth * projection.focalPixels * clumpInverse;
      if (halfWidth * 2.0f < C::MIN_CLUMP_PIXELS) continue;
      const Vec4 topClip = projection.toClip(clump.top);
      if (topClip.w < projection.nearPlane) continue;
      ScreenPoint base = projection.clipToScreen(clumpClip);
      ScreenPoint upper = projection.clipToScreen(topClip);
      const float topHalf =
          clump.halfWidth * projection.focalPixels / topClip.w;
      if (std::max(base.x, upper.x) + halfWidth < left ||
          std::min(base.x, upper.x) - halfWidth > right ||
          std::min(base.y, upper.y) > bottom || std::max(base.y, upper.y) < top)
        continue;
      {
        const bool hopping = cheering && (clump.flags & cheeringMask) != 0U;
        const float hop = (crowdRise[clump.phase] +
                           (hopping ? crowdHop[clump.phase] : 0.0f)) *
                          C::DOT_HEIGHT * clumpInverse;
        upper.x += upX * hop;
        upper.y += upY * hop;
      }
      batchQuad({base.x - halfWidth, base.y}, {base.x + halfWidth, base.y},
                {upper.x + topHalf, upper.y}, {upper.x - topHalf, upper.y},
                shadeColor256(clump.color, 218U), clump.color);
      continue;
    }
    {
      // The clump's own bounds first: most of a stand is off screen.
      const ScreenPoint base = projection.clipToScreen(clumpClip);
      const float reach = (clump.halfWidth + C::DOT_WIDTH) *
                          projection.focalPixels * clumpInverse;
      const float rise = (C::DOT_HEIGHT * 2.0f +
                          C::ROW_DEPTH * static_cast<float>(C::CLUMP_ROWS)) *
                         projection.focalPixels * clumpInverse;
      if (base.x + reach < left || base.x - reach > right ||
          base.y - rise > bottom || base.y + rise < top)
        continue;
    }
    // Near: individual spectators whose colours fade towards the clump
    // average while they are still small (cheap mip-mapping).
    const auto detail = static_cast<std::uint32_t>(
        256.0f * std::clamp((seatPixels - C::CLUMP_PIXELS) /
                                (C::FULL_DETAIL_PIXELS - C::CLUMP_PIXELS),
                            0.0f, 1.0f));
    for (std::uint32_t index = clump.dotBegin; index < clump.dotEnd; ++index)
    {
      const Stadium3D::CrowdDot& dot = geometry.crowd[index];
      const Vec4 clip = projection.toClip(dot.base);
      if (clip.w < projection.nearPlane) continue;
      ScreenPoint base = projection.clipToScreen(clip);
      const float inverseDepth = 1.0f / clip.w;
      const float halfWidth = halfDotFocal * inverseDepth;
      const float dx = upX * C::DOT_HEIGHT * inverseDepth;
      const float dy = upY * C::DOT_HEIGHT * inverseDepth;
      if (base.x + halfWidth < left || base.x - halfWidth > right ||
          base.y + dy > bottom || base.y < top)
        continue;
      const bool celebrating = cheering && (dot.flags & cheeringMask) != 0U;
      const float lift = crowdSway[dot.phase] + crowdRise[dot.phase] +
                         (celebrating ? crowdHop[dot.phase] : 0.0f);
      base.x += dx * lift;
      base.y += dy * lift;
      const ImU32 body =
          detail < 256U ? mixColor256(clump.color, dot.body, detail) : dot.body;
      const ImU32 head =
          detail < 256U ? mixColor256(clump.color, dot.head, detail) : dot.head;
      if (halfWidth * 2.0f < C::HEAD_DETAIL_PIXELS)
      {
        // Head-and-shoulders outline: reads as a person, not a block.
        const float headHalf = halfWidth * C::HEAD_WIDTH_SHARE;
        const std::array<ImVec2, 6> outline{
            point(base.x, base.y, -halfWidth, 0.0f, dx, dy),
            point(base.x, base.y, halfWidth, 0.0f, dx, dy),
            point(base.x, base.y, halfWidth, C::BODY_SHARE, dx, dy),
            point(base.x, base.y, headHalf, 1.0f, dx, dy),
            point(base.x, base.y, -headHalf, 1.0f, dx, dy),
            point(base.x, base.y, -halfWidth, C::BODY_SHARE, dx, dy)};
        const std::array<ImU32, 6> colors{shadeColor256(body, 184U),
                                          shadeColor256(body, 184U),
                                          body,
                                          head,
                                          head,
                                          body};
        batchFan(outline, colors);
        continue;
      }
      // A seated silhouette: torso with sloping shoulders and a round head.
      const float shoulder = C::BODY_SHARE;
      const float shoulderLow = shoulder - C::SHOULDER_DROP;
      const float narrow = halfWidth * C::SHOULDER_SHARE;
      const ImU32 lower = shadeColor256(body, 184U);
      const std::array<ImVec2, 6> torso{
          point(base.x, base.y, -halfWidth, 0.0f, dx, dy),
          point(base.x, base.y, halfWidth, 0.0f, dx, dy),
          point(base.x, base.y, halfWidth, shoulderLow, dx, dy),
          point(base.x, base.y, narrow, shoulder, dx, dy),
          point(base.x, base.y, -narrow, shoulder, dx, dy),
          point(base.x, base.y, -halfWidth, shoulderLow, dx, dy)};
      const std::array<ImU32, 6> torsoColors{lower, lower, body,
                                             body,  body,  body};
      batchFan(torso, torsoColors);
      if (celebrating)
      {
        // Arms thrown up over the head, hands in skin colour.
        const float armHalf = halfWidth * C::ARM_WIDTH_SHARE;
        const float reach = 1.0f + C::ARM_REACH;
        for (const float sideSign : {-1.0f, 1.0f})
        {
          const float from = sideSign * narrow;
          const float to = sideSign * halfWidth * 1.15f;
          batchQuad(point(base.x, base.y, from - armHalf, shoulder, dx, dy),
                    point(base.x, base.y, from + armHalf, shoulder, dx, dy),
                    point(base.x, base.y, to + armHalf, reach, dx, dy),
                    point(base.x, base.y, to - armHalf, reach, dx, dy), body,
                    head);
        }
        if ((dot.flags & Flags::SCARF) != 0U)
        {
          const KitColors& kit =
              (dot.flags & Flags::AWAY_FAN) != 0U ? kits.away : kits.home;
          const float scarfHalf = halfWidth * C::SCARF_WIDTH_SHARE * 0.5f;
          const float scarfBottom = reach - C::SCARF_HEIGHT_SHARE;
          for (const float sideSign : {-1.0f, 1.0f})
          {
            const ImU32 color = sideSign < 0.0f ? kit.shirt : kit.trim;
            batchQuad(
                point(base.x, base.y, 0.0f, scarfBottom, dx, dy),
                point(base.x, base.y, sideSign * scarfHalf, scarfBottom, dx,
                      dy),
                point(base.x, base.y, sideSign * scarfHalf, reach, dx, dy),
                point(base.x, base.y, 0.0f, reach, dx, dy),
                shadeColor(color, 0.85f), color);
          }
        }
      }
      const float headRadius = (1.0f - shoulder - C::NECK_GAP) * 0.5f;
      const float headMiddle = 1.0f - headRadius;
      const float headHalf = halfWidth * C::HEAD_WIDTH_SHARE;
      std::array<ImVec2, HEAD_SIDES> outline{};
      std::array<ImU32, HEAD_SIDES> headColors{};
      for (std::size_t corner = 0; corner < HEAD_SIDES; ++corner)
      {
        outline[corner] =
            point(base.x, base.y, headHalf * HEAD_SHAPE[corner][0],
                  headMiddle + headRadius * HEAD_SHAPE[corner][1], dx, dy);
        headColors[corner] =
            HEAD_SHAPE[corner][1] < 0.0f ? shadeColor256(head, 210U) : head;
      }
      batchFan(outline, headColors);
    }
  }
  drawCrowdFlags(face, upX, upY);
  releaseBatch();
}

void MatchRenderer3D::State::drawCrowdFlags(const Stadium3D::Face& face,
                                            float upX, float upY)
{
  using C = Tuning::Crowd;
  const bool cheering = cheeringMask != 0U;
  for (std::uint32_t index = face.flagBegin; index < face.flagEnd; ++index)
  {
    const Stadium3D::CrowdFlag& flag = geometry.flags[index];
    const Vec4 clip = projection.toClip(flag.base);
    if (clip.w < projection.nearPlane) continue;
    const float inverseDepth = 1.0f / clip.w;
    if (C::FLAG_HEIGHT * projection.focalPixels * inverseDepth <
        C::FLAG_MIN_PIXELS)
      continue;
    const bool celebrating = cheering && (flag.flags & cheeringMask) != 0U;
    const float energy = celebrating ? 1.0f + crowdCelebration : 1.0f;
    const float lift =
        celebrating ? crowdHop[flag.phase] * C::DOT_HEIGHT : 0.0f;
    const float wavePhase = reducedMotion
                                ? static_cast<float>(flag.phase)
                                : elapsedSeconds * C::FLAG_WAVE_SPEED * energy +
                                      static_cast<float>(flag.phase);
    const Vec3 poleBottom = flag.base + UP * lift;
    const Vec3 poleTop = poleBottom + UP * C::POLE_HEIGHT;
    // Two cloth panels (one per colour) rippling away from the pole.
    std::array<Vec3, 6> cloth{};
    for (std::size_t column = 0; column < 3; ++column)
    {
      const float share = static_cast<float>(column) * 0.5f;
      const float wave =
          C::FLAG_WAVE * energy * share * std::sin(wavePhase + share * 2.8f);
      const Vec3 edge =
          poleTop + flag.along * (C::FLAG_LENGTH * share) + UP * wave;
      cloth[column] = edge;
      cloth[column + 3] = edge - UP * C::FLAG_HEIGHT;
    }
    std::array<ImVec2, 6> screen{};
    bool visible = true;
    for (std::size_t corner = 0; corner < cloth.size() && visible; ++corner)
    {
      ScreenPoint projected;
      visible = projection.project(cloth[corner], projected);
      screen[corner] = toImVec(projected);
    }
    ScreenPoint bottomScreen;
    if (!visible || !projection.project(poleBottom, bottomScreen)) continue;
    const float poleHalf = std::max(
        0.5f, C::DOT_WIDTH * 0.08f * projection.focalPixels * inverseDepth);
    const float normal = std::max(std::hypot(upX, upY), 1e-3f);
    const float offsetX = -upY / normal * poleHalf;
    const float offsetY = upX / normal * poleHalf;
    batchQuad({bottomScreen.x - offsetX, bottomScreen.y - offsetY},
              {bottomScreen.x + offsetX, bottomScreen.y + offsetY},
              {screen[0].x + offsetX, screen[0].y + offsetY},
              {screen[0].x - offsetX, screen[0].y - offsetY}, C::POLE_COLOR,
              C::POLE_COLOR);
    for (std::size_t panel = 0; panel < 2; ++panel)
    {
      const ImU32 color = panel == 0 ? flag.primary : flag.secondary;
      batchQuad(screen[panel + 3], screen[panel + 4], screen[panel + 1],
                screen[panel], shadeColor(color, 0.82f), color);
    }
  }
}

void MatchRenderer3D::State::drawStadium()
{
  using S = Tuning::Stands;
  const Vec3 eye = projection.eye;
  const bool belowRoofs = eye.z < Tuning::Camera::STAND_CLEAR_HEIGHT;
  sectionOrder.clear();
  for (std::uint32_t index = 0;
       index < static_cast<std::uint32_t>(geometry.sections.size()); ++index)
  {
    const Stadium3D::Section& section = geometry.sections[index];
    // A stand the eye sits in (or behind, below roof level) is never drawn:
    // real broadcasts do not show the gantry's own stand either. High
    // cameras look over the roofs and draw every stand.
    if (belowRoofs &&
        ((section.side == Stadium3D::Side::SOUTH && eye.y < -S::SIDE_FRONT) ||
         (section.side == Stadium3D::Side::NORTH &&
          eye.y > WIDTH + S::SIDE_FRONT) ||
         (section.side == Stadium3D::Side::WEST && eye.x < -S::END_FRONT) ||
         (section.side == Stadium3D::Side::EAST &&
          eye.x > LENGTH + S::END_FRONT)))
      continue;
    sectionOrder.emplace_back(projection.depth(section.centre), index);
  }
  std::sort(sectionOrder.begin(), sectionOrder.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });

  for (const auto& [depth, sectionIndex] : sectionOrder)
  {
    const Stadium3D::Section& section = geometry.sections[sectionIndex];
    for (std::uint32_t faceIndex = section.faceBegin;
         faceIndex < section.faceEnd; ++faceIndex)
    {
      const Stadium3D::Face& face = geometry.faces[faceIndex];
      if (RenderMath::dot(face.normal, eye - face.corners[0]) <= 0.0f) continue;
      drawWorldPolygon(face.corners, face.colors, false);
      drawCrowd(face);
      // Floodlights are off by day.
      if (face.glow != 0 && !day)
      {
        const Vec3 centre = (face.corners[0] + face.corners[2]) * 0.5f;
        ScreenPoint glow;
        if (projection.project(centre, glow))
        {
          const float radius = Tuning::Floodlight::GLOW_RADIUS *
                               projection.focalPixels / glow.depth;
          drawList->AddCircleFilled(toImVec(glow), radius,
                                    withAlpha(face.glow, 34));
          drawList->AddCircleFilled(toImVec(glow), radius * 0.55f,
                                    withAlpha(face.glow, 70));
          drawList->AddCircleFilled(toImVec(glow), radius * 0.25f,
                                    withAlpha(face.glow, 200));
        }
      }
    }
  }
}

void MatchRenderer3D::State::addPolygon(std::span<const ScreenPoint> screen,
                                        ImU32 color, float objectDepth,
                                        float localDepth)
{
  Primitive primitive;
  primitive.firstPoint = static_cast<std::uint32_t>(points.size());
  primitive.pointCount = static_cast<std::uint16_t>(screen.size());
  primitive.color = color;
  for (const ScreenPoint& point : screen) points.push_back(toImVec(point));
  const std::span<ImVec2> written(points.data() + primitive.firstPoint,
                                  screen.size());
  if (signedArea(written) < 0.0f) std::reverse(written.begin(), written.end());
  keys.push_back(
      {objectDepth, localDepth, static_cast<std::uint32_t>(primitives.size())});
  primitives.push_back(primitive);
}

void MatchRenderer3D::State::addLine(Vec3 from, Vec3 to, ImU32 color,
                                     float thickness, float objectDepth)
{
  ScreenPoint a;
  ScreenPoint b;
  if (!projection.project(from, a) || !projection.project(to, b)) return;
  Primitive primitive;
  primitive.firstPoint = static_cast<std::uint32_t>(points.size());
  primitive.pointCount = 2;
  primitive.kind = PrimitiveKind::LINE;
  primitive.color = color;
  primitive.size = std::max(
      1.0f, thickness * projection.focalPixels * 2.0f / (a.depth + b.depth));
  points.push_back(toImVec(a));
  points.push_back(toImVec(b));
  keys.push_back({objectDepth, (a.depth + b.depth) * 0.5f,
                  static_cast<std::uint32_t>(primitives.size())});
  primitives.push_back(primitive);
}

void MatchRenderer3D::State::addBox(const std::array<Vec3, 8>& corners,
                                    ImU32 color, float objectDepth)
{
  std::array<ScreenPoint, 8> screen{};
  for (std::size_t index = 0; index < corners.size(); ++index)
    if (!projection.project(corners[index], screen[index])) return;
  for (const auto& face : Stadium3D::BOX_FACES)
  {
    const Vec3& v0 = corners[face[0]];
    const Vec3 normal = RenderMath::cross(corners[face[2]] - v0,
                                          corners[face[3]] - corners[face[1]]);
    if (RenderMath::dot(normal, projection.eye - v0) <= 0.0f) continue;
    const std::array<ScreenPoint, 4> quad{screen[face[0]], screen[face[1]],
                                          screen[face[2]], screen[face[3]]};
    const float localDepth =
        (quad[0].depth + quad[1].depth + quad[2].depth + quad[3].depth) * 0.25f;
    addPolygon(
        quad, shadeColor(color, lighting(RenderMath::normalize(normal), light)),
        objectDepth, localDepth);
  }
}

void MatchRenderer3D::State::addPrism(std::span<const Vec3> bottom,
                                      std::span<const Vec3> top, ImU32 color,
                                      float objectDepth, bool capBottom,
                                      bool capTop)
{
  constexpr std::size_t MAX_SIDES = 8;
  const std::size_t sides = std::min({bottom.size(), top.size(), MAX_SIDES});
  if (sides < 3) return;
  std::array<ScreenPoint, MAX_SIDES> low{};
  std::array<ScreenPoint, MAX_SIDES> high{};
  Vec3 lowCentre;
  Vec3 highCentre;
  for (std::size_t index = 0; index < sides; ++index)
  {
    if (!projection.project(bottom[index], low[index]) ||
        !projection.project(top[index], high[index]))
      return;
    lowCentre = lowCentre + bottom[index];
    highCentre = highCentre + top[index];
  }
  const float inverse = 1.0f / static_cast<float>(sides);
  lowCentre = lowCentre * inverse;
  highCentre = highCentre * inverse;
  const Vec3 axisCentre = (lowCentre + highCentre) * 0.5f;
  for (std::size_t index = 0; index < sides; ++index)
  {
    const std::size_t next = (index + 1) % sides;
    const Vec3 faceCentre =
        (bottom[index] + bottom[next] + top[index] + top[next]) * 0.25f;
    Vec3 normal = RenderMath::cross(bottom[next] - bottom[index],
                                    top[index] - bottom[index]);
    if (RenderMath::dot(normal, faceCentre - axisCentre) < 0.0f)
      normal = normal * -1.0f;
    if (RenderMath::dot(normal, projection.eye - faceCentre) <= 0.0f) continue;
    const std::array<ScreenPoint, 4> quad{low[index], low[next], high[next],
                                          high[index]};
    addPolygon(
        quad, shadeColor(color, lighting(RenderMath::normalize(normal), light)),
        objectDepth,
        (quad[0].depth + quad[1].depth + quad[2].depth + quad[3].depth) *
            0.25f);
  }
  const auto cap = [&](const std::array<ScreenPoint, MAX_SIDES>& ring,
                       Vec3 centre, Vec3 outward)
  {
    if (RenderMath::dot(outward, projection.eye - centre) <= 0.0f) return;
    float depth = 0.0f;
    for (std::size_t index = 0; index < sides; ++index)
      depth += ring[index].depth;
    addPolygon(
        std::span<const ScreenPoint>(ring.data(), sides),
        shadeColor(color, lighting(RenderMath::normalize(outward), light)),
        objectDepth, depth * inverse);
  };
  if (capTop) cap(high, highCentre, highCentre - lowCentre);
  if (capBottom) cap(low, lowCentre, lowCentre - highCentre);
}

void MatchRenderer3D::State::orientedBox(Vec3 centre, Vec3 axisA, Vec3 axisB,
                                         Vec3 axisC, ImU32 color,
                                         float objectDepth)
{
  std::array<Vec3, 8> corners{};
  for (std::size_t corner = 0; corner < 8; ++corner)
  {
    corners[corner] = centre + axisA * ((corner & 1U) != 0U ? 1.0f : -1.0f) +
                      axisB * ((corner & 2U) != 0U ? 1.0f : -1.0f) +
                      axisC * ((corner & 4U) != 0U ? 1.0f : -1.0f);
  }
  addBox(corners, color, objectDepth);
}

void MatchRenderer3D::State::findBallHolder(const MatchRenderSnapshot& snapshot,
                                            float alpha)
{
  heldBy = nullptr;
  for (const MatchRenderPlayer& player : snapshot.players)
  {
    if (!player.possessesBall || !player.onPitch) continue;
    if (player.isGoalkeeper)
    {
      const GoalkeeperState keeperState = player.isHomeTeam
                                              ? snapshot.homeGoalkeeperState
                                              : snapshot.awayGoalkeeperState;
      if (keeperState == GoalkeeperState::HOLD) heldBy = &player;
    }
    else if (snapshot.state == MatchState::THROW_IN)
    {
      // The taker picks the ball up once he reaches the spot.
      const Vec3 root = worldOf(lerpRenderPosition(
          player.previousPosition, player.currentPosition, alpha));
      if (PlayerRig::flatDistance(root, ballWorld) <
          Tuning::Rig::THROW_PICKUP_METRES)
        heldBy = &player;
    }
    return;
  }
}

void MatchRenderer3D::State::updateMotion(const MatchRenderPlayer& player,
                                          AnimationSlot& slot, float alpha,
                                          float deltaSeconds)
{
  using P = Tuning::Player;
  using Q = Tuning::Pose;
  using R = Tuning::Rig;
  const Vec3 root = worldOf(lerpRenderPosition(
      player.previousPosition, player.currentPosition, alpha));
  const float yaw = rigYaw(player, alpha);
  if (slot.player != player.player)
  {
    slot = AnimationSlot{};
    slot.player = player.player;
    slot.lastPosition = root;
    slot.lastSpeed = player.speedMetresPerSecond;
    slot.lastTackleCooldown = player.tackleCooldown;
    slot.moveDirection = {std::cos(yaw), std::sin(yaw), 0.0f};
    if (player.player)
    {
      const std::uint32_t looks = cosmeticHash(player.player->getId());
      slot.phase = static_cast<float>(looks % 628U) / 100.0f;
      slot.leftFooted = (looks >> 12) % 5U == 0U;
      slot.handsOnHead = (looks >> 14) % 2U == 0U;
      slot.number = shirtNumberFor(player);
      slot.backName = player.player->getLastName();
      for (char& letter : slot.backName)
      {
        if (letter >= 'a' && letter <= 'z')
          letter = static_cast<char>(letter - 'a' + 'A');
      }
    }
    slot.looks = lookFor(player.player ? player.player->getId() : 0U);
  }
  const float scale =
      P::SCALE * playerHeightMetres(player) / P::REFERENCE_HEIGHT_METRES;
  // The gait (stride length, swing amplitude, idling) comes from the
  // simulated ground speed; the leg phase advances with the distance the
  // player actually covers on screen, so the planted boots match the body.
  Vec3 moved = root - slot.lastPosition;
  moved.z = 0.0f;
  float distance = RenderMath::length(moved);
  if (distance > P::TELEPORT_METRES)
  {
    distance = 0.0f;
    // A jump in the playback: put both feet down where the player is now.
    slot.feet[0].valid = false;
    slot.feet[1].valid = false;
  }
  slot.lastPosition = root;
  const float groundSpeed = player.speedMetresPerSecond;
  const float targetStride =
      groundSpeed < P::IDLE_SPEED
          ? 0.0f
          : std::clamp(groundSpeed / P::FULL_STRIDE_SPEED, 0.0f, 1.0f);
  slot.stride += (targetStride - slot.stride) *
                 RenderMath::dampingFactor(P::STRIDE_RATE, deltaSeconds);
  slot.gaitSpeed +=
      ((groundSpeed < P::IDLE_SPEED ? 0.0f : groundSpeed) - slot.gaitSpeed) *
      RenderMath::dampingFactor(Tuning::Gait::SPEED_RATE, deltaSeconds);
  const float cycleMetres =
      (P::STRIDE_BASE_METRES + P::STRIDE_PER_SPEED * groundSpeed) * scale;
  slot.phase =
      std::fmod(slot.phase + TWO_PI * std::min(distance / cycleMetres,
                                               P::MAX_CYCLES_PER_FRAME),
                TWO_PI);
  if (distance > 1e-4f && groundSpeed >= P::IDLE_SPEED)
  {
    const Vec3 heading = moved * (1.0f / distance);
    slot.moveDirection = RenderMath::normalize(RenderMath::lerp(
        slot.moveDirection, heading,
        RenderMath::dampingFactor(R::DIRECTION_RATE, deltaSeconds)));
  }

  // Acceleration and turn rate per simulated second, so a paused match
  // freezes the pose and fast playback does not exaggerate it.
  if (simSeconds > 0.0f)
  {
    const float rawAcceleration =
        std::clamp((groundSpeed - slot.lastSpeed) / simSeconds, -12.0f, 12.0f);
    slot.acceleration +=
        (rawAcceleration - slot.acceleration) *
        RenderMath::dampingFactor(Q::ACCEL_SMOOTHING, simSeconds);
    const float rawTurn =
        slot.hasYaw
            ? std::clamp(RenderMath::wrapAngle(yaw - slot.lastYaw) / simSeconds,
                         -Q::MAX_TURN_RATE, Q::MAX_TURN_RATE)
            : 0.0f;
    slot.turnRate += (rawTurn - slot.turnRate) *
                     RenderMath::dampingFactor(Q::TURN_SMOOTHING, simSeconds);
    if (slot.event != PlayerRig::Event::NONE)
    {
      slot.eventSeconds += simSeconds;
      if (slot.eventSeconds >= eventDuration(slot.event))
        slot.event = PlayerRig::Event::NONE;
    }
  }
  slot.lastSpeed = groundSpeed;
  slot.lastYaw = yaw;
  slot.hasYaw = true;

  // Rise to meet a high ball dropping in nearby (the header itself is the
  // touch event); keepers reach with their arms instead.
  float jumpTarget = 0.0f;
  if (livePlay && !player.isGoalkeeper && player.onPitch &&
      ballWorld.z >= R::HEADER_MIN_BALL && ballWorld.z <= R::HEADER_MAX_BALL &&
      PlayerRig::flatDistance(ballWorld, root) < R::JUMP_REACH)
  {
    const float headHeight = (P::REFERENCE_HEIGHT_METRES - 0.1f) * scale;
    jumpTarget = std::clamp(ballWorld.z - headHeight, 0.0f, R::MAX_JUMP);
  }
  if (livePlay && player.isGoalkeeper && player.onPitch && heldBy == nullptr &&
      PlayerRig::flatDistance(ballWorld, root) <
          playerHeightMetres(player) *
              MatchTuning::Goalkeeper::HAND_REACH_HEIGHT_SHARE)
  {
    const float standingReach = playerHeightMetres(player) *
                                MatchTuning::Aerial::GOALKEEPER_ARM_REACH_RATIO;
    jumpTarget =
        std::clamp(ballWorld.z - standingReach, 0.0F,
                   MatchTuning::Aerial::GOALKEEPER_BASE_JUMP_METRES +
                       MatchTuning::Aerial::GOALKEEPER_SKILL_JUMP_METRES);
  }
  if (simSeconds > 0.0f)
  {
    slot.jump += (jumpTarget - slot.jump) *
                 RenderMath::dampingFactor(R::JUMP_RATE, simSeconds);
  }

  // Keeper stances ease in and out.
  using K = Tuning::Keeper;
  const float blend = RenderMath::dampingFactor(K::DIVE_RATE, deltaSeconds);
  float setTarget = 0.0f;
  float holdTarget = 0.0f;
  float diveTarget = 0.0f;
  if (player.isGoalkeeper && player.onPitch)
  {
    const GoalkeeperState keeperState =
        player.isHomeTeam ? homeKeeperState : awayKeeperState;
    const Vec3 toBall = ballWorld - root;
    const float ballDistance = std::hypot(toBall.x, toBall.y);
    if (player.isDiving || keeperState == GoalkeeperState::DIVE)
    {
      if (slot.diveBlend < 0.05f)
      {
        const Vec3 side{-std::sin(yaw), std::cos(yaw), 0.0f};
        slot.diveSide = RenderMath::dot(toBall, side) >= 0.0f ? 1.0f : -1.0f;
      }
      diveTarget = 1.0f;
    }
    else if (player.possessesBall && keeperState == GoalkeeperState::HOLD)
    {
      // Ball in his hands (not at his feet for a goal kick or a dribble).
      holdTarget = 1.0f;
    }
    else if (livePlay && keeperState == GoalkeeperState::SET_POSITION &&
             ballDistance < K::SET_DISTANCE)
    {
      setTarget = 1.0f;
    }
  }
  // Down after a collision: the same easing lays him on his side. Getting
  // up, he rises over the time it takes.
  if (player.fallen)
  {
    setTarget = 0.0f;
    holdTarget = 0.0f;
    diveTarget = 1.0f - player.getUpShare;
  }
  slot.setBlend += (setTarget - slot.setBlend) * blend;
  slot.holdBlend += (holdTarget - slot.holdBlend) * blend;
  slot.diveBlend += (diveTarget - slot.diveBlend) * blend;

  // Foot planting: the boots stay where they were put down while the body
  // passes over them, and standing players only shuffle when they drift.
  const Vec3 flatSide{-std::sin(yaw), std::cos(yaw), 0.0f};
  const float duty = PlayerRig::dutyFactor(slot.gaitSpeed);
  const bool idle =
      groundSpeed < P::IDLE_SPEED && slot.gaitSpeed < R::IDLE_GAIT_SPEED;
  const float stance = R::STANCE_WIDTH + slot.setBlend * K::SET_FOOT_SPREAD;
  for (std::size_t leg = 0; leg < 2; ++leg)
  {
    PlayerRig::StrideInput stride;
    stride.rest =
        root + flatSide * ((leg == 0 ? 1.0f : -1.0f) * stance * scale);
    stride.rest.z = 0.0f;
    stride.forward = slot.moveDirection;
    stride.yaw = yaw;
    stride.legPhase = std::fmod(
        slot.phase + (leg == 0 ? 0.0f : std::numbers::pi_v<float>), TWO_PI);
    stride.duty = duty;
    stride.stanceLength = duty * cycleMetres;
    stride.frontReach = R::FRONT_REACH * scale;
    stride.liftHeight =
        (R::WALK_LIFT + (R::SPRINT_LIFT - R::WALK_LIFT) * slot.stride) * scale;
    stride.maxDrift = R::MAX_DRIFT * scale;
    stride.idle = idle;
    stride.idleStepMetres = R::IDLE_STEP_METRES * scale;
    stride.stepDuration = R::IDLE_STEP_SECONDS;
    stride.otherStepping = slot.feet[1 - leg].stepSeconds > 0.0f;
    stride.deltaSeconds = simSeconds;
    PlayerRig::stepFoot(slot.feet[leg], stride);
  }
  slot.stanceLength = duty * cycleMetres;
  slot.duty = duty;
}

int MatchRenderer3D::State::shirtNumberFor(const MatchRenderPlayer& player)
{
  // Shared with the 2D view, so both show the same numbers.
  return numbers.numberFor(player, playerStats);
}

GoalMood MatchRenderer3D::State::goalMoodFor(
    const MatchRenderPlayer& player) const
{
  if (!goal.active || !player.onPitch) return GoalMood::NONE;
  if (player.isHomeTeam != goal.byHome) return GoalMood::DEJECTED;
  if (!goal.ownGoal && player.player && player.player->getId() == goal.scorer)
    return GoalMood::SCORER;
  return GoalMood::CELEBRATING;
}

void MatchRenderer3D::State::addSegment(Vec3 from, Vec3 to, float fromHalf,
                                        float toHalf, ImU32 color, Vec3 hint,
                                        float objectDepth, PlayerLod lod)
{
  using P = Tuning::Player;
  const Vec3 span = to - from;
  const float spanLength = RenderMath::length(span);
  if (spanLength < 1e-4f) return;
  const Vec3 along = span * (1.0f / spanLength);
  if (lod == PlayerLod::FAR)
  {
    addLine(from, to, color, fromHalf + toHalf, objectDepth);
    return;
  }
  Vec3 acrossA = RenderMath::cross(along, hint);
  if (RenderMath::dot(acrossA, acrossA) < 0.04f)
    acrossA = RenderMath::cross(along, UP);
  if (RenderMath::dot(acrossA, acrossA) < 0.04f)
    acrossA = RenderMath::cross(along, Vec3{1.0f, 0.0f, 0.0f});
  acrossA = RenderMath::normalize(acrossA);
  const Vec3 acrossB = RenderMath::cross(along, acrossA);
  if (lod == PlayerLod::NEAR)
  {
    std::array<Vec3, P::LIMB_SIDES> top{};
    std::array<Vec3, P::LIMB_SIDES> bottom{};
    for (std::size_t side = 0; side < top.size(); ++side)
    {
      const float angle = TWO_PI * (static_cast<float>(side) + 0.5f) /
                          static_cast<float>(top.size());
      const Vec3 ring = acrossA * std::cos(angle) + acrossB * std::sin(angle);
      top[side] = from + ring * fromHalf;
      bottom[side] = to + ring * toHalf;
    }
    addPrism(bottom, top, color, objectDepth, true, true);
    return;
  }
  // Mid distance: a tapered four-sided limb.
  std::array<Vec3, 8> corners{};
  for (std::size_t corner = 0; corner < 8; ++corner)
  {
    const bool far = (corner & 4U) != 0U;
    const float half = far ? toHalf : fromHalf;
    corners[corner] = (far ? to : from) +
                      acrossA * (((corner & 1U) != 0U ? 1.0f : -1.0f) * half) +
                      acrossB * (((corner & 2U) != 0U ? 1.0f : -1.0f) * half);
  }
  addBox(corners, color, objectDepth);
}

void MatchRenderer3D::State::addPlayer(const MatchRenderPlayer& player,
                                       AnimationSlot& slot, float alpha)
{
  using P = Tuning::Player;
  using Q = Tuning::Pose;
  using R = Tuning::Rig;
  using PlayerRig::Event;
  // In a jump, his whole body is lifted (his shadow stays on the ground).
  const Vec3 root = worldOf(
      lerpRenderPosition(player.previousPosition, player.currentPosition, alpha),
      player.previousFeetHeightMetres +
          (player.currentFeetHeightMetres - player.previousFeetHeightMetres) * alpha);
  ScreenPoint anchor;
  if (!projection.project(root + UP * P::HIP_HEIGHT, anchor)) return;
  const RenderMath::ScreenRect& rect = projection.rect;
  if (anchor.x < rect.x - P::CULL_MARGIN_PIXELS ||
      anchor.x > rect.x + rect.width + P::CULL_MARGIN_PIXELS ||
      anchor.y < rect.y - P::CULL_MARGIN_PIXELS ||
      anchor.y > rect.y + rect.height + P::CULL_MARGIN_PIXELS)
    return;

  // Out of the sun (a stand's shadow by day) only the sky lights him.
  const LightRig sceneLight = light;
  if (day && geometry.inStandShadow(root))
  {
    light.diffuse = 0.0f;
    light.ambient = Tuning::Day::SHADE_AMBIENT;
  }
  const bool keeper = player.isGoalkeeper;
  const std::size_t kitIndex =
      (player.isHomeTeam ? 0U : 2U) + (keeper ? 1U : 0U);
  const KitColors& kit = kitList[kitIndex];
  const PlayerLook& look = slot.looks;
  const ImU32 skin = look.skin;
  const ImU32 hands = keeper ? gloveColors[kitIndex] : skin;
  const float heightMetres = playerHeightMetres(player);
  const float scale = P::SCALE * heightMetres / P::REFERENCE_HEIGHT_METRES;
  const float pixelHeight =
      heightMetres * projection.focalPixels / anchor.depth;
  const PlayerLod lod = pixelHeight < R::FAR_PIXELS         ? PlayerLod::FAR
                        : pixelHeight < P::ROUND_MIN_PIXELS ? PlayerLod::MID
                                                            : PlayerLod::NEAR;
  const bool detailed = pixelHeight >= P::HAND_MIN_PIXELS;
  // Broad-shouldered or slight: widths vary a little from player to player.
  const float bulk = scale * look.bulk;

  const float yaw = rigYaw(player, alpha);
  const Vec3 forward{std::cos(yaw), std::sin(yaw), 0.0f};
  const Vec3 flatSide{-forward.y, forward.x, 0.0f};
  const float stride = slot.stride;
  const GoalMood mood = goalMoodFor(player);
  const bool celebrating = mood != GoalMood::NONE && celebrationWeight > 0.0f;

  // --- action -------------------------------------------------------------
  PlayerRig::ActionInput actionInput;
  actionInput.speed = slot.gaitSpeed;
  actionInput.turnRate = slot.turnRate;
  actionInput.event = slot.event;
  actionInput.goalkeeper = keeper;
  actionInput.diving = slot.diveBlend > 0.5f;
  actionInput.holding = slot.holdBlend > 0.5f;
  actionInput.keeperSet = slot.setBlend > 0.5f;
  actionInput.throwIn = heldBy == &player && !keeper;
  actionInput.mood = !celebrating                 ? PlayerRig::Mood::NONE
                     : mood == GoalMood::DEJECTED ? PlayerRig::Mood::DEJECTED
                                                  : PlayerRig::Mood::CELEBRATE;
  const PlayerRig::Action action = PlayerRig::selectAction(actionInput);

  // --- upper-body pose ------------------------------------------------------
  // Walk, jog and sprint blend with the smoothed speed; below a walk the
  // swing fades into the idle stance. Arms swing against the planted legs.
  const Tuning::Gait::Shape gait = gaitShape(slot.gaitSpeed);
  const float motion =
      std::clamp(slot.gaitSpeed / Tuning::Gait::WALK.speed, 0.0f, 1.0f);
  const float halfStance = std::max(slot.stanceLength * 0.5f, 0.1f);
  Pose pose;
  for (std::size_t index = 0; index < 2; ++index)
  {
    const float outward = index == 0 ? 1.0f : -1.0f;
    const float legForward = std::clamp(
        RenderMath::dot(slot.feet[index].position - root, forward) / halfStance,
        -1.0f, 1.0f);
    pose.armSwing[index] = -motion * gait.arm * legForward;
    pose.armSpread[index] = outward * 0.08f;
    pose.elbow[index] = P::ELBOW_BEND + (gait.elbow - P::ELBOW_BEND) * motion;
  }
  // Lean into acceleration, sit back when braking, bank into turns.
  const float accelerationLean = std::clamp(
      slot.acceleration * Q::ACCEL_LEAN, -Q::MAX_BRAKE_LEAN, Q::MAX_ACCEL_LEAN);
  pose.lean = motion * gait.lean + accelerationLean;
  pose.turnRoll =
      std::clamp(slot.turnRate * player.speedMetresPerSecond * Q::TURN_LEAN,
                 -Q::MAX_TURN_LEAN, Q::MAX_TURN_LEAN);
  // Sharp turns lead with the shoulders.
  pose.twist = std::clamp(slot.turnRate * Q::TWIST_PER_TURN_RATE, -Q::MAX_TWIST,
                          Q::MAX_TWIST);
  const float stop = std::clamp(
      (-slot.acceleration - Q::STOP_DECELERATION) / Q::STOP_DECELERATION, 0.0f,
      1.0f);
  pose.hipDrop += stop * Q::STOP_HIP_DROP;
  pose.lift = slot.jump;

  // Timed ball actions: weight of the curve that owns the acting leg.
  const float eventTime = slot.eventSeconds;
  const float eventWeight = eventEnvelope(slot.event, eventTime);
  switch (slot.event)
  {
    case Event::PASS:
    case Event::SHOT:
    case Event::CROSS:
    {
      using Kk = Tuning::Kick;
      Pose kick = pose;
      kick.lean = slot.event == Event::PASS ? Kk::PASS_LEAN : Kk::TORSO_LEAN;
      for (std::size_t index = 0; index < 2; ++index)
      {
        const float outward = index == 0 ? 1.0f : -1.0f;
        // The arm opposite the kicking leg reaches forward for balance.
        kick.armSwing[index] = index == slot.eventLeg ? -0.25f : 0.5f;
        kick.armSpread[index] = outward * Kk::ARM_ABDUCTION;
        kick.elbow[index] = 0.4f;
      }
      pose = mixPose(pose, kick, eventWeight);
      break;
    }
    case Event::HEADER:
    {
      using Kk = Tuning::Kick;
      Pose header = pose;
      // Snap the head through the ball, then recover.
      const float nod =
          std::clamp(eventTime / Kk::HEADER_NOD_SECONDS, 0.0f, 1.0f);
      header.lean = -0.25f + (Kk::HEADER_NOD + 0.25f) * nod;
      for (std::size_t index = 0; index < 2; ++index)
      {
        const float outward = index == 0 ? 1.0f : -1.0f;
        header.armSwing[index] = 0.9f;
        header.armSpread[index] = outward * 0.6f;
        header.elbow[index] = 0.9f;
      }
      pose = mixPose(pose, header, eventWeight);
      break;
    }
    case Event::TACKLE:
    {
      Pose tackle = pose;
      tackle.lean = R::TACKLE_LEAN;
      tackle.hipDrop = R::TACKLE_HIP_DROP;
      for (std::size_t index = 0; index < 2; ++index)
      {
        const float outward = index == 0 ? 1.0f : -1.0f;
        tackle.armSwing[index] = index == slot.eventLeg ? -0.3f : 0.35f;
        tackle.armSpread[index] = outward * 0.7f;
        tackle.elbow[index] = 0.5f;
      }
      pose = mixPose(pose, tackle, eventWeight);
      break;
    }
    case Event::SLIDE:
    {
      Pose slide = pose;
      slide.lean = R::SLIDE_LEAN;
      slide.hipDrop = P::HIP_HEIGHT - R::SLIDE_PELVIS;
      slide.lift = 0.0f;
      for (std::size_t index = 0; index < 2; ++index)
      {
        const float outward = index == 0 ? 1.0f : -1.0f;
        // Hands back towards the grass to break the fall.
        slide.armSwing[index] = -0.7f;
        slide.armSpread[index] = outward * 0.65f;
        slide.elbow[index] = 0.2f;
      }
      pose = mixPose(pose, slide, eventWeight);
      break;
    }
    case Event::THROW:
    case Event::NONE:
      break;
  }

  if (keeper)
  {
    using K = Tuning::Keeper;
    if (slot.setBlend > 0.01f)
    {
      Pose set = pose;
      set.hipDrop = K::SET_HIP_DROP;
      set.lean = K::SET_LEAN;
      for (std::size_t index = 0; index < 2; ++index)
      {
        const float outward = index == 0 ? 1.0f : -1.0f;
        set.armSwing[index] = K::SET_ARM_FORWARD;
        set.armSpread[index] = outward * K::SET_ARM_SPREAD;
        set.elbow[index] = 0.45f;
      }
      pose = mixPose(pose, set, slot.setBlend * (1.0f - stride));
    }
    if (slot.diveBlend > 0.01f)
    {
      Pose dive = pose;
      dive.bodyRoll = slot.diveSide * K::DIVE_ROLL;
      dive.lift = K::DIVE_LIFT;
      dive.lean = 0.0f;
      dive.turnRoll = 0.0f;
      dive.twist = 0.0f;
      dive.hipDrop = 0.0f;
      for (std::size_t index = 0; index < 2; ++index)
      {
        const float outward = index == 0 ? 1.0f : -1.0f;
        dive.thigh[index] = index == 0 ? 0.15f : -0.1f;
        dive.knee[index] = -0.25f;
        dive.legSpread[index] = outward * 0.06f;
        dive.armSwing[index] = std::numbers::pi_v<float> - 0.1f;
        dive.armSpread[index] = outward * 0.12f;
        dive.elbow[index] = 0.05f;
      }
      pose = mixPose(pose, dive, slot.diveBlend);
    }
  }
  if (player.fallen && slot.diveBlend > 0.01f)
  {
    // Lying on his side along the ground, legs out, arms loose.
    Pose lie = pose;
    lie.bodyRoll = 0.5f * std::numbers::pi_v<float>;
    lie.lift = Tuning::Keeper::FALLEN_LIFT;
    lie.lean = 0.0f;
    lie.turnRoll = 0.0f;
    lie.twist = 0.0f;
    lie.hipDrop = 0.0f;
    for (std::size_t index = 0; index < 2; ++index)
    {
      const float outward = index == 0 ? 1.0f : -1.0f;
      lie.thigh[index] = index == 0 ? 0.25f : -0.05f;
      lie.knee[index] = index == 0 ? -0.6f : -0.2f;
      lie.legSpread[index] = outward * 0.05f;
      lie.armSwing[index] = 0.4f;
      lie.armSpread[index] = outward * 0.3f;
      lie.elbow[index] = 0.6f;
    }
    pose = mixPose(pose, lie, slot.diveBlend);
  }

  if (celebrating)
  {
    using C = Tuning::Celebration;
    Pose react = pose;
    if (mood == GoalMood::DEJECTED)
    {
      react.lean = C::DEJECTED_LEAN;
      for (std::size_t index = 0; index < 2; ++index)
      {
        react.armSwing[index] = 0.05f;
        react.armSpread[index] = (index == 0 ? 1.0f : -1.0f) * 0.06f;
        react.elbow[index] = 0.15f;
      }
    }
    else
    {
      const float hop =
          reducedMotion
              ? 0.0f
              : C::HOP * (1.0f - stride) *
                    std::abs(std::sin(elapsedSeconds * C::HOP_SPEED +
                                      static_cast<float>(look.seed % 7U)));
      react.lift = std::max(react.lift, hop);
      react.lean = std::min(react.lean, 0.1f);
      for (std::size_t index = 0; index < 2; ++index)
      {
        const float outward = index == 0 ? 1.0f : -1.0f;
        if (mood == GoalMood::SCORER)
        {
          // Arms out like wings on the run to the corner flag.
          react.armSwing[index] = 0.15f;
          react.armSpread[index] = outward * C::WINGS_SPREAD;
          react.elbow[index] = 0.05f;
        }
        else
        {
          react.armSwing[index] = C::ARMS_UP;
          react.armSpread[index] = outward * C::ARMS_SPREAD;
          react.elbow[index] = 0.2f;
        }
      }
    }
    pose = mixPose(pose, react, celebrationWeight);
  }

  // --- skeleton -----------------------------------------------------------
  // Running drops the pelvis while a foot is down and floats it in flight;
  // walking vaults over the stance leg.
  const float bobSign = slot.duty > 0.5f ? 1.0f : -1.0f;
  const float bob =
      bobSign * motion * gait.bob *
      std::cos(2.0f * (slot.phase - std::numbers::pi_v<float> * slot.duty));
  const float crouch = R::RUN_CROUCH * slot.stride;
  const float roll = pose.turnRoll + pose.bodyRoll;
  const Vec3 bodyUp = UP * std::cos(roll) + flatSide * std::sin(roll);
  const Vec3 bodySide = flatSide * std::cos(roll) - UP * std::sin(roll);
  // Turns bank from the boots; dives pivot around the hips.
  const Vec3 hipUp =
      UP * std::cos(pose.bodyRoll) + flatSide * std::sin(pose.bodyRoll);
  float pelvisHeight =
      (P::HIP_HEIGHT - pose.hipDrop - crouch + bob + pose.lift) * scale;
  // Sink just enough that a foot planted out in front stays reachable.
  const float reach = (P::THIGH_LENGTH + P::SHIN_LENGTH) * scale * 0.995f;
  const float ankleHeight = R::ANKLE_HEIGHT * scale;
  if (pose.lift <= 0.01f && slot.diveBlend < 0.05f)
  {
    float lowest = pelvisHeight - R::MAX_PELVIS_DROP * scale;
    for (std::size_t index = 0; index < 2; ++index)
    {
      const PlayerRig::FootState& foot = slot.feet[index];
      if (!foot.inStance) continue;
      const Vec3 hip = root + flatSide * ((index == 0 ? 1.0f : -1.0f) *
                                          P::HIP_SPREAD * scale);
      const float ahead = RenderMath::dot(foot.position - hip, forward);
      if (ahead <= 0.0f) continue;
      const float flat = PlayerRig::flatDistance(foot.position, hip);
      const float highest =
          ankleHeight + std::sqrt(std::max(reach * reach - flat * flat, 0.0f));
      pelvisHeight = std::max(lowest, std::min(pelvisHeight, highest));
    }
  }
  // Stepping into a strike or a block: the body leans out towards a ball
  // beyond easy reach (drawn only; the player stays where the engine has
  // him).
  Vec3 lunge;
  if (eventWeight > 0.0f && slot.event != Event::HEADER &&
      slot.event != Event::THROW && slot.event != Event::SLIDE)
  {
    const Vec3 toContact{slot.contact.x - root.x, slot.contact.y - root.y,
                         0.0f};
    const float away = RenderMath::length(toContact);
    if (away > 1e-3f)
    {
      lunge = toContact * (std::clamp(away - R::LUNGE_FREE * scale, 0.0f,
                                      R::LUNGE_MAX * scale) *
                           eventWeight / away);
    }
  }
  const Vec3 pelvis =
      root + lunge + UP * pelvisHeight + (UP - hipUp) * (P::HIP_HEIGHT * scale);
  const float objectDepth = anchor.depth;

  // Leg targets: the planted (or swinging) feet, lifted with the body when
  // airborne, bent by the keeper's dive, and handed to the kick or tackle
  // curve while one plays.
  const BodyFrame legFrame{bodyUp, forward, bodySide};
  std::array<Vec3, 2> hipJoint{};
  std::array<Vec3, 2> ankleTarget{};
  std::array<float, 2> heel{};
  std::array<bool, 2> planted{};
  for (std::size_t index = 0; index < 2; ++index)
  {
    const float sideSign = index == 0 ? 1.0f : -1.0f;
    hipJoint[index] = pelvis + bodySide * (sideSign * P::HIP_SPREAD * scale);
    const PlayerRig::FootState& foot = slot.feet[index];
    Vec3 target = foot.position + UP * ankleHeight;
    planted[index] = foot.inStance && pose.lift <= 0.01f;
    if (pose.lift > 0.0f)
    {
      // Feet tuck under the body in the air.
      target = target + UP * (pose.lift * scale * R::JUMP_TUCK) +
               (root - foot.position) *
                   (R::JUMP_GATHER * std::min(pose.lift * 4.0f, 1.0f));
      target.z = std::max(target.z, foot.position.z + ankleHeight);
    }
    if (slot.diveBlend > 0.01f)
    {
      const Vec3 thighDirection =
          limbDirection(legFrame, pose.thigh[index], pose.legSpread[index]);
      const Vec3 shinDirection =
          limbDirection(legFrame, pose.thigh[index] + pose.knee[index],
                        pose.legSpread[index]);
      const Vec3 fk = hipJoint[index] +
                      thighDirection * (P::THIGH_LENGTH * scale) +
                      shinDirection * (P::SHIN_LENGTH * scale);
      target = RenderMath::lerp(target, fk, slot.diveBlend);
      planted[index] = planted[index] && slot.diveBlend < 0.5f;
    }
    ankleTarget[index] = target;
  }
  const Vec3 eventSide{-slot.eventDirection.y, slot.eventDirection.x, 0.0f};
  if (eventWeight > 0.0f)
  {
    const std::size_t leg = slot.eventLeg;
    const float outward = leg == 0 ? 1.0f : -1.0f;
    Vec3 target = ankleTarget[leg];
    float weight = eventWeight;
    switch (slot.event)
    {
      case Event::PASS:
      case Event::SHOT:
      case Event::CROSS:
      {
        using Kk = Tuning::Kick;
        // The instep meets the ball: the ankle just behind and above it.
        const Vec3 contact =
            slot.contact - slot.eventDirection * (Kk::CONTACT_BEHIND * scale) +
            UP * (Kk::CONTACT_ABOVE * scale);
        target = PlayerRig::kickAnkle(slot.event, eventTime, contact,
                                      slot.eventDirection, eventSide * -outward,
                                      kickTiming(), weight);
        break;
      }
      case Event::TACKLE:
        target = Vec3{slot.contact.x, slot.contact.y, ankleHeight};
        break;
      case Event::SLIDE:
      {
        // Lead leg out along the grass towards the ball, the other folded.
        const Vec3 ground{pelvis.x, pelvis.y, 0.0f};
        target = ground + slot.eventDirection * (R::SLIDE_LEG_REACH * scale) +
                 UP * ankleHeight;
        const std::size_t other = 1 - leg;
        const Vec3 tucked =
            ground + slot.eventDirection * (R::SLIDE_TUCK_REACH * scale) +
            eventSide * (outward * -0.18f * scale) + UP * ankleHeight;
        ankleTarget[other] =
            RenderMath::lerp(ankleTarget[other], tucked, eventWeight);
        planted[other] = false;
        break;
      }
      case Event::HEADER:
      case Event::THROW:
      case Event::NONE:
        weight = 0.0f;
        break;
    }
    if (weight > 0.0f)
    {
      ankleTarget[leg] = RenderMath::lerp(ankleTarget[leg], target, weight);
      planted[leg] = false;
    }
  }

  // --- legs ---------------------------------------------------------------
  std::array<Vec3, 2> knee{};
  std::array<Vec3, 2> ankle{};
  const Vec3 kneePole = forward + bodyUp * 0.15f;
  for (std::size_t index = 0; index < 2; ++index)
  {
    Vec3 target = ankleTarget[index];
    const float footYaw = slot.feet[index].yaw;
    const Vec3 footForward{std::cos(footYaw), std::sin(footYaw), 0.0f};
    if (planted[index])
    {
      heel[index] = PlayerRig::rollOntoToes(target, hipJoint[index],
                                            footForward, R::TOE_LENGTH * scale,
                                            reach, R::MAX_HEEL_ANGLE);
    }
    const PlayerRig::TwoBone leg = PlayerRig::solveTwoBone(
        hipJoint[index], target, P::THIGH_LENGTH * scale,
        P::SHIN_LENGTH * scale, kneePole);
    knee[index] = leg.middle;
    ankle[index] = leg.end;
  }

  const Vec3 shinHint = bodySide;
  for (std::size_t index = 0; index < 2; ++index)
  {
    const float footYaw = slot.feet[index].yaw;
    Vec3 footForward{std::cos(footYaw), std::sin(footYaw), 0.0f};
    if (!planted[index])
    {
      // In the air the toes point down along the shin's swing.
      footForward = RenderMath::normalize(footForward - UP * R::SWING_TOE_DROP);
    }
    else if (heel[index] > 0.0f)
    {
      footForward = RenderMath::normalize(footForward * std::cos(heel[index]) -
                                          UP * std::sin(heel[index]));
    }
    // Thigh: the shorts leg over the top, bare above the knee.
    const Vec3 hem =
        RenderMath::lerp(hipJoint[index], knee[index], P::SHORTS_LEG_SHARE);
    if (lod == PlayerLod::FAR)
    {
      addSegment(hipJoint[index], knee[index], P::THIGH_TOP * bulk,
                 P::THIGH_BOTTOM * bulk, kit.shorts, shinHint, objectDepth,
                 lod);
      addSegment(knee[index], ankle[index], P::CALF_TOP * bulk,
                 P::ANKLE_HALF_WIDTH * bulk, kit.socks, shinHint, objectDepth,
                 lod);
      continue;
    }
    addSegment(hipJoint[index], hem, P::SHORTS_LEG_TOP * bulk,
               P::SHORTS_LEG_HEM * bulk, kit.shorts, shinHint, objectDepth,
               lod);
    addSegment(hem, knee[index], P::THIGH_MIDDLE * bulk, P::THIGH_BOTTOM * bulk,
               skin, shinHint, objectDepth, lod);
    // Socks pulled up to the knee with a turned-down band.
    const Vec3 band =
        RenderMath::lerp(knee[index], ankle[index], P::SOCK_BAND_SHARE);
    if (lod == PlayerLod::NEAR)
    {
      addSegment(knee[index], band, P::CALF_TOP * bulk * 1.06f,
                 P::CALF_TOP * bulk * 1.08f, kit.trim, shinHint, objectDepth,
                 lod);
      addSegment(band, ankle[index], P::CALF_TOP * bulk,
                 P::ANKLE_HALF_WIDTH * bulk, kit.socks, shinHint, objectDepth,
                 lod);
    }
    else
    {
      addSegment(knee[index], ankle[index], P::CALF_TOP * bulk,
                 P::ANKLE_HALF_WIDTH * bulk, kit.socks, shinHint, objectDepth,
                 lod);
    }
    // Boot from heel to toe, the sole underneath.
    const Vec3 footSide =
        RenderMath::normalize(RenderMath::cross(UP, footForward));
    const Vec3 footUp = RenderMath::cross(footForward, footSide);
    const Vec3 bootCentre = ankle[index] +
                            footForward * (P::BOOT_FORWARD * scale) -
                            footUp * ((R::ANKLE_HEIGHT - P::BOOT_HALF_HEIGHT -
                                       P::SOLE_HALF_HEIGHT * 2.0f) *
                                      scale);
    std::array<Vec3, 8> boot{};
    for (std::size_t corner = 0; corner < 8; ++corner)
    {
      const bool toe = (corner & 1U) != 0U;
      // The toe box narrows and drops a little.
      const float width =
          (toe ? P::BOOT_TOE_WIDTH : P::BOOT_HALF_WIDTH) * scale;
      const float height = ((corner & 4U) != 0U ? (toe ? P::BOOT_TOE_HEIGHT
                                                       : P::BOOT_HALF_HEIGHT)
                                                : -P::BOOT_HALF_HEIGHT) *
                           scale;
      boot[corner] =
          bootCentre +
          footForward * ((toe ? 1.0f : -1.0f) * P::BOOT_HALF_LENGTH * scale) +
          footSide * (((corner & 2U) != 0U ? 1.0f : -1.0f) * width) +
          footUp * height;
    }
    addBox(boot, look.boots, objectDepth);
    if (lod == PlayerLod::NEAR)
    {
      std::array<Vec3, 8> sole{};
      for (std::size_t corner = 0; corner < 8; ++corner)
      {
        sole[corner] =
            boot[corner & 3U] -
            footUp * ((corner & 4U) != 0U ? 0.0f
                                          : P::SOLE_HALF_HEIGHT * 2.0f * scale);
      }
      addBox(sole, P::SOLE_COLOR, objectDepth);
    }
  }

  // --- pelvis and torso
  // -------------------------------------------------------
  const Vec3 twistedForward =
      forward * std::cos(pose.twist) + bodySide * std::sin(pose.twist);
  const Vec3 twistedSide =
      bodySide * std::cos(pose.twist) - forward * std::sin(pose.twist);
  const Vec3 torsoUp =
      bodyUp * std::cos(pose.lean) + twistedForward * std::sin(pose.lean);
  const Vec3 torsoForward =
      twistedForward * std::cos(pose.lean) - bodyUp * std::sin(pose.lean);
  const Vec3 torsoSide = RenderMath::normalize(twistedSide);
  const Vec3 hips =
      pelvis + bodyUp * ((P::SHORTS_HEIGHT - P::HIP_HEIGHT) * scale);
  const Vec3 waist =
      pelvis + bodyUp * ((P::TORSO_BASE - P::HIP_HEIGHT) * scale);
  const Vec3 chest = waist + torsoUp * (P::TORSO_LENGTH * scale);
  if (lod == PlayerLod::FAR)
  {
    addSegment(hips - bodyUp * (P::SHORTS_HALF_HEIGHT * scale), waist,
               P::SHORTS_HALF_WIDTH * bulk, P::SHORTS_HALF_WIDTH * bulk,
               kit.shorts, forward, objectDepth, lod);
  }
  else
  {
    orientedBox(hips, forward * (P::SHORTS_HALF_DEPTH * bulk),
                bodySide * (P::SHORTS_HALF_WIDTH * bulk),
                bodyUp * (P::SHORTS_HALF_HEIGHT * scale), kit.shorts,
                objectDepth);
  }
  if (lod == PlayerLod::NEAR)
  {
    // A chamfered section: rounder flanks, a broad flat back for the print.
    constexpr std::array<std::array<float, 2>, P::TORSO_SIDES> SECTION{{
        {0.72f, 1.0f},
        {1.0f, 0.55f},
        {1.0f, -0.55f},
        {0.72f, -1.0f},
        {-0.72f, -1.0f},
        {-1.0f, -0.55f},
        {-1.0f, 0.55f},
        {-0.72f, 1.0f},
    }};
    std::array<Vec3, P::TORSO_SIDES> lower{};
    std::array<Vec3, P::TORSO_SIDES> middle{};
    std::array<Vec3, P::TORSO_SIDES> upper{};
    const Vec3 ribs =
        waist + torsoUp * (P::TORSO_LENGTH * P::RIB_SHARE * scale);
    for (std::size_t corner = 0; corner < SECTION.size(); ++corner)
    {
      const auto ring = [&](Vec3 centre, float width, float depth)
      {
        return centre + torsoSide * (SECTION[corner][0] * width * bulk) +
               torsoForward * (SECTION[corner][1] * depth * bulk);
      };
      lower[corner] = ring(waist, P::WAIST_HALF_WIDTH, P::WAIST_HALF_DEPTH);
      middle[corner] = ring(ribs, P::RIB_HALF_WIDTH, P::CHEST_HALF_DEPTH);
      upper[corner] =
          ring(chest, P::CHEST_HALF_WIDTH, P::CHEST_HALF_DEPTH * 0.9f);
    }
    addPrism(lower, middle, kit.shirt, objectDepth, false, false);
    addPrism(middle, upper, kit.shirt, objectDepth, false, true);
  }
  else
  {
    std::array<Vec3, 8> torso{};
    for (std::size_t corner = 0; corner < 8; ++corner)
    {
      const bool upper = (corner & 4U) != 0U;
      const float depth =
          (upper ? P::CHEST_HALF_DEPTH : P::WAIST_HALF_DEPTH) * bulk;
      const float width =
          (upper ? P::CHEST_HALF_WIDTH : P::WAIST_HALF_WIDTH) * bulk;
      torso[corner] = (upper ? chest : waist) +
                      torsoForward * ((corner & 1U) != 0U ? depth : -depth) +
                      torsoSide * ((corner & 2U) != 0U ? width : -width);
    }
    addBox(torso, kit.shirt, objectDepth);
  }
  if (detailed)
  {
    orientedBox(chest, torsoForward * (P::COLLAR_HALF_SIZE * scale),
                torsoSide * (P::COLLAR_HALF_SIZE * scale),
                torsoUp * (P::COLLAR_HALF_HEIGHT * scale), kit.trim,
                objectDepth);
  }
  if (lod != PlayerLod::FAR)
  {
    addBackPrint(slot, kitIndex, waist, torsoUp, torsoForward, torsoSide, scale,
                 objectDepth);
  }

  // --- arms -----------------------------------------------------------------
  const Vec3 headCentre = chest + torsoUp * (P::NECK_LENGTH * scale);
  const BodyFrame armFrame{torsoUp, torsoForward, torsoSide};
  std::array<Vec3, 2> handTarget{};
  float handWeight = 0.0f;
  if (action == PlayerRig::Action::THROW_IN || slot.event == Event::THROW)
  {
    // Both hands over the head; the throw whips them forward.
    const float throwShare =
        slot.event == Event::THROW
            ? std::clamp(eventTime / R::THROW_WHIP_SECONDS, 0.0f, 1.0f)
            : 0.0f;
    const Vec3 grip =
        headCentre + torsoUp * (R::THROW_GRIP_UP * scale) +
        torsoForward * ((R::THROW_GRIP_BACK +
                         (R::THROW_RELEASE_FORWARD - R::THROW_GRIP_BACK) *
                             PlayerRig::smoothStep(throwShare)) *
                        scale);
    handTarget = {grip + torsoSide * (R::HAND_GRIP_HALF * scale),
                  grip - torsoSide * (R::HAND_GRIP_HALF * scale)};
    handWeight = slot.event == Event::THROW ? eventWeight : 1.0f;
    if (heldBy == &player)
    {
      heldBallWorld = grip + torsoUp * (0.02f * scale);
      heldBallPlaced = true;
    }
  }
  else if (keeper && slot.holdBlend > 0.01f && slot.diveBlend < 0.5f)
  {
    // The ball gathered into the chest.
    const Vec3 grip = chest - torsoUp * (R::KEEPER_GRIP_DOWN * scale) +
                      torsoForward * (R::KEEPER_GRIP_FORWARD * scale);
    handTarget = {grip + torsoSide * (R::HAND_GRIP_HALF * scale),
                  grip - torsoSide * (R::HAND_GRIP_HALF * scale)};
    handWeight = slot.holdBlend;
    if (heldBy == &player)
    {
      heldBallWorld = grip;
      heldBallPlaced = true;
    }
  }
  else if (keeper && livePlay && heldBy == nullptr &&
           ballWorld.z >
               heightMetres * MatchTuning::Goalkeeper::SHOULDER_HEIGHT_SHARE &&
           PlayerRig::flatDistance(ballWorld, root) <
               heightMetres * MatchTuning::Goalkeeper::HAND_REACH_HEIGHT_SHARE)
  {
    handTarget = {ballWorld + torsoSide * (R::HAND_GRIP_HALF * scale),
                  ballWorld - torsoSide * (R::HAND_GRIP_HALF * scale)};
    handWeight = 1.0F;
  }
  else if (mood == GoalMood::DEJECTED && celebrating && slot.handsOnHead)
  {
    const Vec3 top = headCentre + torsoUp * (P::HEAD_RADIUS * 0.8f * scale);
    handTarget = {top + torsoSide * (0.09f * scale),
                  top - torsoSide * (0.09f * scale)};
    handWeight = celebrationWeight;
  }
  const float upperArm = P::UPPER_ARM_LENGTH * scale;
  const float forearm = P::FOREARM_LENGTH * scale;
  for (std::size_t index = 0; index < 2; ++index)
  {
    const float sideSign = index == 0 ? 1.0f : -1.0f;
    const Vec3 shoulder = chest - torsoUp * (P::SHOULDER_DROP * scale) +
                          torsoSide * (sideSign * P::SHOULDER_SPREAD * bulk);
    Vec3 elbow = shoulder + limbDirection(armFrame, pose.armSwing[index],
                                          pose.armSpread[index]) *
                                upperArm;
    Vec3 wrist = elbow + limbDirection(armFrame,
                                       pose.armSwing[index] + pose.elbow[index],
                                       pose.armSpread[index]) *
                             forearm;
    if (handWeight > 0.0f)
    {
      const Vec3 target =
          RenderMath::lerp(wrist, handTarget[index], handWeight);
      const PlayerRig::TwoBone arm = PlayerRig::solveTwoBone(
          shoulder, target, upperArm, forearm,
          torsoSide * (sideSign * 0.6f) - torsoForward * 0.4f - torsoUp * 0.3f);
      elbow = arm.middle;
      wrist = arm.end;
    }
    // Short sleeves (keepers wear long ones).
    const Vec3 cuff = RenderMath::lerp(shoulder, elbow, P::SLEEVE_SHARE);
    const Vec3 armHint = torsoForward;
    if (lod == PlayerLod::FAR)
    {
      addSegment(shoulder, elbow, P::UPPER_ARM_TOP * bulk,
                 P::ELBOW_HALF_WIDTH * bulk, kit.shirt, armHint, objectDepth,
                 lod);
      addSegment(elbow, wrist, P::ELBOW_HALF_WIDTH * bulk,
                 P::WRIST_HALF_WIDTH * bulk, keeper ? kit.shirt : skin, armHint,
                 objectDepth, lod);
      continue;
    }
    if (keeper)
    {
      addSegment(shoulder, elbow, P::UPPER_ARM_TOP * bulk,
                 P::ELBOW_HALF_WIDTH * bulk, kit.shirt, armHint, objectDepth,
                 lod);
      addSegment(elbow, wrist, P::ELBOW_HALF_WIDTH * bulk,
                 P::WRIST_HALF_WIDTH * bulk, kit.shirt, armHint, objectDepth,
                 lod);
    }
    else
    {
      addSegment(shoulder, cuff, P::UPPER_ARM_TOP * bulk * 1.08f,
                 P::SLEEVE_HEM * bulk, kit.shirt, armHint, objectDepth, lod);
      addSegment(cuff, elbow, P::UPPER_ARM_MIDDLE * bulk,
                 P::ELBOW_HALF_WIDTH * bulk, skin, armHint, objectDepth, lod);
      addSegment(elbow, wrist, P::ELBOW_HALF_WIDTH * bulk,
                 P::WRIST_HALF_WIDTH * bulk, skin, armHint, objectDepth, lod);
    }
    if (detailed)
    {
      const Vec3 handDirection = RenderMath::normalize(wrist - elbow);
      const float half =
          (keeper ? P::GLOVE_HALF_SIZE : P::HAND_HALF_SIZE) * scale;
      orientedBox(wrist + handDirection * half, torsoForward * half,
                  torsoSide * (half * 0.6f), handDirection * half, hands,
                  objectDepth);
    }
  }

  // --- neck and head ---------------------------------------------------------
  if (lod == PlayerLod::NEAR)
  {
    addSegment(chest - torsoUp * (0.02f * scale),
               headCentre - torsoUp * (P::HEAD_RADIUS * 0.6f * scale),
               P::NECK_HALF_WIDTH * scale, P::NECK_HALF_WIDTH * scale * 0.9f,
               shadeColor(skin, 0.9f), torsoForward, objectDepth, lod);
  }
  ScreenPoint head;
  if (projection.project(headCentre, head))
  {
    Primitive primitive;
    primitive.firstPoint = static_cast<std::uint32_t>(points.size());
    primitive.pointCount = 2;
    primitive.kind = PrimitiveKind::HEAD;
    primitive.color = skin;
    primitive.secondaryColor = look.hair;
    const float facing = RenderMath::dot(
        torsoForward, RenderMath::normalize(projection.eye - headCentre));
    primitive.reference =
        static_cast<std::uint32_t>(look.hairStyle) |
        (look.beard ? HEAD_BEARD_BIT : 0U) |
        (static_cast<std::uint32_t>(
             std::clamp(facing * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f)
         << HEAD_FACING_SHIFT);
    primitive.size =
        P::HEAD_RADIUS * scale * projection.focalPixels / head.depth;
    points.push_back(toImVec(head));
    // Second point: where the face looks on screen (shading and hair side).
    ScreenPoint facePoint;
    points.push_back(
        projection.project(headCentre + torsoForward * 0.1f, facePoint)
            ? toImVec(facePoint)
            : toImVec(head));
    keys.push_back({objectDepth, head.depth,
                    static_cast<std::uint32_t>(primitives.size())});
    primitives.push_back(primitive);

    ScreenPoint feet;
    projection.project(root, feet);
    playersOnScreen.push_back({&player,
                               {head.x, head.y - primitive.size},
                               toImVec(feet),
                               head.depth});
  }
  light = sceneLight;
}

void MatchRenderer3D::State::addBackPrint(const AnimationSlot& slot,
                                          std::size_t kitIndex, Vec3 waist,
                                          Vec3 torsoUp, Vec3 torsoForward,
                                          Vec3 bodySide, float scale,
                                          float objectDepth)
{
  using N = Tuning::Number;
  using P = Tuning::Player;
  if (slot.number <= 0) return;
  const float backDepth =
      (P::WAIST_HALF_DEPTH + P::CHEST_HALF_DEPTH) * 0.5f * scale + 0.004f;
  const Vec3 centre =
      waist + torsoUp * (N::CENTRE_UP * scale) - torsoForward * backDepth;
  const Vec3 toEye = RenderMath::normalize(projection.eye - centre);
  if (RenderMath::dot(torsoForward * -1.0f, toEye) < N::MIN_FACING) return;
  // Seen from behind, the player's right is the viewer's right.
  const Vec3 across = bodySide * -1.0f;
  const Vec3 down = torsoUp * -1.0f;
  const auto print =
      [&](Vec3 middle, float glyphHeight, float minimumPixels, const char* text)
  {
    // Digits and capitals fill ~72% of the font's line height.
    const float lineHeight = glyphHeight / 0.72f;
    ScreenPoint mid;
    ScreenPoint below;
    if (!projection.project(middle, mid) ||
        !projection.project(middle + down * (lineHeight * 0.5f), below))
      return;
    const float linePixels =
        2.0f * std::hypot(below.x - mid.x, below.y - mid.y);
    if (linePixels * 0.72f < minimumPixels) return;
    const float step = std::round(std::log(linePixels / N::MIN_FONT_SIZE) /
                                  std::log(N::FONT_STEP));
    const float fontSize =
        std::clamp(N::MIN_FONT_SIZE * std::pow(N::FONT_STEP, step),
                   N::MIN_FONT_SIZE, N::MAX_FONT_SIZE);
    const ImVec2 size =
        ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
    if (size.x <= 0.0f || size.y <= 0.0f) return;
    const float halfWidth = lineHeight * size.x / size.y * 0.5f;
    ScreenPoint right;
    if (!projection.project(middle + across * halfWidth, right)) return;
    Primitive primitive;
    primitive.firstPoint = static_cast<std::uint32_t>(points.size());
    primitive.pointCount = 3;
    primitive.kind = PrimitiveKind::TEXT;
    primitive.color = numberColors[kitIndex];
    primitive.size = fontSize;
    primitive.reference = static_cast<std::uint32_t>(texts.size());
    texts.push_back(text);
    points.push_back(toImVec(mid));
    points.push_back(toImVec(right));
    points.push_back(toImVec(below));
    // Just in front of the shirt's back face.
    keys.push_back({objectDepth, mid.depth - 0.05f,
                    static_cast<std::uint32_t>(primitives.size())});
    primitives.push_back(primitive);
  };
  const auto number = static_cast<std::size_t>(std::clamp(slot.number, 0, 99));
  print(centre, N::HEIGHT * scale, N::MIN_PIXELS, NUMBER_TEXT[number].data());
  if (!slot.backName.empty())
  {
    print(centre + torsoUp * ((N::HEIGHT * 0.5f + N::NAME_GAP +
                               N::NAME_HEIGHT * 0.5f) *
                              scale),
          N::NAME_HEIGHT * scale, N::NAME_MIN_PIXELS, slot.backName.c_str());
  }
}

void MatchRenderer3D::State::drawBackText(const Primitive& primitive)
{
  // Glyphs laid out once, then mapped onto the shirt with the affine frame
  // (centre, half-width and half-height points) projected for this player.
  const ImVec2* frame = points.data() + primitive.firstPoint;
  const char* text = texts[primitive.reference];
  ImFont* font = ImGui::GetFont();
  const ImVec2 size = font->CalcTextSizeA(primitive.size, FLT_MAX, 0.0f, text);
  if (size.x <= 0.0f || size.y <= 0.0f) return;
  const ImVec2 canonical{projection.rect.x, projection.rect.y};
  const ImVec4 unclipped{-1e6f, -1e6f, 1e6f, 1e6f};
  const int firstVertex = drawList->VtxBuffer.Size;
  // Two passes a hair apart give the heavy block digits of a real kit.
  const float weight = primitive.size * Tuning::Number::WEIGHT;
  drawList->AddText(font, primitive.size, canonical, primitive.color, text,
                    nullptr, 0.0f, &unclipped);
  drawList->AddText(font, primitive.size, {canonical.x + weight, canonical.y},
                    primitive.color, text, nullptr, 0.0f, &unclipped);
  const float width = size.x + weight;
  const ImVec2 axisX{frame[1].x - frame[0].x, frame[1].y - frame[0].y};
  const ImVec2 axisY{frame[2].x - frame[0].x, frame[2].y - frame[0].y};
  for (int index = firstVertex; index < drawList->VtxBuffer.Size; ++index)
  {
    ImDrawVert& vertex = drawList->VtxBuffer[index];
    const float u = (vertex.pos.x - canonical.x) / width * 2.0f - 1.0f;
    const float v = (vertex.pos.y - canonical.y) / size.y * 2.0f - 1.0f;
    vertex.pos = {frame[0].x + axisX.x * u + axisY.x * v,
                  frame[0].y + axisX.y * u + axisY.y * v};
  }
}

void MatchRenderer3D::State::detectTouches(const MatchRenderSnapshot& snapshot)
{
  using Kk = Tuning::Kick;
  using R = Tuning::Rig;
  const MatchRenderBall& ball = snapshot.ball;
  const float alpha = snapshot.interpolationAlpha;

  // Tackles: a player's challenge cooldown only jumps up when he goes in.
  for (std::size_t index = 0; index < snapshot.players.size(); ++index)
  {
    const MatchRenderPlayer& player = snapshot.players[index];
    AnimationSlot& slot = slots[index];
    const bool challenged = player.tackleCooldown >
                            slot.lastTackleCooldown + R::TACKLE_COOLDOWN_RISE;
    slot.lastTackleCooldown = player.tackleCooldown;
    if (!challenged || !livePlay || !player.onPitch) continue;
    if (slot.event != PlayerRig::Event::NONE &&
        slot.event != PlayerRig::Event::TACKLE)
      continue;
    const Vec3 root = worldOf(lerpRenderPosition(
        player.previousPosition, player.currentPosition, alpha));
    const Vec3 toBall = ballWorld - root;
    // Going to ground from further out or at pace; otherwise a block.
    const bool sliding =
        PlayerRig::flatDistance(ballWorld, root) > R::SLIDE_DISTANCE ||
        player.speedMetresPerSecond > R::SLIDE_SPEED;
    slot.event = sliding ? PlayerRig::Event::SLIDE : PlayerRig::Event::TACKLE;
    slot.eventSeconds = 0.0f;
    slot.contact = {ballWorld.x, ballWorld.y, 0.0f};
    const float reach = std::hypot(toBall.x, toBall.y);
    slot.eventDirection = reach > 0.05f
                              ? Vec3{toBall.x / reach, toBall.y / reach, 0.0f}
                              : slot.moveDirection;
    const Vec3 side{-slot.moveDirection.y, slot.moveDirection.x, 0.0f};
    slot.eventLeg = RenderMath::dot(toBall, side) >= 0.0f ? 0U : 1U;
  }

  // Kicks: the kicker's lockout is reset on every strike and only counts
  // down otherwise, so a rise marks the step the ball left his foot.
  const bool struck = ball.kickerLockout > lastKickerLockout + 1e-4f;
  lastKickerLockout = ball.kickerLockout;
  if (!struck || !ball.kicker) return;
  // A miscontrol also locks the player out, for less time: no kick pose.
  if (ball.kickerLockout < MatchTuning::Passing::KICKER_LOCKOUT_SECONDS - 0.01f)
    return;
  for (std::size_t index = 0; index < snapshot.players.size(); ++index)
  {
    const MatchRenderPlayer& player = snapshot.players[index];
    if (player.player != ball.kicker) continue;
    AnimationSlot& slot = slots[index];
    const Vec3 from = worldOf(ball.previousPosition,
                                                 ball.previousHeightMetres);
    const Vec3 to = worldOf(ball.currentPosition,
                                               ball.currentHeightMetres);
    const Vec3 flight{to.x - from.x, to.y - from.y, 0.0f};
    const float flightLength = RenderMath::length(flight);
    if (flightLength > Kk::TELEPORT_METRES) return;
    PlayerRig::Event event = PlayerRig::Event::PASS;
    if (ball.fromThrowIn)
      event = PlayerRig::Event::THROW;
    else if (ball.previousHeightMetres >= Kk::HEADER_HEIGHT &&
             !player.isGoalkeeper)
      event = PlayerRig::Event::HEADER;
    else if (ball.isShot)
      event = PlayerRig::Event::SHOT;
    else if (ball.isAerialDelivery ||
             ball.currentHeightMetres > Kk::LOFT_HEIGHT)
      event = PlayerRig::Event::CROSS;
    slot.event = event;
    slot.eventSeconds = 0.0f;
    slot.contact = from;
    slot.eventDirection = flightLength > 1e-3f ? flight * (1.0f / flightLength)
                                               : slot.moveDirection;
    // The preferred foot, unless the ball sits well over on the other side.
    const Vec3 root = worldOf(lerpRenderPosition(
        player.previousPosition, player.currentPosition, alpha));
    const float yaw = RenderMath::worldYawFromFacing(RenderMath::lerpAngle(
        player.previousFacingAngle, player.currentFacingAngle, alpha));
    const Vec3 left{-std::sin(yaw), std::cos(yaw), 0.0f};
    const float offset = RenderMath::dot(from - root, left);
    slot.eventLeg = offset > Kk::WRONG_FOOT_METRES    ? 0U
                    : offset < -Kk::WRONG_FOOT_METRES ? 1U
                    : slot.leftFooted                 ? 0U
                                                      : 1U;
    return;
  }
}

void MatchRenderer3D::State::updateGoalMoment(
    const MatchRenderSnapshot& snapshot)
{
  using C = Tuning::Celebration;
  const bool scoring = snapshot.state == MatchState::GOAL;
  if (!scoring)
  {
    goal.active = false;
    celebrationWeight = 0.0f;
    crowdCelebration = 0.0f;
    return;
  }
  if (!goal.active)
  {
    goal = GoalMomentState{};
    goal.active = true;
    goal.byHome = snapshot.goalScoredByHome;
    // The goal the ball is nearest to took it, whichever way teams play.
    goal.goalIndex = ballWorld.x < LENGTH * 0.5f ? 0U : 1U;
    goal.impact = {
        goal.goalIndex == 0 ? -Tuning::Goal::BASE_DEPTH * 0.7f
                            : LENGTH + Tuning::Goal::BASE_DEPTH * 0.7f,
        std::clamp(ballWorld.y, WIDTH * 0.5f - Tuning::Goal::WIDTH * 0.5f,
                   WIDTH * 0.5f + Tuning::Goal::WIDTH * 0.5f),
        std::clamp(ballWorld.z, 0.3f, Tuning::Goal::HEIGHT - 0.3f)};
    if (snapshot.events)
    {
      const std::vector<MatchEvent>& events = *snapshot.events;
      const std::size_t oldest = events.size() > 64 ? events.size() - 64 : 0;
      for (std::size_t index = events.size(); index-- > oldest;)
      {
        const MatchEvent& event = events[index];
        if (event.type != MatchEventType::GOAL &&
            event.type != MatchEventType::OWN_GOAL)
          continue;
        goal.ownGoal = event.type == MatchEventType::OWN_GOAL;
        goal.scorer = event.primaryPlayerId;
        goal.minute = MatchClock::minuteLabel(event.timeMinute, event.period,
                                              event.addedMinute > 0.0f);
        break;
      }
    }
    for (const MatchRenderPlayer& player : snapshot.players)
    {
      if (!goal.ownGoal && player.player &&
          player.player->getId() == goal.scorer)
        goal.scorerName = player.player->getName();
    }
  }
  goal.shownSeconds += frameSeconds;
  goal.celebrationSeconds =
      std::max(0.0f, snapshot.goalCelebrationDuration -
                         snapshot.goalCelebrationRemaining);
  const auto envelope = [](float seconds, float hold)
  {
    const float in = std::clamp(seconds / 0.6f, 0.0f, 1.0f);
    const float out =
        std::clamp(1.0f - (seconds - hold) / C::FADE_SECONDS, 0.0f, 1.0f);
    return std::min(in, out);
  };
  celebrationWeight = envelope(goal.celebrationSeconds, C::PLAYER_SECONDS);
  crowdCelebration = envelope(goal.celebrationSeconds, C::CROWD_SECONDS);
}

void MatchRenderer3D::State::drawGoalSting(const MatchRenderSnapshot& snapshot)
{
  // A broadcast lower third in the style of the focus HUD chips: a club
  // colour bar, the goal call and score, then the scorer and minute.
  using S = Tuning::Sting;
  if (!goal.active || goal.shownSeconds > S::SECONDS) return;
  const float t = goal.shownSeconds;
  const float fadeIn = std::clamp(t / S::FADE_IN_SECONDS, 0.0f, 1.0f);
  const float fadeOut =
      std::clamp((S::SECONDS - t) / S::FADE_OUT_SECONDS, 0.0f, 1.0f);
  const float opacity = std::min(fadeIn, fadeOut);
  if (opacity <= 0.0f) return;
  const float ease = 1.0f - (1.0f - fadeIn) * (1.0f - fadeIn);
  const float slide = reducedMotion ? 0.0f : (1.0f - ease) * S::SLIDE_EM;

  ImFont* font = ImGui::GetFont();
  const float em = ImGui::GetFontSize();
  const float titleSize = em * S::TITLE_SCALE;
  const float padding = em * S::PADDING_EM;
  const char* call = LOC("MATCH_GOAL_BANNER");
  char score[16];
  std::snprintf(score, sizeof(score), "%d - %d", snapshot.homeScore,
                snapshot.awayScore);
  // Scorer and minute in a fixed buffer: no allocation per frame.
  std::array<char, 96> detail{};
  std::snprintf(detail.data(), detail.size(), "%s%s%s", goal.scorerName.c_str(),
                goal.scorerName.empty() || goal.minute.empty() ? "" : "  ",
                goal.minute.c_str());
  const bool hasDetail = detail[0] != '\0';
  const ImVec2 callSize = font->CalcTextSizeA(titleSize, FLT_MAX, 0.0f, call);
  const ImVec2 scoreSize = font->CalcTextSizeA(titleSize, FLT_MAX, 0.0f, score);
  const ImVec2 detailSize =
      font->CalcTextSizeA(em, FLT_MAX, 0.0f, detail.data());
  const float bar = em * S::BAR_EM;
  const float titleWidth = callSize.x + padding * 2.0f + scoreSize.x;
  const float width = bar + padding * 2.0f + std::max(titleWidth, detailSize.x);
  const float height =
      callSize.y + (hasDetail ? detailSize.y : 0.0f) + padding * 1.5f;
  const float x = projection.rect.x +
                  std::max(padding, (projection.rect.width - width) * 0.5f) -
                  slide * em;
  const float y = projection.rect.y + projection.rect.height - height -
                  projection.rect.height * S::BOTTOM_SHARE;
  const auto fade = [opacity](ImU32 color)
  {
    const auto alpha = static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFFU);
    return withAlpha(color, static_cast<std::uint8_t>(alpha * opacity));
  };
  const float rounding = em * S::ROUNDING_EM;
  drawList->AddRectFilled({x, y}, {x + width, y + height}, fade(S::PANEL_COLOR),
                          rounding);
  const KitColors& kit = goal.byHome ? kits.home : kits.away;
  drawList->AddRectFilled({x, y}, {x + bar, y + height}, fade(kit.shirt),
                          rounding, ImDrawFlags_RoundCornersLeft);
  drawList->AddRectFilled({x + bar * 0.6f, y}, {x + bar, y + height},
                          fade(kit.trim));
  const float textX = x + bar + padding;
  const float textY = y + padding * 0.75f;
  drawList->AddText(font, titleSize, {textX, textY}, fade(S::CALL_COLOR), call);
  drawList->AddText(font, titleSize,
                    {textX + callSize.x + padding * 2.0f, textY},
                    fade(S::TEXT_COLOR), score);
  if (hasDetail)
  {
    drawList->AddText(font, em, {textX, textY + callSize.y},
                      fade(S::DETAIL_COLOR), detail.data());
  }
}

void MatchRenderer3D::State::addGoals()
{
  using R = Tuning::Ripple;
  for (std::size_t goalIndex = 0; goalIndex < geometry.goals.size();
       ++goalIndex)
  {
    const Stadium3D::Goal& frame = geometry.goals[goalIndex];
    // The net that took the goal billows out from the ball and settles.
    const bool rippling = goal.active && goal.goalIndex == goalIndex &&
                          goal.shownSeconds < R::SECONDS;
    const float age = goal.shownSeconds;
    const Vec3 mouth{goalIndex == 0 ? 0.0f : LENGTH, WIDTH * 0.5f,
                     Tuning::Goal::HEIGHT * 0.5f};
    for (const Stadium3D::NetPanel& net : frame.nets)
    {
      std::array<ScreenPoint, 4> corners{};
      bool visible = true;
      for (std::size_t index = 0; index < 4 && visible; ++index)
        visible = projection.project(net.corners[index], corners[index]);
      if (!visible) continue;
      Primitive primitive;
      primitive.firstPoint = static_cast<std::uint32_t>(points.size());
      primitive.kind = PrimitiveKind::NET;
      primitive.color = Tuning::Goal::NET_FILL_COLOR;
      primitive.secondaryColor = Tuning::Goal::NET_LINE_COLOR;
      for (const ScreenPoint& corner : corners)
        points.push_back(toImVec(corner));
      const std::span<ImVec2> quad(points.data() + primitive.firstPoint, 4);
      if (signedArea(quad) < 0.0f) std::reverse(quad.begin(), quad.end());
      // Mesh strands: bilinear lines across the panel in both directions.
      const auto bilinear = [&net](float u, float v)
      {
        const Vec3 bottom = RenderMath::lerp(net.corners[0], net.corners[1], u);
        const Vec3 top = RenderMath::lerp(net.corners[3], net.corners[2], u);
        return RenderMath::lerp(bottom, top, v);
      };
      Vec3 outward = RenderMath::normalize(RenderMath::cross(
          net.corners[1] - net.corners[0], net.corners[3] - net.corners[0]));
      const Vec3 panelCentre = (net.corners[0] + net.corners[2]) * 0.5f;
      if (RenderMath::dot(outward, panelCentre - mouth) < 0.0f)
        outward = outward * -1.0f;
      const auto displaced = [&](Vec3 point)
      {
        const float distance = RenderMath::length(point - goal.impact);
        const float push =
            R::AMPLITUDE * std::exp(-R::DECAY * age) *
            std::exp(-distance / R::REACH) *
            std::cos(R::WAVE_NUMBER * distance - R::WAVE_SPEED * age);
        return point + outward * push;
      };
      const auto strand = [&](Vec3 from, Vec3 to)
      {
        ScreenPoint a;
        ScreenPoint b;
        if (!rippling)
        {
          if (!projection.project(from, a) || !projection.project(to, b))
            return;
          points.push_back(toImVec(a));
          points.push_back(toImVec(b));
          return;
        }
        if (!projection.project(displaced(from), a)) return;
        for (int segment = 1; segment <= R::SEGMENTS; ++segment)
        {
          const Vec3 point = displaced(RenderMath::lerp(
              from, to,
              static_cast<float>(segment) / static_cast<float>(R::SEGMENTS)));
          if (!projection.project(point, b)) return;
          points.push_back(toImVec(a));
          points.push_back(toImVec(b));
          a = b;
        }
      };
      for (int column = 1; column < net.columns; ++column)
      {
        const float u =
            static_cast<float>(column) / static_cast<float>(net.columns);
        strand(bilinear(u, 0.0f), bilinear(u, 1.0f));
      }
      for (int row = 1; row < net.rows; ++row)
      {
        const float v = static_cast<float>(row) / static_cast<float>(net.rows);
        strand(bilinear(0.0f, v), bilinear(1.0f, v));
      }
      primitive.pointCount =
          static_cast<std::uint16_t>(points.size() - primitive.firstPoint);
      const float depth = (corners[0].depth + corners[1].depth +
                           corners[2].depth + corners[3].depth) *
                          0.25f;
      keys.push_back(
          {depth, depth, static_cast<std::uint32_t>(primitives.size())});
      primitives.push_back(primitive);
    }
    for (const auto& support : frame.supports)
    {
      const float depth = projection.depth((support[0] + support[1]) * 0.5f);
      addLine(support[0], support[1], Tuning::Goal::SUPPORT_COLOR,
              Tuning::Goal::POST_SIZE * 0.5f, depth);
    }
    for (const Stadium3D::Box& part : frame.frame)
    {
      const float depth =
          projection.depth((part.corners[0] + part.corners[7]) * 0.5f);
      addBox(part.corners, part.color, depth);
    }
  }
}

void MatchRenderer3D::State::addTrainingGoals(const MatchRenderSnapshot& snapshot)
{
  // A training pitch's goals at its own size: posts and bar, and a net
  // (sides, roof and back) behind the line, depth-sorted with the players.
  const PitchGoal& g = snapshot.pitchGoal;
  if (!g.present()) return;
  using G = Tuning::Goal;
  const float centreY = 0.5f * pitchWidth;
  const float half = g.halfWidth + 0.5f * g.post;
  const float barZ = g.crossbar + 0.5f * g.post;
  const float postHalf = 0.5f * g.post;
  for (const bool right : {false, true})
  {
    const float line = right ? pitchLength : 0.0f;
    const float behind = right ? 1.0f : -1.0f;
    const auto at = [&](float u, float v, float z)
    { return worldOf(PitchPoint{line + behind * u, centreY + v}, z); };
    // The net: panels from the frame back to the net's depth, its roof
    // dropping a little towards the back.
    const float back = g.netDepth;
    const float backTop = std::max(0.3f * g.crossbar, g.crossbar - 0.25f * back);
    const std::array<std::array<Vec3, 4>, 4> panels{{
        {at(0, -half, 0), at(back, -half, 0), at(back, -half, backTop), at(0, -half, barZ)},
        {at(0, half, 0), at(back, half, 0), at(back, half, backTop), at(0, half, barZ)},
        {at(0, -half, barZ), at(back, -half, backTop), at(back, half, backTop), at(0, half, barZ)},
        {at(back, -half, 0), at(back, half, 0), at(back, half, backTop), at(back, -half, backTop)}}};
    for (const auto& panel : panels)
    {
      std::array<ScreenPoint, 4> screen{};
      bool visible = true;
      for (std::size_t corner = 0; corner < 4 && visible; ++corner)
        visible = projection.project(panel[corner], screen[corner]);
      if (!visible) continue;
      const float depth = (screen[0].depth + screen[1].depth + screen[2].depth +
                           screen[3].depth) * 0.25f;
      addPolygon(screen, G::NET_FILL_COLOR, depth, depth);
      // A few strands along the panel, so it reads as netting.
      for (int strand = 1; strand < 4; ++strand)
      {
        const float t = static_cast<float>(strand) / 4.0f;
        addLine(RenderMath::lerp(panel[0], panel[1], t),
                RenderMath::lerp(panel[3], panel[2], t), G::NET_LINE_COLOR,
                0.01f, depth);
      }
    }
    // The frame: two posts and the bar, white boxes.
    for (const float side : {-half, half})
    {
      const Vec3 centre = at(0, side, 0.5f * barZ);
      orientedBox(centre, Vec3{postHalf, 0, 0}, Vec3{0, postHalf, 0},
                  Vec3{0, 0, 0.5f * barZ + postHalf}, G::POST_COLOR,
                  projection.depth(centre));
    }
    const Vec3 bar = at(0, 0, barZ);
    orientedBox(bar, Vec3{postHalf, 0, 0}, Vec3{0, half + postHalf, 0},
                Vec3{0, 0, postHalf}, G::POST_COLOR, projection.depth(bar));
  }
}

void MatchRenderer3D::State::drawTrainingGround()
{
  using G = Tuning::Grass;
  constexpr float MARGIN = 5.0f;
  constexpr float FENCE_HEIGHT = 1.0f;
  // Plain grass, a shade darker outside the walls, then the pitch.
  const auto quad = [this](PitchPoint a, PitchPoint b, float height, ImU32 color)
  {
    const std::array<Vec3, 4> corners{worldOf(a, height),
                                      worldOf(PitchPoint{b.x, a.y}, height),
                                      worldOf(b, height),
                                      worldOf(PitchPoint{a.x, b.y}, height)};
    const std::array<ImU32, 4> colors{color, color, color, color};
    drawWorldPolygon(corners, colors, false);
  };
  // Lit as the match pitch is (day or floodlit night).
  const float sunlit = day ? Tuning::Day::GRASS_LIGHT : 1.0f;
  const ImU32 outside = shadeColor(G::PITCH_COLOR, sunlit * 0.86f);
  const ImU32 inside = shadeColor(G::PITCH_COLOR, sunlit);
  quad(PitchPoint{-MARGIN, -MARGIN},
       PitchPoint{pitchLength + MARGIN, pitchWidth + MARGIN}, 0.0f, outside);
  quad(PitchPoint{0.0f, 0.0f}, PitchPoint{pitchLength, pitchWidth}, 0.0f, inside);
  // The walls: see-through netting (unlimited height in the engine; drawn
  // as a fence).
  constexpr ImU32 NET = IM_COL32(24, 28, 32, 90);
  constexpr ImU32 RAIL = IM_COL32(24, 28, 32, 220);
  // Along both sides, and along each end either side of its goal mouth.
  const float middle = 0.5f * pitchWidth;
  const std::array<std::array<PitchPoint, 2>, 6> stretches{{
      {PitchPoint{0.0f, 0.0f}, PitchPoint{pitchLength, 0.0f}},
      {PitchPoint{0.0f, pitchWidth}, PitchPoint{pitchLength, pitchWidth}},
      {PitchPoint{0.0f, 0.0f}, PitchPoint{0.0f, middle - mouthHalf}},
      {PitchPoint{0.0f, middle + mouthHalf}, PitchPoint{0.0f, pitchWidth}},
      {PitchPoint{pitchLength, 0.0f}, PitchPoint{pitchLength, middle - mouthHalf}},
      {PitchPoint{pitchLength, middle + mouthHalf}, PitchPoint{pitchLength, pitchWidth}}}};
  for (const auto& [a, b] : stretches)
  {
    const std::array<Vec3, 4> panel{worldOf(a), worldOf(b),
                                    worldOf(b, FENCE_HEIGHT),
                                    worldOf(a, FENCE_HEIGHT)};
    const std::array<ImU32, 4> colors{NET, NET, NET, NET};
    drawWorldPolygon(panel, colors, true);
    const std::array<Vec3, 4> rail{worldOf(a, FENCE_HEIGHT - 0.04f),
                                   worldOf(b, FENCE_HEIGHT - 0.04f),
                                   worldOf(b, FENCE_HEIGHT),
                                   worldOf(a, FENCE_HEIGHT)};
    const std::array<ImU32, 4> railColors{RAIL, RAIL, RAIL, RAIL};
    drawWorldPolygon(rail, railColors, true);
  }
}

void MatchRenderer3D::State::addKit(const MatchRenderSnapshot& snapshot)
{
  const PitchKit::Layout& kit = snapshot.kit;
  // Lines and areas on the grass: thin painted strips.
  constexpr float HALF_LINE = 0.05f;
  const auto strip = [this](PitchPoint from, PitchPoint to, ImU32 color)
  {
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float run = std::max(std::hypot(dx, dy), 1e-4f);
    const float nx = -dy / run * HALF_LINE;
    const float ny = dx / run * HALF_LINE;
    constexpr float LIFT = 0.005f;
    const std::array<Vec3, 4> corners{
        worldOf(PitchPoint{from.x + nx, from.y + ny}, LIFT),
        worldOf(PitchPoint{to.x + nx, to.y + ny}, LIFT),
        worldOf(PitchPoint{to.x - nx, to.y - ny}, LIFT),
        worldOf(PitchPoint{from.x - nx, from.y - ny}, LIFT)};
    const std::array<ImU32, 4> colors{color, color, color, color};
    drawWorldPolygon(corners, colors, true);
  };
  for (const PitchKit::Line& line : kit.lines) strip(line.from, line.to, line.colour);
  for (const PitchKit::Area& area : kit.areas)
  {
    const PitchPoint a = area.min;
    const PitchPoint b{area.max.x, area.min.y};
    const PitchPoint c = area.max;
    const PitchPoint d{area.min.x, area.max.y};
    strip(a, b, area.colour);
    strip(b, c, area.colour);
    strip(c, d, area.colour);
    strip(d, a, area.colour);
  }
  // Cones as shaded eight-sided solids: a tall cone narrowing to a small
  // tip, or a flat marker disc a little narrower on top.
  constexpr float R = PitchKit::CONE_RADIUS_METRES;
  constexpr std::size_t SIDES = 8;
  for (const PitchKit::Cone& cone : kit.cones)
  {
    const Vec3 base = worldOf(cone.at);
    const bool tall = cone.kind == PitchKit::ConeKind::TALL;
    const float height = tall ? PitchKit::TALL_CONE_HEIGHT_METRES
                              : PitchKit::FLAT_CONE_HEIGHT_METRES;
    const float topRadius = tall ? 0.15f * R : 0.65f * R;
    std::array<Vec3, SIDES> bottom{};
    std::array<Vec3, SIDES> top{};
    for (std::size_t side = 0; side < SIDES; ++side)
    {
      const float angle = TWO_PI * static_cast<float>(side) / SIDES;
      const Vec3 out{std::cos(angle), std::sin(angle), 0.0f};
      bottom[side] = base + out * R;
      top[side] = base + out * topRadius + UP * height;
    }
    addPrism(bottom, top, cone.colour, projection.depth(base + UP * height),
             false, true);
  }
}

void MatchRenderer3D::State::addCornerFlags()
{
  using C = Tuning::CornerFlag;
  for (const Vec3& base : geometry.cornerFlags)
  {
    const Vec3 top = base + UP * C::POLE_HEIGHT;
    const float depth = projection.depth(top);
    addLine(base, top, C::POLE_COLOR, C::POLE_WIDTH, depth);
    // The flag streams away from the pitch and ripples gently.
    const Vec3 outward =
        RenderMath::normalize(Vec3{base.x < LENGTH * 0.5f ? -1.0f : 1.0f,
                                   base.y < WIDTH * 0.5f ? -1.0f : 1.0f, 0.0f});
    const float wave =
        std::sin(elapsedSeconds * C::WAVE_SPEED + base.x + base.y) *
        C::WAVE_AMPLITUDE;
    const std::array<Vec3, 3> flag{
        top, top - UP * C::FLAG_HEIGHT,
        top + outward * C::FLAG_LENGTH - UP * (C::FLAG_HEIGHT * 0.5f + wave)};
    std::array<ScreenPoint, 3> screen{};
    bool visible = true;
    for (std::size_t index = 0; index < flag.size() && visible; ++index)
      visible = projection.project(flag[index], screen[index]);
    if (visible) addPolygon(screen, C::FLAG_COLOR, depth, depth - 0.01f);
  }
}

void MatchRenderer3D::State::addFixtures()
{
  for (const Stadium3D::Box& part : geometry.fixtures)
  {
    const float depth =
        projection.depth((part.corners[0] + part.corners[7]) * 0.5f);
    addBox(part.corners, part.color, depth);
  }
}

void MatchRenderer3D::State::addTouchlinePerson(const MatchRenderTouchlinePerson& person,
                                                std::size_t index, float alpha)
{
  using P = Tuning::Player;
  using T = Tuning::TouchlineFigure;
  if (person.hidden) return;
  const Vec3 root = worldOf(
      lerpRenderPosition(person.previousPosition, person.currentPosition, alpha));
  ScreenPoint anchor;
  if (!projection.project(root + UP * P::HIP_HEIGHT, anchor)) return;
  const RenderMath::ScreenRect& rect = projection.rect;
  if (anchor.x < rect.x - P::CULL_MARGIN_PIXELS ||
      anchor.x > rect.x + rect.width + P::CULL_MARGIN_PIXELS ||
      anchor.y < rect.y - P::CULL_MARGIN_PIXELS ||
      anchor.y > rect.y + rect.height + P::CULL_MARGIN_PIXELS)
    return;
  const float scale = person.heightMetres / P::REFERENCE_HEIGHT_METRES;
  const float pixelHeight = person.heightMetres * projection.focalPixels / anchor.depth;
  const PlayerLod lod = pixelHeight < Tuning::Rig::FAR_PIXELS ? PlayerLod::FAR
                        : pixelHeight < P::ROUND_MIN_PIXELS   ? PlayerLod::MID
                                                              : PlayerLod::NEAR;
  const float objectDepth = anchor.depth;
  // Staff and managers have no player: a look of their own, the same every
  // frame (by side, role and place in the list).
  const PlayerLook look =
      lookFor(person.player ? person.player->getId()
                            : T::STAFF_LOOK_SEED + static_cast<std::uint32_t>(index) * 7U +
                                  (person.isHomeTeam ? 0U : 1U));
  const KitColors& kit = kitList[person.isHomeTeam ? 0U : 2U];
  ImU32 top = T::TRACKSUIT_COLOR;
  ImU32 legs = T::TRACKSUIT_COLOR;
  if (person.role == TouchlineRole::SUBSTITUTE)
    top = shadeColor(kit.shirt, T::SUB_TOP_SHADE);
  else if (person.role == TouchlineRole::SENT_OFF)
  {
    top = kit.shirt;
    legs = kit.shorts;
  }
  else if (person.role == TouchlineRole::MANAGER)
    top = legs = T::SUIT_COLOR;
  else if (person.role == TouchlineRole::SERVER)
    top = T::COACH_TOP_COLOR;
  else if (isOfficial(person.role))
  {
    top = officialsShirtColor(kits);
    legs = T::OFFICIAL_SHORTS_COLOR;
  }

  const float yaw =
      person.fallen
          ? RenderMath::worldYawFromFacing(person.lyingAngle)
          : RenderMath::worldYawFromFacing(RenderMath::lerpAngle(
                person.previousFacingAngle, person.currentFacingAngle, alpha));
  const Vec3 forward{std::cos(yaw), std::sin(yaw), 0.0f};
  const Vec3 side{-forward.y, forward.x, 0.0f};

  // Down after a knock: a body lying along the ground, head at one end.
  if (person.fallen && person.getUpShare < 0.5f)
  {
    const float length = person.heightMetres - 2.0f * P::HEAD_RADIUS * scale;
    const Vec3 feet = root - forward * (0.5f * length) + UP * T::LYING_HEIGHT;
    const Vec3 neck = root + forward * (0.5f * length) + UP * T::LYING_HEIGHT;
    addSegment(feet, root + UP * T::LYING_HEIGHT, T::LIMB_HALF * scale, T::TORSO_HALF * scale,
               legs, UP, objectDepth, lod);
    addSegment(root + UP * T::LYING_HEIGHT, neck, T::TORSO_HALF * scale, T::TORSO_HALF * scale,
               top, UP, objectDepth, lod);
    const Vec3 head = neck + forward * (P::HEAD_RADIUS * scale);
    ScreenPoint headPoint;
    if (projection.project(head, headPoint))
    {
      Primitive primitive;
      primitive.firstPoint = static_cast<std::uint32_t>(points.size());
      primitive.pointCount = 2;
      primitive.kind = PrimitiveKind::HEAD;
      primitive.color = look.skin;
      primitive.secondaryColor = look.hair;
      primitive.reference = static_cast<std::uint32_t>(look.hairStyle) |
                            (look.beard ? HEAD_BEARD_BIT : 0U) | (128U << HEAD_FACING_SHIFT);
      primitive.size = P::HEAD_RADIUS * scale * projection.focalPixels / headPoint.depth;
      points.push_back(toImVec(headPoint));
      points.push_back(toImVec(headPoint));
      keys.push_back({objectDepth, headPoint.depth,
                      static_cast<std::uint32_t>(primitives.size())});
      primitives.push_back(primitive);
    }
    return;
  }

  // Seated on the bench, or on his feet (a simple stride while he walks).
  const bool seated = person.seated;
  const float hipHeight = (seated ? T::SEAT_HEIGHT : P::HIP_HEIGHT) * scale;
  const Vec3 pelvis = root + UP * hipHeight;
  // Faster legs and a longer stride as he runs.
  const float speed = person.speedMetresPerSecond;
  const float stride =
      seated ? 0.0f
             : std::sin(elapsedSeconds * (T::STRIDE_RATE + T::STRIDE_RATE_PER_SPEED * speed)) *
                   (std::min(speed / T::FULL_STRIDE_SPEED, 1.0f) * T::STRIDE_REACH +
                    std::min(std::max(speed - T::FULL_STRIDE_SPEED, 0.0f) * T::RUN_REACH_PER_SPEED,
                             T::RUN_REACH_MOST)) *
                   scale;
  for (const float which : {-1.0f, 1.0f})
  {
    const Vec3 hip = pelvis + side * (which * P::HIP_SPREAD * scale);
    const Vec3 ankle =
        seated ? hip + forward * (T::KNEE_FORWARD * scale) -
                     UP * (hipHeight - T::ANKLE_HEIGHT * scale)
               : root + side * (which * P::HIP_SPREAD * scale) +
                     forward * (which * stride) + UP * (T::ANKLE_HEIGHT * scale);
    if (seated)
    {
      const Vec3 knee = hip + forward * (T::KNEE_FORWARD * scale);
      addSegment(hip, knee, T::LIMB_HALF * scale, T::LIMB_HALF * scale * 0.85f, legs, UP,
                 objectDepth, lod);
      addSegment(knee, ankle, T::LIMB_HALF * scale * 0.8f, T::LIMB_HALF * scale * 0.6f, legs,
                 forward, objectDepth, lod);
    }
    else
    {
      addSegment(hip, ankle, T::LIMB_HALF * scale, T::LIMB_HALF * scale * 0.6f, legs, forward,
                 objectDepth, lod);
    }
    addSegment(ankle, ankle + forward * (T::FOOT_LENGTH * scale), T::FOOT_HALF * scale,
               T::FOOT_HALF * scale, T::SHOE_COLOR, UP, objectDepth, lod);
  }
  const Vec3 shoulders = pelvis + UP * (T::TORSO_LENGTH * scale);
  orientedBox(pelvis + UP * (0.5f * T::TORSO_LENGTH * scale), forward * (T::TORSO_DEPTH * scale),
              side * (T::TORSO_HALF * scale), UP * (0.5f * T::TORSO_LENGTH * scale), top,
              objectDepth);
  for (const float which : {-1.0f, 1.0f})
  {
    const Vec3 shoulder = shoulders + side * (which * T::SHOULDER_SPREAD * scale);
    const Vec3 hand =
        seated ? pelvis + side * (which * T::SHOULDER_SPREAD * scale) +
                     forward * (T::KNEE_FORWARD * 0.6f * scale) + UP * (0.05f * scale)
               : shoulder - UP * (T::ARM_LENGTH * scale) - forward * (which * stride * 0.6f);
    addSegment(shoulder, hand, T::ARM_HALF * scale, T::ARM_HALF * scale * 0.75f, top, forward,
               objectDepth, lod);
  }
  const Vec3 headCentre = shoulders + UP * (T::NECK_LENGTH * scale + P::HEAD_RADIUS * scale);
  ScreenPoint head;
  if (!projection.project(headCentre, head)) return;
  Primitive primitive;
  primitive.firstPoint = static_cast<std::uint32_t>(points.size());
  primitive.pointCount = 2;
  primitive.kind = PrimitiveKind::HEAD;
  primitive.color = look.skin;
  primitive.secondaryColor = look.hair;
  const float facing =
      RenderMath::dot(forward, RenderMath::normalize(projection.eye - headCentre));
  primitive.reference =
      static_cast<std::uint32_t>(look.hairStyle) | (look.beard ? HEAD_BEARD_BIT : 0U) |
      (static_cast<std::uint32_t>(std::clamp(facing * 0.5f + 0.5f, 0.0f, 1.0f) * 255.0f)
       << HEAD_FACING_SHIFT);
  primitive.size = P::HEAD_RADIUS * scale * projection.focalPixels / head.depth;
  points.push_back(toImVec(head));
  ScreenPoint facePoint;
  points.push_back(projection.project(headCentre + forward * 0.1f, facePoint)
                       ? toImVec(facePoint)
                       : toImVec(head));
  keys.push_back({objectDepth, head.depth, static_cast<std::uint32_t>(primitives.size())});
  primitives.push_back(primitive);
}

void MatchRenderer3D::State::addBoards()
{
  using B = Tuning::Boards;
  for (std::uint32_t index = 0;
       index < static_cast<std::uint32_t>(geometry.boards.size()); ++index)
  {
    const Stadium3D::AdBoard& board = geometry.boards[index];
    const std::array<Vec3, 4> corners{
        board.origin, board.origin + board.right * board.length,
        board.origin + board.right * board.length + UP * B::HEIGHT,
        board.origin + UP * B::HEIGHT};
    std::array<ScreenPoint, 4> screen{};
    bool visible = true;
    for (std::size_t corner = 0; corner < 4 && visible; ++corner)
      visible = projection.project(corners[corner], screen[corner]);
    if (!visible) continue;
    const float depth = (screen[0].depth + screen[1].depth + screen[2].depth +
                         screen[3].depth) *
                        0.25f;
    const bool front =
        RenderMath::dot(board.normal, projection.eye - board.origin) > 0.0f;
    if (!front)
    {
      addPolygon(screen, B::BACK_COLOR, depth, depth);
      continue;
    }
    Primitive primitive;
    primitive.firstPoint = static_cast<std::uint32_t>(points.size());
    primitive.pointCount = 4;
    primitive.kind = PrimitiveKind::BOARD;
    primitive.reference = index;
    for (const ScreenPoint& point : screen) points.push_back(toImVec(point));
    keys.push_back(
        {depth, depth, static_cast<std::uint32_t>(primitives.size())});
    primitives.push_back(primitive);
  }
}

void MatchRenderer3D::State::addBall(const MatchRenderSnapshot& snapshot,
                                     float alpha)
{
  using B = Tuning::Ball;
  const PitchPoint ball = lerpRenderPosition(
      snapshot.ball.previousPosition, snapshot.ball.currentPosition, alpha);
  const float height = ballHeightMetres(snapshot.ball, alpha);
  // Held in the hands (keeper, throw-in) it is drawn where the hands are.
  const Vec3 world = heldBallPlaced
                         ? heldBallWorld
                         : worldOf(ball, height + B::RADIUS);
  ScreenPoint screen;
  if (!projection.project(world, screen)) return;
  // True size up close; a gentle boost with distance keeps it readable on
  // wide shots without ever looking oversized.
  const float boost =
      1.0f + (B::MAX_BOOST - 1.0f) *
                 std::clamp((screen.depth - B::BOOST_START_DEPTH) /
                                (B::BOOST_FULL_DEPTH - B::BOOST_START_DEPTH),
                            0.0f, 1.0f);
  Primitive primitive;
  primitive.firstPoint = static_cast<std::uint32_t>(points.size());
  primitive.pointCount = 1;
  primitive.kind = PrimitiveKind::BALL;
  primitive.size = std::max(
      B::MIN_PIXELS, B::RADIUS * boost * projection.focalPixels / screen.depth);
  // The panels roll with the ground the ball covers, in its on-screen
  // direction of travel.
  const Vec3 moved = world - lastBallWorld;
  const float distance = RenderMath::length(moved);
  lastBallWorld = world;
  ScreenPoint ahead;
  if (distance > 1e-4f && distance < Tuning::Player::TELEPORT_METRES &&
      projection.project(world + moved * (1.0f / distance), ahead))
  {
    const float dx = ahead.x - screen.x;
    const float dy = ahead.y - screen.y;
    const float length = std::hypot(dx, dy);
    if (length > 1e-3f) ballRoll = {dx / length, dy / length};
    ballSpin = std::fmod(
        ballSpin + std::min(distance / B::RADIUS, B::SPIN_MAX_PER_FRAME),
        TWO_PI);
  }
  points.push_back(toImVec(screen));
  keys.push_back({screen.depth, screen.depth,
                  static_cast<std::uint32_t>(primitives.size())});
  primitives.push_back(primitive);
}

void MatchRenderer3D::State::drawBoard(const Primitive& primitive)
{
  using B = Tuning::Boards;
  const Stadium3D::AdBoard& board = geometry.boards[primitive.reference];
  const auto rotation =
      static_cast<std::size_t>(elapsedSeconds / B::ROTATE_SECONDS);
  const Stadium3D::SponsorStyle& style =
      Stadium3D::SPONSORS[(board.sponsor + rotation) %
                          Stadium3D::SPONSORS.size()];
  const ImVec2* quad = points.data() + primitive.firstPoint;
  const std::array<ImVec2, 4> corners{quad[0], quad[1], quad[2], quad[3]};
  const std::array<ImU32, 4> colors{
      shadeColor(style.background, 0.8f), shadeColor(style.background, 0.8f),
      shadeColor(style.background, 1.15f), shadeColor(style.background, 1.15f)};
  emitRaw(corners, colors);

  // Fit the lettering box on the board, then decide how it can be shown
  // from its projected height at the board centre.
  ImFont* font = ImGui::GetFont();
  const ImVec2 unitSize =
      font->CalcTextSizeA(B::MIN_FONT_SIZE, FLT_MAX, 0.0f, style.text);
  if (unitSize.x <= 0.0f || unitSize.y <= 0.0f) return;
  float textHeight = B::HEIGHT * B::TEXT_HEIGHT_RATIO;
  float textWidth = textHeight * unitSize.x / unitSize.y;
  const float maximumWidth = board.length * B::TEXT_WIDTH_RATIO;
  if (textWidth > maximumWidth)
  {
    textHeight *= maximumWidth / textWidth;
    textWidth = maximumWidth;
  }
  const Vec3 textOrigin = board.origin +
                          board.right * ((board.length - textWidth) * 0.5f) +
                          UP * ((B::HEIGHT - textHeight) * 0.5f);
  const Vec3 textCentre =
      textOrigin + board.right * (textWidth * 0.5f) + UP * (textHeight * 0.5f);
  ScreenPoint centre;
  if (!projection.project(textCentre, centre)) return;
  const float textPixels = textHeight * projection.focalPixels / centre.depth;
  if (textPixels < B::MIN_TEXT_PIXELS)
  {
    drawBoardLogo(board, style, textOrigin, textWidth, textHeight);
    return;
  }

  // Bake the glyphs close to their on-screen size (quantised so only a few
  // sizes are ever cached) and map each vertex onto the board plane, so the
  // lettering stays crisp and follows the perspective.
  const float step = std::round(std::log(textPixels / B::MIN_FONT_SIZE) /
                                std::log(B::FONT_STEP));
  const float fontSize = std::min(
      B::MIN_FONT_SIZE * std::pow(B::FONT_STEP, step), B::MAX_FONT_SIZE);
  const ImVec2 textSize =
      font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, style.text);
  if (textSize.x <= 0.0f || textSize.y <= 0.0f) return;
  const ImVec2 canonical{projection.rect.x, projection.rect.y};
  const ImVec4 unclipped{-1e6f, -1e6f, 1e6f, 1e6f};
  const int firstVertex = drawList->VtxBuffer.Size;
  drawList->AddText(font, fontSize, canonical, style.foreground, style.text,
                    nullptr, 0.0f, &unclipped);
  for (int index = firstVertex; index < drawList->VtxBuffer.Size; ++index)
  {
    ImDrawVert& vertex = drawList->VtxBuffer[index];
    const float u = (vertex.pos.x - canonical.x) / textSize.x;
    const float v = (vertex.pos.y - canonical.y) / textSize.y;
    const Vec4 clip =
        projection.toClip(textOrigin + board.right * (u * textWidth) +
                          UP * ((1.0f - v) * textHeight));
    const ScreenPoint mapped = projection.clipToScreen(
        {clip.x, clip.y, clip.z, std::max(clip.w, projection.nearPlane)});
    vertex.pos = {mapped.x, mapped.y};
  }
}

void MatchRenderer3D::State::drawBoardLogo(const Stadium3D::AdBoard& board,
                                           const Stadium3D::SponsorStyle& style,
                                           Vec3 origin, float width,
                                           float height)
{
  // Too small to read: a clean two-tone word mark in the sponsor's colours.
  using B = Tuning::Boards;
  const float barHeight = height * B::LOGO_BAR_HEIGHT;
  const Vec3 bottom = origin + UP * ((height - barHeight) * 0.5f);
  const float markWidth = width * B::LOGO_MARK_SHARE;
  const auto bar = [&](float from, float to, ImU32 color)
  {
    const std::array<Vec3, 4> corners{
        bottom + board.right * from, bottom + board.right * to,
        bottom + board.right * to + UP * barHeight,
        bottom + board.right * from + UP * barHeight};
    const std::array<ImU32, 4> colors{color, color, color, color};
    drawWorldPolygon(corners, colors, false);
  };
  bar(0.0f, markWidth * 0.8f,
      mixColor(style.foreground, style.background, 0.35f));
  bar(markWidth, width, style.foreground);
}

void MatchRenderer3D::State::flushSorted()
{
  std::sort(keys.begin(), keys.end(),
            [](const SortKey& a, const SortKey& b)
            {
              if (a.objectDepth != b.objectDepth)
                return a.objectDepth > b.objectDepth;
              return a.localDepth > b.localDepth;
            });
  for (const SortKey& key : keys)
  {
    const Primitive& primitive = primitives[key.primitive];
    const ImVec2* first = points.data() + primitive.firstPoint;
    switch (primitive.kind)
    {
      case PrimitiveKind::POLYGON:
        drawList->AddConvexPolyFilled(first, primitive.pointCount,
                                      primitive.color);
        break;
      case PrimitiveKind::LINE:
        drawList->AddLine(first[0], first[1], primitive.color, primitive.size);
        break;
      case PrimitiveKind::HEAD:
        drawHead(primitive);
        break;
      case PrimitiveKind::BALL:
      {
        const float radius = primitive.size;
        drawList->AddCircleFilled(first[0], radius, Tuning::Ball::SHADE_COLOR);
        drawList->AddCircleFilled(
            {first[0].x - radius * 0.14f, first[0].y - radius * 0.14f},
            radius * 0.82f, Tuning::Ball::COLOR);
        if (radius >= 3.0f)
        {
          // Three dark panels turning over the ball along its roll.
          const ImVec2 across{-ballRoll.y, ballRoll.x};
          for (int patch = 0; patch < 3; ++patch)
          {
            const float phase =
                ballSpin + TWO_PI * static_cast<float>(patch) / 3.0f;
            const float facing = std::cos(phase);
            if (facing < -0.1f) continue;
            const float shift = std::sin(phase) * radius * 0.52f;
            const float side = static_cast<float>(patch - 1) * radius * 0.28f;
            drawList->AddCircleFilled(
                {first[0].x + ballRoll.x * shift + across.x * side,
                 first[0].y + ballRoll.y * shift + across.y * side},
                radius * 0.26f * (0.5f + 0.5f * std::max(facing, 0.0f)),
                Tuning::Ball::PATCH_COLOR);
          }
        }
        drawList->AddCircleFilled(
            {first[0].x - radius * 0.38f, first[0].y - radius * 0.4f},
            radius * 0.22f, IM_COL32_WHITE);
        break;
      }
      case PrimitiveKind::NET:
      {
        drawList->AddConvexPolyFilled(first, 4, primitive.color);
        for (std::uint16_t index = 4; index + 1 < primitive.pointCount;
             index = static_cast<std::uint16_t>(index + 2))
        {
          drawList->AddLine(first[index], first[index + 1],
                            primitive.secondaryColor, 1.0f);
        }
        break;
      }
      case PrimitiveKind::BOARD:
        drawBoard(primitive);
        break;
      case PrimitiveKind::TEXT:
        drawBackText(primitive);
        break;
    }
  }
}

void MatchRenderer3D::State::drawHead(const Primitive& primitive)
{
  // A shaded disc with the hair laid over the crown and the back of the
  // head, and the face turned the way the player looks.
  const ImVec2* first = points.data() + primitive.firstPoint;
  const ImVec2 centre = first[0];
  const float radius = std::max(primitive.size, 1.0f);
  const ImU32 skin = primitive.color;
  const ImU32 hair = primitive.secondaryColor;
  const auto style =
      static_cast<HairStyle>(primitive.reference & HEAD_STYLE_MASK);
  if (radius < 2.2f)
  {
    drawList->AddCircleFilled(
        centre, radius,
        style == HairStyle::SHAVED ? skin : mixColor(skin, hair, 0.35f));
    return;
  }
  const float facing =
      static_cast<float>((primitive.reference >> HEAD_FACING_SHIFT) & 0xFFU) /
          255.0f * 2.0f -
      1.0f;
  // Where the face points on screen, about sin(angle) of a radius.
  const ImVec2 face{(first[1].x - centre.x) / radius,
                    (first[1].y - centre.y) / radius};
  const auto at = [&](float x, float y)
  { return ImVec2{centre.x + x * radius, centre.y + y * radius}; };
  drawList->AddCircleFilled(centre, radius, shadeColor(skin, 0.74f));
  drawList->AddCircleFilled(at(-0.16f, -0.12f), radius * 0.76f, skin);
  const ImU32 hairColor = style == HairStyle::BUZZ ? mixColor(hair, skin, 0.4f)
                          : style == HairStyle::SHAVED
                              ? mixColor(skin, hair, 0.18f)
                              : hair;
  const bool fromBehind = facing < -0.35f;
  if (fromBehind)
  {
    // The back of the head: hair down to the nape.
    drawList->AddCircleFilled(
        at(0.0f, -0.08f), radius * (style == HairStyle::VOLUME ? 1.0f : 0.93f),
        hairColor);
    if (style == HairStyle::LONG)
      drawList->AddCircleFilled(at(0.0f, 0.42f), radius * 0.62f, hairColor);
    return;
  }
  const float crown = style == HairStyle::VOLUME ? 0.4f : 0.3f;
  const float capRadius = style == HairStyle::VOLUME ? 0.98f
                          : style == HairStyle::LONG ? 0.95f
                                                     : 0.86f;
  drawList->AddCircleFilled(at(-face.x * 0.3f, -crown - face.y * 0.3f),
                            radius * capRadius, hairColor);
  if (style == HairStyle::LONG)
  {
    drawList->AddCircleFilled(at(-face.x * 0.55f, 0.25f - face.y * 0.4f),
                              radius * 0.6f, hairColor);
  }
  // The face, lit from the same side as the body.
  const ImVec2 faceCentre = at(face.x * 0.3f, 0.14f + face.y * 0.3f);
  const float faceRadius = radius * 0.66f;
  drawList->AddCircleFilled(faceCentre, faceRadius,
                            shadeColor(skin, 0.9f + 0.1f * facing));
  if ((primitive.reference & HEAD_BEARD_BIT) != 0U && radius >= 3.0f &&
      facing > -0.1f)
  {
    const float pi = std::numbers::pi_v<float>;
    drawList->PathArcTo(faceCentre, faceRadius, 0.15f * pi, 0.85f * pi);
    drawList->PathFillConvex(mixColor(hair, skin, 0.35f));
  }
}

void MatchRenderer3D::State::drawVignette()
{
  const float x0 = projection.rect.x;
  const float y0 = projection.rect.y;
  const float x1 = x0 + projection.rect.width;
  const float y1 = y0 + projection.rect.height;
  const float edgeX = projection.rect.width * Tuning::Sky::VIGNETTE_EDGE;
  const float edgeY = projection.rect.height * Tuning::Sky::VIGNETTE_EDGE;
  const ImU32 dark =
      day ? Tuning::Day::VIGNETTE_COLOR : Tuning::Sky::VIGNETTE_COLOR;
  const ImU32 clear = withAlpha(dark, 0);
  drawList->AddRectFilledMultiColor({x0, y0}, {x1, y0 + edgeY}, dark, dark,
                                    clear, clear);
  drawList->AddRectFilledMultiColor({x0, y1 - edgeY}, {x1, y1}, clear, clear,
                                    dark, dark);
  drawList->AddRectFilledMultiColor({x0, y0}, {x0 + edgeX, y1}, dark, clear,
                                    clear, dark);
  drawList->AddRectFilledMultiColor({x1 - edgeX, y0}, {x1, y1}, clear, dark,
                                    dark, clear);
}

void MatchRenderer3D::State::drawLabelsAndHover(
    const MatchRenderOptions& options)
{
  const ImVec2 mouse = ImGui::GetMousePos();
  const bool mouseInView =
      mouse.x >= projection.rect.x &&
      mouse.x <= projection.rect.x + projection.rect.width &&
      mouse.y >= projection.rect.y &&
      mouse.y <= projection.rect.y + projection.rect.height;
  // HUD sizes follow the UI scale (HiDPI).
  const float ui = ImGui::GetStyle().FontScaleDpi;
  const float hoverPadding = Tuning::Hud::HOVER_PADDING * ui;
  const PlayerOnScreen* hovered = nullptr;
  for (const PlayerOnScreen& entry : playersOnScreen)
  {
    const float halfWidth =
        std::max((entry.feet.y - entry.headTop.y) * 0.3f, hoverPadding);
    if (mouseInView && mouse.x >= entry.feet.x - halfWidth &&
        mouse.x <= entry.feet.x + halfWidth &&
        mouse.y >= entry.headTop.y - hoverPadding &&
        mouse.y <= entry.feet.y + hoverPadding &&
        (!hovered || entry.depth < hovered->depth))
      hovered = &entry;
  }

  for (const PlayerOnScreen& entry : playersOnScreen)
  {
    const Player* source = entry.player->player;
    if (!source) continue;
    if (!options.showPlayerNames && !entry.player->possessesBall &&
        &entry != hovered)
      continue;
    const std::string& name = source->getLastName();
    const ImVec2 size = ImGui::CalcTextSize(name.c_str());
    const ImVec2 position{
        entry.headTop.x - size.x * 0.5f,
        entry.headTop.y - Tuning::Hud::LABEL_OFFSET * ui - size.y};
    const float padding = Tuning::Hud::LABEL_PADDING * ui;
    drawList->AddRectFilled(
        {position.x - padding, position.y - padding * 0.5f},
        {position.x + size.x + padding, position.y + size.y + padding * 0.5f},
        Tuning::Hud::LABEL_BACK_COLOR, Tuning::Hud::ROUNDING * ui);
    drawList->AddText(position, Tuning::Hud::TEXT_COLOR, name.c_str());
  }

  if (hovered && hovered->player->player)
  {
    const MatchRenderPlayer& player = *hovered->player;
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(player.player->getName().c_str());
    ImGui::TextUnformatted(
        fmt::sprintf(LOC("MATCH_PLAYER_TOOLTIP_SHORT"),
                     RoleUtils::shortName(player.player->getRole()),
                     playerIntentLabel(player.intent),
                     static_cast<double>(player.stamina * 100.0f))
            .c_str());
    ImGui::EndTooltip();
  }
}

void MatchRenderer3D::State::drawScoreBug(const MatchRenderSnapshot& snapshot,
                                          const MatchRenderOptions& options)
{
  if (!options.homeLabel || !options.awayLabel) return;
  using H = Tuning::Hud;
  char score[16];
  std::snprintf(score, sizeof(score), "%d - %d", snapshot.homeScore,
                snapshot.awayScore);
  const int minute = static_cast<int>(snapshot.matchTimeMinutes);
  const int second = static_cast<int>(
      (snapshot.matchTimeMinutes - static_cast<float>(minute)) * 60.0f);
  char clock[16];
  std::snprintf(clock, sizeof(clock), "%02d:%02d", minute, second);

  const ImVec2 homeSize = ImGui::CalcTextSize(options.homeLabel);
  const ImVec2 awaySize = ImGui::CalcTextSize(options.awayLabel);
  const ImVec2 scoreSize = ImGui::CalcTextSize(score);
  const ImVec2 clockSize = ImGui::CalcTextSize(clock);
  const float height = homeSize.y + H::PADDING * 2.0f;
  float x = projection.rect.x + H::MARGIN;
  const float y = projection.rect.y + H::MARGIN;

  const auto panel = [&](float width, ImU32 color)
  {
    drawList->AddRectFilled({x, y}, {x + width, y + height}, color,
                            H::ROUNDING);
  };
  const auto team = [&](const char* label, ImVec2 size, ImU32 kitColor)
  {
    const float width = H::CHIP_WIDTH + size.x + H::PADDING * 3.0f;
    panel(width, H::PANEL_COLOR);
    drawList->AddRectFilled(
        {x + H::PADDING, y + H::PADDING},
        {x + H::PADDING + H::CHIP_WIDTH, y + height - H::PADDING}, kitColor);
    drawList->AddText({x + H::CHIP_WIDTH + H::PADDING * 2.0f, y + H::PADDING},
                      H::TEXT_COLOR, label);
    x += width;
  };
  team(options.homeLabel, homeSize, kits.home.shirt);
  const float scoreWidth = scoreSize.x + H::PADDING * 2.0f;
  panel(scoreWidth, H::SCORE_PANEL_COLOR);
  drawList->AddText({x + H::PADDING, y + H::PADDING}, H::SCORE_TEXT_COLOR,
                    score);
  x += scoreWidth;
  team(options.awayLabel, awaySize, kits.away.shirt);
  x += H::PADDING;
  const float clockWidth = clockSize.x + H::PADDING * 2.0f;
  panel(clockWidth, H::PANEL_COLOR);
  drawList->AddText({x + H::PADDING, y + H::PADDING}, H::TEXT_COLOR, clock);
}

MatchRenderer3D::MatchRenderer3D() : state(std::make_unique<State>()) {}

MatchRenderer3D::~MatchRenderer3D() = default;

bool MatchRenderer3D::projectPitch(PitchPoint pitch, float heightMetres,
                                   float& screenX, float& screenY) const
{
  const State& s = *state;
  if (!s.hasProjection) return false;
  ScreenPoint point;
  if (!s.projection.project(s.worldOf(pitch, heightMetres),
                            point))
    return false;
  screenX = point.x;
  screenY = point.y;
  return true;
}

void MatchRenderer3D::render(const MatchRenderSnapshot& snapshot,
                             const MatchRenderOptions& options,
                             const MatchViewport& viewport)
{
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  if (!drawList || viewport.width < 2.0f || viewport.height < 2.0f) return;
  State& s = *state;
  s.drawList = drawList;
  s.whitePixel = ImGui::GetFontTexUvWhitePixel();
  const float deltaSeconds =
      std::clamp(options.frameSeconds, 0.0f, Tuning::Camera::MAX_FRAME_SECONDS);
  s.elapsedSeconds += deltaSeconds;
  s.frameSeconds = deltaSeconds;
  s.reducedMotion = Theme::reducedMotion();
  // Daylight from the kick-off time, or forced by the view option; night
  // otherwise (and when the time is unknown).
  s.day = options.dayLook ||
          (options.kickoffMinutes >= Tuning::Day::DAY_FROM_MINUTES &&
           options.kickoffMinutes < Tuning::Day::DAY_UNTIL_MINUTES);
  s.light = s.day ? dayLight() : nightLight();
  s.hideFigures = options.hidePlayerFigures;
  s.training = snapshot.pitchSurface == PitchSurface::TRAINING;
  s.pitchLength = snapshot.pitchLengthMetres;
  s.pitchWidth = snapshot.pitchWidthMetres;
  s.zoneOffsetX = s.training ? 0.5f * (LENGTH - s.pitchLength) : 0.0f;
  s.zoneOffsetY = s.training ? 0.5f * (WIDTH - s.pitchWidth) : 0.0f;
  s.mouthHalf = s.training && snapshot.pitchGoal.present()
                    ? snapshot.pitchGoal.halfWidth + snapshot.pitchGoal.post
                    : 0.0f;

  s.prepareMatch(snapshot);
  if (s.slots.size() != snapshot.players.size())
    s.slots.resize(snapshot.players.size());
  const float alpha = snapshot.interpolationAlpha;
  // Simulated time of this frame from the match clock (0 while paused).
  const float rawSimSeconds =
      s.lastMatchMinutes >= 0.0f
          ? (snapshot.matchTimeMinutes - s.lastMatchMinutes) * 60.0f
          : 0.0f;
  s.simSeconds = std::clamp(rawSimSeconds, 0.0f, Tuning::Pose::MAX_SIM_SECONDS);
  const bool firstFrame = s.lastMatchMinutes < 0.0f;
  s.lastMatchMinutes = snapshot.matchTimeMinutes;
  s.livePlay = snapshot.state == MatchState::PLAYING;
  const Vec3 previousBall = s.ballWorld;
  s.ballWorld = s.worldOf(
      lerpRenderPosition(snapshot.ball.previousPosition,
                         snapshot.ball.currentPosition, alpha),
      ballHeightMetres(snapshot.ball, alpha));
  // A playback jump (highlights skipping ahead, a new match state) moves
  // the play far in one frame: the cameras cut to it instead of easing.
  s.jumped = !firstFrame &&
             (std::abs(rawSimSeconds) > Tuning::Camera::JUMP_SIM_SECONDS ||
              RenderMath::length(s.ballWorld - previousBall) >
                  Tuning::Camera::JUMP_METRES);
  s.findBallHolder(snapshot, alpha);
  s.heldBallPlaced = false;
  s.homeKeeperState = snapshot.homeGoalkeeperState;
  s.awayKeeperState = snapshot.awayGoalkeeperState;
  s.playerStats = snapshot.playerStats;
  s.updateGoalMoment(snapshot);
  for (std::size_t index = 0; index < snapshot.players.size(); ++index)
    s.updateMotion(snapshot.players[index], s.slots[index], alpha,
                   deltaSeconds);
  s.detectTouches(snapshot);
  {
    // The crowd rises for shots and stays on its feet a moment after.
    using C = Tuning::Crowd;
    const bool chance = s.livePlay && snapshot.ball.isShot;
    const float rate = chance ? C::RISE_RATE : C::SETTLE_RATE;
    s.crowdTension += ((chance ? 1.0f : 0.0f) - s.crowdTension) *
                      RenderMath::dampingFactor(rate, deltaSeconds);
  }
  s.updateCrowdMotion();
  MatchCameraFocus focus = s.computeFocus(snapshot, deltaSeconds);
  // Play mode: the camera keeps the human's active footballer in view.
  if (options.activePlayer != 0)
    for (const MatchRenderPlayer& player : snapshot.players)
      if (player.player && player.onPitch &&
          player.player->getId() == options.activePlayer)
      {
        focus.hasActive = true;
        focus.active = s.worldOf(lerpRenderPosition(
            player.previousPosition, player.currentPosition, alpha));
      }
  // Mouse input maps through the frame the user was looking at.
  if (s.jumped && options.cameraMode != MatchCameraMode::FREE)
    s.camera.snap(focus, options.cameraMode);
  else
    s.camera.update(focus, options.cameraMode,
                    s.cameraControl(options.cameraInput), deltaSeconds);
  s.projection = RenderMath::Projection::make(
      s.camera.eye(), s.camera.target(), s.camera.verticalFov(),
      Tuning::Camera::NEAR_PLANE, Tuning::Camera::FAR_PLANE,
      {viewport.x, viewport.y, viewport.width, viewport.height});
  s.hasProjection = true;

  drawList->PushClipRect(
      {viewport.x, viewport.y},
      {viewport.x + viewport.width, viewport.y + viewport.height}, true);
  s.drawSky();
  if (s.training) s.drawTrainingGround();
  for (const Stadium3D::GroundPolygon& polygon : s.geometry.ground)
  {
    if (s.training) break;
    s.drawWorldPolygon(
        std::span<const Vec3>(polygon.points.data(), polygon.count),
        std::span<const ImU32>(polygon.colors.data(), polygon.count), false);
  }
  if (!s.training) s.drawPitch();
  for (const Stadium3D::GroundPolygon& polygon : s.geometry.wear)
  {
    if (s.training) break;
    s.drawWorldPolygon(
        std::span<const Vec3>(polygon.points.data(), polygon.count),
        std::span<const ImU32>(polygon.colors.data(), polygon.count), false);
  }
  for (const Stadium3D::GroundPolygon& polygon : s.geometry.markings)
  {
    if (s.training) break;
    s.drawWorldPolygon(
        std::span<const Vec3>(polygon.points.data(), polygon.count),
        std::span<const ImU32>(polygon.colors.data(), polygon.count), true);
  }
  // By day the roofs shade part of the pitch, markings included.
  for (const Stadium3D::GroundPolygon& polygon : s.geometry.standShadows)
  {
    if (s.training) break;
    s.drawWorldPolygon(
        std::span<const Vec3>(polygon.points.data(), polygon.count),
        std::span<const ImU32>(polygon.colors.data(), polygon.count), false);
  }
  s.drawShadows(snapshot, alpha);
#ifdef DEBUG
  if (options.showAiDebug)
  {
    for (const MatchRenderPlayer& player : snapshot.players)
    {
      ScreenPoint from;
      ScreenPoint to;
      if (s.projection.project(
              s.worldOf(lerpRenderPosition(
                  player.previousPosition, player.currentPosition, alpha)),
              from) &&
          s.projection.project(
              s.worldOf(player.movementTarget), to))
      {
        drawList->AddLine(toImVec(from), toImVec(to),
                          Tuning::Hud::DEBUG_TARGET_COLOR, 1.5f);
      }
    }
  }
#endif
  if (!s.training) s.drawStadium();

  s.points.clear();
  s.primitives.clear();
  s.keys.clear();
  s.texts.clear();
  s.playersOnScreen.clear();
  if (!s.hideFigures)
    for (std::size_t index = 0; index < snapshot.players.size(); ++index)
      if (!snapshot.players[index].hidden &&
          !(s.training && !snapshot.players[index].onPitch))
        s.addPlayer(snapshot.players[index], s.slots[index], alpha);
  if (s.training)
  {
    s.addKit(snapshot);
    s.addTrainingGoals(snapshot);
  }
  else
  {
    s.addGoals();
    s.addCornerFlags();
    s.addBoards();
    s.addFixtures();
  }
  // The people round the pitch (a training pitch: only its serving coach).
  for (std::size_t index = 0; index < snapshot.touchline.size(); ++index)
    s.addTouchlinePerson(snapshot.touchline[index], index, alpha);
  if (!snapshot.ball.outOfSight) s.addBall(snapshot, alpha);
  s.flushSorted();

  s.drawVignette();
  s.drawLabelsAndHover(options);
  s.drawScoreBug(snapshot, options);
  s.drawGoalSting(snapshot);
  drawList->PopClipRect();
}
