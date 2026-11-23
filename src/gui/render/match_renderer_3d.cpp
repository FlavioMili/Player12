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
#include <cstddef>
#include <cstdint>
#include <format>
#include <numbers>
#include <span>
#include <string>
#include <utility>
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
template <std::size_t Size, class... Args>
void formatText(std::array<char, Size>& buffer,
                std::format_string<Args...> format, Args&&... args)
{
  static_assert(Size > 0);
  const auto result = std::format_to_n(buffer.data(), Size - 1, format,
                                       std::forward<Args>(args)...);
  *result.out = '\0';
}

using RenderMath::ScreenPoint;
using RenderMath::Vec3;
using RenderMath::Vec4;
using Tuning = MatchRender3DTuning;

constexpr float LENGTH = MatchTuning::Pitch::LENGTH_METRES;
constexpr float WIDTH = MatchTuning::Pitch::WIDTH_METRES;
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

struct SegmentStyle
{
  Vec3 hint;
  float objectDepth;
  PlayerLod lod;
};

/** Per-snapshot-slot animation memory. */
struct GaitState
{
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
  /** Smoothed jump towards a high ball (m). */
  float jump = 0.0f;
};

struct TimedActionState
{
  /** Timed ball action, simulated seconds into it, where the ball was met
   * and where it went, and the acting leg. */
  PlayerRig::Event event = PlayerRig::Event::NONE;
  float eventSeconds = 0.0f;
  Vec3 contact;
  Vec3 eventDirection{1.0f, 0.0f, 0.0f};
  std::size_t eventLeg = 1;
  float lastTackleCooldown = 0.0f;
  /** Hands to the head when the other side scores. */
  bool handsOnHead = false;
};

struct KeeperPoseState
{
  /** Keeper pose blends (0..1) and the side of the current dive. */
  float setBlend = 0.0f;
  float holdBlend = 0.0f;
  float diveBlend = 0.0f;
  float diveSide = 1.0f;
};

struct AnimationSlot
{
  const Player* player = nullptr;
  PlayerLook looks;
  bool leftFooted = false;
  /** Printed on the back: shirt number and upper-case surname. */
  int number = 0;
  std::string backName;
  GaitState gait;
  TimedActionState action;
  KeeperPoseState keeper;
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
  Pose result;
  result.lean = std::lerp(from.lean, to.lean, t);
  result.twist = std::lerp(from.twist, to.twist, t);
  result.turnRoll = std::lerp(from.turnRoll, to.turnRoll, t);
  result.bodyRoll = std::lerp(from.bodyRoll, to.bodyRoll, t);
  result.hipDrop = std::lerp(from.hipDrop, to.hipDrop, t);
  result.lift = std::lerp(from.lift, to.lift, t);
  for (std::size_t index = 0; index < 2; ++index)
  {
    result.thigh[index] = std::lerp(from.thigh[index], to.thigh[index], t);
    result.knee[index] = std::lerp(from.knee[index], to.knee[index], t);
    result.legSpread[index] =
        std::lerp(from.legSpread[index], to.legSpread[index], t);
    result.armSwing[index] =
        std::lerp(from.armSwing[index], to.armSwing[index], t);
    result.armSpread[index] =
        std::lerp(from.armSpread[index], to.armSpread[index], t);
    result.elbow[index] = std::lerp(from.elbow[index], to.elbow[index], t);
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
    return G::Shape{
        std::lerp(a.speed, b.speed, t), std::lerp(a.thigh, b.thigh, t),
        std::lerp(a.knee, b.knee, t),   std::lerp(a.arm, b.arm, t),
        std::lerp(a.elbow, b.elbow, t), std::lerp(a.lean, b.lean, t),
        std::lerp(a.bob, b.bob, t)};
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
  using enum PlayerRig::Event;
  using R = Tuning::Rig;
  switch (event)
  {
    case PASS:
    case SHOT:
    case CROSS:
      return kickTiming().total();
    case HEADER:
      return Tuning::Kick::HEADER_SECONDS;
    case TACKLE:
      return R::TACKLE_SECONDS;
    case SLIDE:
      return R::SLIDE_SECONDS;
    case THROW:
      return R::THROW_SECONDS;
    case NONE:
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
  using enum PlayerRig::Event;
  const float total = eventDuration(event);
  if (total <= 0.0f || seconds >= total) return 0.0f;
  float in = 0.0f;
  float hold = total * 0.55f;
  switch (event)
  {
    case PASS:
    case SHOT:
    case CROSS:
      hold = Tuning::Kick::CONTACT_HOLD_SECONDS +
             Tuning::Kick::FOLLOW_THROUGH_SECONDS;
      break;
    case HEADER:
      in = 0.05f;
      break;
    case TACKLE:
      in = 0.06f;
      break;
    case SLIDE:
      in = 0.12f;
      hold = total * 0.65f;
      break;
    case THROW:
    case NONE:
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

struct BackPrintContext
{
  const RenderMath::Projection& projection;
  std::vector<ImVec2>& points;
  std::vector<Primitive>& primitives;
  std::vector<SortKey>& keys;
  std::vector<const char*>& texts;
  ImU32 color;
  Vec3 across;
  Vec3 down;
  float objectDepth;
};

void appendBackPrint(const BackPrintContext& context, Vec3 middle,
                     float glyphHeight, float minimumPixels, const char* text)
{
  using N = Tuning::Number;
  // Digits and capitals fill ~72% of the font's line height.
  const float lineHeight = glyphHeight / 0.72f;
  ScreenPoint mid;
  ScreenPoint below;
  if (!context.projection.project(middle, mid) ||
      !context.projection.project(middle + context.down * (lineHeight * 0.5f),
                                  below))
    return;
  const float linePixels = 2.0f * std::hypot(below.x - mid.x, below.y - mid.y);
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
  if (!context.projection.project(middle + context.across * halfWidth, right))
    return;
  Primitive primitive;
  primitive.firstPoint = static_cast<std::uint32_t>(context.points.size());
  primitive.pointCount = 3;
  primitive.kind = PrimitiveKind::TEXT;
  primitive.color = context.color;
  primitive.size = fontSize;
  primitive.reference = static_cast<std::uint32_t>(context.texts.size());
  context.texts.push_back(text);
  context.points.push_back(toImVec(mid));
  context.points.push_back(toImVec(right));
  context.points.push_back(toImVec(below));
  // Just in front of the shirt's back face.
  context.keys.push_back(
      {context.objectDepth, mid.depth - 0.05f,
       static_cast<std::uint32_t>(context.primitives.size())});
  context.primitives.push_back(primitive);
}

template <class Displace>
void appendNetStrand(const RenderMath::Projection& projection,
                     std::vector<ImVec2>& points, bool rippling, int segments,
                     Vec3 from, Vec3 to, Displace&& displaced)
{
  ScreenPoint a;
  ScreenPoint b;
  if (!rippling)
  {
    if (!projection.project(from, a) || !projection.project(to, b)) return;
    points.push_back(toImVec(a));
    points.push_back(toImVec(b));
    return;
  }
  if (!projection.project(displaced(from), a)) return;
  for (int segment = 1; segment <= segments; ++segment)
  {
    if (const Vec3 point = RenderMath::lerp(
            from, to,
            static_cast<float>(segment) / static_cast<float>(segments));
        !projection.project(displaced(point), b))
      return;
    points.push_back(toImVec(a));
    points.push_back(toImVec(b));
    a = b;
  }
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
  std::byte cheeringMask{};
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
                  SegmentStyle style);
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
                    const BodyFrame& frame, float scale, float objectDepth);
  void drawBackText(const Primitive& primitive);
  /** Starts kick, header and tackle poses from the engine's touches. */
  void detectTouches(const MatchRenderSnapshot& snapshot);
  /** Tracks the goal being celebrated (sting, net ripple, reactions). */
  void updateGoalMoment(const MatchRenderSnapshot& snapshot);
  void addGoals();
  void addCornerFlags();
  void addBoards();
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

struct CrowdFaceLayout
{
  float upX;
  float upY;
  float acrossX;
  float acrossY;
  float halfDotFocal;
  float left;
  float right;
  float top;
  float bottom;
  bool cheering;
};

ImVec2 crowdPoint(const CrowdFaceLayout& layout, float x, float y, float across,
                  float up, float dx, float dy)
{
  return {x + layout.acrossX * across + dx * up,
          y + layout.acrossY * across + dy * up};
}

template <class RendererState>
void drawCrowdDot(RendererState& state, const CrowdFaceLayout& layout,
                  const Stadium3D::CrowdClump& clump,
                  const Stadium3D::CrowdDot& dot, std::uint32_t detail)
{
  using C = Tuning::Crowd;
  namespace Flags = Stadium3D::CrowdFlags;
  const Vec4 clip = state.projection.toClip(dot.base);
  if (clip.w < state.projection.nearPlane) return;
  ScreenPoint base = state.projection.clipToScreen(clip);
  const float inverseDepth = 1.0f / clip.w;
  const float halfWidth = layout.halfDotFocal * inverseDepth;
  const float dx = layout.upX * C::DOT_HEIGHT * inverseDepth;
  const float dy = layout.upY * C::DOT_HEIGHT * inverseDepth;
  if (base.x + halfWidth < layout.left || base.x - halfWidth > layout.right ||
      base.y + dy > layout.bottom || base.y < layout.top)
    return;
  const bool celebrating =
      layout.cheering && (dot.flags & state.cheeringMask) != std::byte{};
  const float lift = state.crowdSway[dot.phase] + state.crowdRise[dot.phase] +
                     (celebrating ? state.crowdHop[dot.phase] : 0.0f);
  base.x += dx * lift;
  base.y += dy * lift;
  const ImU32 body =
      detail < 256U ? mixColor256(clump.color, dot.body, detail) : dot.body;
  const ImU32 head =
      detail < 256U ? mixColor256(clump.color, dot.head, detail) : dot.head;
  const auto point = [&](float across, float up)
  { return crowdPoint(layout, base.x, base.y, across, up, dx, dy); };
  if (halfWidth * 2.0f < C::HEAD_DETAIL_PIXELS)
  {
    const float headHalf = halfWidth * C::HEAD_WIDTH_SHARE;
    const std::array<ImVec2, 6> outline{
        point(-halfWidth, 0.0f),         point(halfWidth, 0.0f),
        point(halfWidth, C::BODY_SHARE), point(headHalf, 1.0f),
        point(-headHalf, 1.0f),          point(-halfWidth, C::BODY_SHARE)};
    const std::array<ImU32, 6> colors{shadeColor256(body, 184U),
                                      shadeColor256(body, 184U),
                                      body,
                                      head,
                                      head,
                                      body};
    state.batchFan(outline, colors);
    return;
  }

  const float shoulder = C::BODY_SHARE;
  const float shoulderLow = shoulder - C::SHOULDER_DROP;
  const float narrow = halfWidth * C::SHOULDER_SHARE;
  const ImU32 lower = shadeColor256(body, 184U);
  const std::array<ImVec2, 6> torso{
      point(-halfWidth, 0.0f),       point(halfWidth, 0.0f),
      point(halfWidth, shoulderLow), point(narrow, shoulder),
      point(-narrow, shoulder),      point(-halfWidth, shoulderLow)};
  const std::array<ImU32, 6> torsoColors{lower, lower, body, body, body, body};
  state.batchFan(torso, torsoColors);

  if (celebrating)
  {
    const float armHalf = halfWidth * C::ARM_WIDTH_SHARE;
    const float reach = 1.0f + C::ARM_REACH;
    for (const float sideSign : {-1.0f, 1.0f})
    {
      const float from = sideSign * narrow;
      const float to = sideSign * halfWidth * 1.15f;
      state.batchQuad(
          point(from - armHalf, shoulder), point(from + armHalf, shoulder),
          point(to + armHalf, reach), point(to - armHalf, reach), body, head);
    }
    if ((dot.flags & Flags::SCARF) != std::byte{})
    {
      const KitColors& kit = (dot.flags & Flags::AWAY_FAN) != std::byte{}
                                 ? state.kits.away
                                 : state.kits.home;
      const float scarfHalf = halfWidth * C::SCARF_WIDTH_SHARE * 0.5f;
      const float scarfBottom = reach - C::SCARF_HEIGHT_SHARE;
      for (const float sideSign : {-1.0f, 1.0f})
      {
        const ImU32 color = sideSign < 0.0f ? kit.shirt : kit.trim;
        state.batchQuad(point(0.0f, scarfBottom),
                        point(sideSign * scarfHalf, scarfBottom),
                        point(sideSign * scarfHalf, reach), point(0.0f, reach),
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
    outline[corner] = point(headHalf * HEAD_SHAPE[corner][0],
                            headMiddle + headRadius * HEAD_SHAPE[corner][1]);
    headColors[corner] =
        HEAD_SHAPE[corner][1] < 0.0f ? shadeColor256(head, 210U) : head;
  }
  state.batchFan(outline, headColors);
}

template <class RendererState>
void drawPitchCell(RendererState& state, const Stadium3D::PitchGrid& grid,
                   std::size_t cell, std::size_t rows,
                   std::size_t cellsPerStripe)
{
  const auto stripe = static_cast<int>(cell / cellsPerStripe);
  const std::size_t withinStripe = cell % cellsPerStripe;
  const auto column = static_cast<int>(withinStripe / rows);
  const auto row = static_cast<int>(withinStripe % rows);
  const std::array<std::size_t, 4> corner{
      grid.index(stripe, column, row), grid.index(stripe, column + 1, row),
      grid.index(stripe, column + 1, row + 1),
      grid.index(stripe, column, row + 1)};
  std::array<ClipVertex, 4> vertices{};
  bool allInFront = true;
  for (std::size_t index = 0; index < 4; ++index)
  {
    vertices[index] = {state.pitchClip[corner[index]],
                       state.pitchColors[corner[index]]};
    allInFront =
        allInFront && vertices[index].clip.w >= state.projection.nearPlane;
  }
  if (!allInFront)
  {
    // Rare: the cell crosses the near plane (low free camera).
    state.releaseBatch();
    std::array<Vec3, 4> world{};
    std::array<ImU32, 4> colors{};
    for (std::size_t index = 0; index < 4; ++index)
    {
      world[index] = grid.points[corner[index]];
      colors[index] = vertices[index].color;
    }
    state.drawWorldPolygon(world, colors, false);
    return;
  }
  if (outsideClipVolume(vertices)) return;
  std::array<ImVec2, 4> screen{};
  std::array<ImU32, 4> colors{};
  for (std::size_t index = 0; index < 4; ++index)
  {
    screen[index] = state.pitchScreen[corner[index]];
    colors[index] = vertices[index].color;
  }
  state.batchFan(screen, colors);
}

template <class RendererState>
void initializeAnimationSlot(RendererState& state,
                             const MatchRenderPlayer& player,
                             AnimationSlot& slot, Vec3 root, float yaw)
{
  slot = AnimationSlot{};
  slot.player = player.player;
  slot.gait.lastPosition = root;
  slot.gait.lastSpeed = player.speedMetresPerSecond;
  slot.action.lastTackleCooldown = player.tackleCooldown;
  slot.gait.moveDirection = {std::cos(yaw), std::sin(yaw), 0.0f};
  if (player.player)
  {
    const std::uint32_t looks = cosmeticHash(player.player->getId());
    slot.gait.phase = static_cast<float>(looks % 628U) / 100.0f;
    slot.leftFooted = (looks >> 12) % 5U == 0U;
    slot.action.handsOnHead = (looks >> 14) % 2U == 0U;
    slot.number = state.shirtNumberFor(player);
    slot.backName = player.player->getLastName();
    for (char& letter : slot.backName)
    {
      if (letter >= 'a' && letter <= 'z')
        letter = static_cast<char>(letter - 'a' + 'A');
    }
  }
  slot.looks = lookFor(player.player ? player.player->getId() : 0U);
}

template <class RendererState>
float jumpTargetFor(const RendererState& state, const MatchRenderPlayer& player,
                    Vec3 root, float scale)
{
  using P = Tuning::Player;
  using R = Tuning::Rig;
  float target = 0.0f;
  if (state.livePlay && !player.isGoalkeeper && player.onPitch &&
      state.ballWorld.z >= R::HEADER_MIN_BALL &&
      state.ballWorld.z <= R::HEADER_MAX_BALL &&
      PlayerRig::flatDistance(state.ballWorld, root) < R::JUMP_REACH)
  {
    const float headHeight = (P::REFERENCE_HEIGHT_METRES - 0.1f) * scale;
    target = std::clamp(state.ballWorld.z - headHeight, 0.0f, R::MAX_JUMP);
  }
  if (state.livePlay && player.isGoalkeeper && player.onPitch &&
      state.heldBy == nullptr &&
      PlayerRig::flatDistance(state.ballWorld, root) <
          playerHeightMetres(player) *
              MatchTuning::Goalkeeper::HAND_REACH_HEIGHT_SHARE)
  {
    const float standingReach = playerHeightMetres(player) *
                                MatchTuning::Aerial::GOALKEEPER_ARM_REACH_RATIO;
    target = std::clamp(state.ballWorld.z - standingReach, 0.0F,
                        MatchTuning::Aerial::GOALKEEPER_BASE_JUMP_METRES +
                            MatchTuning::Aerial::GOALKEEPER_SKILL_JUMP_METRES);
  }
  return target;
}

float attackDirectionFor(std::span<const MatchRenderPlayer> players,
                         const MatchRenderPlayer& carrier)
{
  float direction = carrier.isHomeTeam ? 1.0f : -1.0f;
  for (const MatchRenderPlayer& player : players)
  {
    if (player.isHomeTeam != carrier.isHomeTeam || !player.isGoalkeeper ||
        !player.onPitch)
      continue;
    direction =
        player.currentPosition.x < MatchTuning::Pitch::CENTRE ? 1.0f : -1.0f;
  }
  return direction;
}

void setCelebrationFocus(MatchCameraFocus& focus,
                         std::span<const MatchRenderPlayer> players,
                         std::uint32_t scorer, float alpha)
{
  for (const MatchRenderPlayer& player : players)
  {
    if (!player.player || player.player->getId() != scorer) continue;
    focus.celebration = RenderMath::worldFromPitch(lerpRenderPosition(
        player.previousPosition, player.currentPosition, alpha));
    focus.hasCelebration = true;
  }
}

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
  focus.ball = RenderMath::worldFromPitch(lerpRenderPosition(
      snapshot.ball.previousPosition, snapshot.ball.currentPosition, alpha));
  focus.ballVelocity =
      (RenderMath::worldFromPitch(snapshot.ball.currentPosition) -
       RenderMath::worldFromPitch(snapshot.ball.previousPosition)) *
      (1.0f / MatchTuning::Timing::FIXED_STEP_SECONDS);

  if (const auto carrier = std::ranges::find_if(
          snapshot.players,
          [](const MatchRenderPlayer& player) { return player.possessesBall; });
      carrier != snapshot.players.end())
  {
    focus.hasCarrier = true;
    focus.carrier = RenderMath::worldFromPitch(lerpRenderPosition(
        carrier->previousPosition, carrier->currentPosition, alpha));
    focus.carrierYaw = RenderMath::worldYawFromFacing(RenderMath::lerpAngle(
        carrier->previousFacingAngle, carrier->currentFacingAngle, alpha));
    // Attack direction comes from where the carrier's own keeper stands, so
    // it stays right even if the engine ever swaps ends.
    float direction = attackDirectionFor(snapshot.players, *carrier);
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
    setCelebrationFocus(focus, snapshot.players, goal.scorer, alpha);
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
    if (const float limit = camera.distance() * 0.5f;
        projection.groundPointAt(input.panFromX, input.panFromY, from) &&
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
    if (signedArea(screenSpan) < 0.0f) std::ranges::reverse(screenSpan);
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
  if (ScreenPoint horizonPoint; projection.project(
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
  const auto alpha = static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFFU);
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
  if (const float swing =
          std::round(contrast * (across >= 0.0f ? 1.0f : -1.0f) * 500.0f) /
          500.0f;
      swing != pitchSwing || day != pitchDay ||
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
      const std::size_t first = grid.index(stripe, 0, 0);
      const std::size_t count =
          static_cast<std::size_t>(grid.columnVertices()) *
          static_cast<std::size_t>(grid.rows + 1);
      for (std::size_t offset = 0; offset < count; ++offset)
      {
        const std::size_t index = first + offset;
        pitchColors[index] = shadeColor(grid.colors[index], stripeLight);
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

  const auto rows = static_cast<std::size_t>(grid.rows);
  const auto columns = static_cast<std::size_t>(grid.columnsPerStripe);
  const std::size_t cellsPerStripe = rows * columns;
  const std::size_t cellCount =
      static_cast<std::size_t>(grid.stripes) * cellsPerStripe;
  for (std::size_t cell = 0; cell < cellCount; ++cell)
    drawPitchCell(*this, grid, cell, rows, cellsPerStripe);
  releaseBatch();
}

template <class RendererState>
void drawPlayerMarker(RendererState& state, const MatchRenderPlayer& player,
                      Vec3 root, float pixelHeight)
{
  if (pixelHeight >= Tuning::Player::MARKER_MAX_PIXELS) return;
  state.releaseBatch();
  const KitColors& teamKit =
      player.isHomeTeam ? state.kits.home : state.kits.away;
  const KitColors& keeperKit =
      player.isHomeTeam ? state.kits.homeGoalkeeper : state.kits.awayGoalkeeper;
  const KitColors& kit = player.isGoalkeeper ? keeperKit : teamKit;
  const float fade =
      std::clamp((Tuning::Player::MARKER_MAX_PIXELS - pixelHeight) /
                     (Tuning::Player::MARKER_MAX_PIXELS * 0.5f),
                 0.0f, 1.0f);
  constexpr float radius = Tuning::Player::MARKER_RADIUS;
  const ImU32 marker = kitColorDistance(kit.shirt, Tuning::Grass::PITCH_COLOR) <
                               KIT_CLASH_DISTANCE
                           ? kit.trim
                           : kit.shirt;
  state.drawGroundEllipse(
      root, {radius, 0.0f, 0.0f}, {0.0f, radius, 0.0f},
      withAlpha(marker,
                static_cast<std::uint8_t>(
                    static_cast<float>(Tuning::Player::MARKER_ALPHA) * fade)));
}

template <class RendererState>
void drawDayPlayerShadow(RendererState& state, Vec3 root, float standing,
                         Vec3 sunAway, float sunLength)
{
  using Sh = Tuning::Shadow;
  if (state.geometry.inStandShadow(root)) return;
  const float bladeLength = standing * sunLength;
  const Vec3 across{-sunAway.y, sunAway.x, 0.0f};
  state.drawSoftEllipse(root + sunAway * (bladeLength * 0.45f),
                        sunAway * (bladeLength * 0.55f + Sh::PENUMBRA),
                        across * (Sh::BLADE_HALF_WIDTH + Sh::PENUMBRA),
                        IM_COL32(0, 0, 0, Sh::SUN_ALPHA));
}

template <class RendererState>
void drawFloodlightPlayerShadows(RendererState& state, Vec3 root,
                                 float pixelHeight, float standing,
                                 std::span<const Vec3> lights)
{
  using F = Tuning::Floodlight;
  using Sh = Tuning::Shadow;
  const float close =
      std::clamp((pixelHeight - Sh::CLOSE_FADE_START_PIXELS) /
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
    state.drawSoftEllipse(root + away * (bladeLength * 0.45f),
                          away * (bladeLength * 0.55f + Sh::PENUMBRA),
                          across * (Sh::BLADE_HALF_WIDTH + Sh::PENUMBRA),
                          IM_COL32(0, 0, 0, darkness));
  }
}

template <class RendererState>
void drawPossessionRing(RendererState& state, Vec3 root)
{
  using Sh = Tuning::Shadow;
  state.releaseBatch();
  const float radius =
      Sh::RING_RADIUS *
      (1.0f + (state.reducedMotion
                   ? 0.0f
                   : Sh::RING_PULSE * std::sin(state.elapsedSeconds *
                                               Sh::RING_PULSE_SPEED)));
  std::array<ImVec2, Sh::RING_SEGMENTS> ring{};
  for (std::size_t index = 0; index < ring.size(); ++index)
  {
    const float angle =
        TWO_PI * static_cast<float>(index) / static_cast<float>(ring.size());
    ScreenPoint point;
    if (!state.projection.project(root + Vec3{std::cos(angle) * radius,
                                              std::sin(angle) * radius, 0.0f},
                                  point))
      return;
    ring[index] = toImVec(point);
  }
  state.drawList->AddPolyline(
      ring.data(), static_cast<int>(ring.size()), Sh::RING_COLOR,
      ImDrawFlags_Closed, Sh::RING_THICKNESS * ImGui::GetStyle().FontScaleDpi);
}

template <class RendererState>
void drawPlayerShadow(RendererState& state, const MatchRenderPlayer& player,
                      Vec3 root, float pixelHeight, float standing,
                      Vec3 sunAway, float sunLength,
                      std::span<const Vec3> lights)
{
  using Sh = Tuning::Shadow;
  drawPlayerMarker(state, player, root, pixelHeight);
  if (state.day)
    drawDayPlayerShadow(state, root, standing, sunAway, sunLength);
  else
    drawFloodlightPlayerShadows(state, root, pixelHeight, standing, lights);
  state.drawSoftEllipse(root, {Sh::CONTACT_RADIUS, 0.0f, 0.0f},
                        {0.0f, Sh::CONTACT_RADIUS, 0.0f}, Sh::CONTACT_COLOR);
  if (player.possessesBall) drawPossessionRing(state, root);
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
    const Vec3 root = RenderMath::worldFromPitch(lerpRenderPosition(
        player.previousPosition, player.currentPosition, alpha));
    ScreenPoint screen;
    if (!projection.project(root, screen)) continue;
    const float pixelHeight =
        playerHeightMetres(player) * projection.focalPixels / screen.depth;
    const float standing = playerHeightMetres(player);
    drawPlayerShadow(*this, player, root, pixelHeight, standing, sunAway,
                     sunLength, lights);
  }

  // A ball held in the hands sits in the holder's own shadow.
  if (heldBy)
  {
    releaseBatch();
    return;
  }
  const Vector2F ball = lerpRenderPosition(
      snapshot.ball.previousPosition, snapshot.ball.currentPosition, alpha);
  const float height = ballHeightMetres(snapshot.ball, alpha);
  const float radius = Tuning::Ball::SHADOW_RADIUS *
                       (1.0f + height * Tuning::Ball::SHADOW_GROWTH);
  const auto shadowAlpha = static_cast<std::uint8_t>(
      110.0f / (1.0f + height * Tuning::Ball::SHADOW_FADE));
  // By day the lifted ball's shadow slides away from the sun.
  const Vec3 slide = day ? sunAway * (height * sunLength) : Vec3{};
  drawSoftEllipse(RenderMath::worldFromPitch(ball) + slide,
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
  cheeringMask = std::byte{};
  if (goal.active && crowdCelebration > 0.0f)
  {
    cheeringMask = goal.byHome ? Stadium3D::CrowdFlags::CHEERS_HOME
                               : Stadium3D::CrowdFlags::CHEERS_AWAY;
  }
}

template <class RendererState>
void drawDistantCrowdClump(RendererState& state,
                           const Stadium3D::CrowdClump& clump,
                           const CrowdFaceLayout& layout, Vec4 clumpClip,
                           float inverseDepth)
{
  using C = Tuning::Crowd;
  const float halfWidth =
      clump.halfWidth * state.projection.focalPixels * inverseDepth;
  if (halfWidth * 2.0f < C::MIN_CLUMP_PIXELS) return;
  const Vec4 topClip = state.projection.toClip(clump.top);
  if (topClip.w < state.projection.nearPlane) return;
  ScreenPoint base = state.projection.clipToScreen(clumpClip);
  ScreenPoint upper = state.projection.clipToScreen(topClip);
  const float topHalf =
      clump.halfWidth * state.projection.focalPixels / topClip.w;
  if (std::max(base.x, upper.x) + halfWidth < layout.left ||
      std::min(base.x, upper.x) - halfWidth > layout.right ||
      std::min(base.y, upper.y) > layout.bottom ||
      std::max(base.y, upper.y) < layout.top)
    return;
  const bool hopping =
      layout.cheering && (clump.flags & state.cheeringMask) != std::byte{};
  const float hop = (state.crowdRise[clump.phase] +
                     (hopping ? state.crowdHop[clump.phase] : 0.0f)) *
                    C::DOT_HEIGHT * inverseDepth;
  upper.x += layout.upX * hop;
  upper.y += layout.upY * hop;
  state.batchQuad({base.x - halfWidth, base.y}, {base.x + halfWidth, base.y},
                  {upper.x + topHalf, upper.y}, {upper.x - topHalf, upper.y},
                  shadeColor256(clump.color, 218U), clump.color);
}

template <class RendererState>
void drawNearCrowdClump(RendererState& state,
                        const Stadium3D::CrowdClump& clump,
                        const CrowdFaceLayout& layout, Vec4 clumpClip,
                        float seatPixels)
{
  using C = Tuning::Crowd;
  const ScreenPoint base = state.projection.clipToScreen(clumpClip);
  const float inverseDepth = 1.0f / clumpClip.w;
  const float reach = (clump.halfWidth + C::DOT_WIDTH) *
                      state.projection.focalPixels * inverseDepth;
  if (const float rise = (C::DOT_HEIGHT * 2.0f +
                          C::ROW_DEPTH * static_cast<float>(C::CLUMP_ROWS)) *
                         state.projection.focalPixels * inverseDepth;
      base.x + reach < layout.left || base.x - reach > layout.right ||
      base.y - rise > layout.bottom || base.y + rise < layout.top)
    return;

  const auto detail = static_cast<std::uint32_t>(
      256.0f * std::clamp((seatPixels - C::CLUMP_PIXELS) /
                              (C::FULL_DETAIL_PIXELS - C::CLUMP_PIXELS),
                          0.0f, 1.0f));
  for (std::uint32_t index = clump.dotBegin; index < clump.dotEnd; ++index)
    drawCrowdDot(state, layout, clump, state.geometry.crowd[index], detail);
}

template <class RendererState>
void drawCrowdClump(RendererState& state, const Stadium3D::CrowdClump& clump,
                    const CrowdFaceLayout& layout, float seatFocal)
{
  using C = Tuning::Crowd;
  const Vec4 clip = state.projection.toClip(clump.base);
  if (clip.w < state.projection.nearPlane) return;
  const float inverseDepth = 1.0f / clip.w;
  const float seatPixels = seatFocal * inverseDepth;
  if (seatPixels < C::CLUMP_PIXELS)
  {
    drawDistantCrowdClump(state, clump, layout, clip, inverseDepth);
    return;
  }
  drawNearCrowdClump(state, clump, layout, clip, seatPixels);
}

template <class RendererState>
void drawStadiumFace(RendererState& state, const Stadium3D::Face& face)
{
  if (RenderMath::dot(face.normal, state.projection.eye - face.corners[0]) <=
      0.0f)
    return;
  state.drawWorldPolygon(face.corners, face.colors, false);
  state.drawCrowd(face);
  if (face.glow == 0 || state.day) return;
  const Vec3 centre = (face.corners[0] + face.corners[2]) * 0.5f;
  ScreenPoint glow;
  if (!state.projection.project(centre, glow)) return;
  const float radius = Tuning::Floodlight::GLOW_RADIUS *
                       state.projection.focalPixels / glow.depth;
  state.drawList->AddCircleFilled(toImVec(glow), radius,
                                  withAlpha(face.glow, 34));
  state.drawList->AddCircleFilled(toImVec(glow), radius * 0.55f,
                                  withAlpha(face.glow, 70));
  state.drawList->AddCircleFilled(toImVec(glow), radius * 0.25f,
                                  withAlpha(face.glow, 200));
}

void MatchRenderer3D::State::drawCrowd(const Stadium3D::Face& face)
{
  using C = Tuning::Crowd;
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
  const bool cheering = cheeringMask != std::byte{};
  const CrowdFaceLayout layout{upX,  upY,   acrossX, acrossY, halfDotFocal,
                               left, right, top,     bottom,  cheering};

  for (std::uint32_t clumpIndex = face.clumpBegin; clumpIndex < face.clumpEnd;
       ++clumpIndex)
    drawCrowdClump(*this, geometry.clumps[clumpIndex], layout, seatFocal);
  drawCrowdFlags(face, upX, upY);
  releaseBatch();
}

void MatchRenderer3D::State::drawCrowdFlags(const Stadium3D::Face& face,
                                            float upX, float upY)
{
  using C = Tuning::Crowd;
  const bool cheering = cheeringMask != std::byte{};
  for (std::uint32_t index = face.flagBegin; index < face.flagEnd; ++index)
  {
    const Stadium3D::CrowdFlag& flag = geometry.flags[index];
    const Vec4 clip = projection.toClip(flag.base);
    if (clip.w < projection.nearPlane) continue;
    const float inverseDepth = 1.0f / clip.w;
    if (C::FLAG_HEIGHT * projection.focalPixels * inverseDepth <
        C::FLAG_MIN_PIXELS)
      continue;
    const bool celebrating =
        cheering && (flag.flags & cheeringMask) != std::byte{};
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
    for (std::size_t corner = 0; corner < cloth.size(); ++corner)
    {
      if (!visible) break;
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
  using enum Stadium3D::Side;
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
        ((section.side == SOUTH && eye.y < -S::SIDE_FRONT) ||
         (section.side == NORTH && eye.y > WIDTH + S::SIDE_FRONT) ||
         (section.side == WEST && eye.x < -S::END_FRONT) ||
         (section.side == EAST && eye.x > LENGTH + S::END_FRONT)))
      continue;
    sectionOrder.emplace_back(projection.depth(section.centre), index);
  }
  std::ranges::sort(sectionOrder, [](const auto& a, const auto& b)
                    { return a.first > b.first; });

  for (const auto& [depth, sectionIndex] : sectionOrder)
  {
    const Stadium3D::Section& section = geometry.sections[sectionIndex];
    for (std::uint32_t faceIndex = section.faceBegin;
         faceIndex < section.faceEnd; ++faceIndex)
    {
      const Stadium3D::Face& face = geometry.faces[faceIndex];
      drawStadiumFace(*this, face);
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
  if (const std::span<ImVec2> written(points.data() + primitive.firstPoint,
                                      screen.size());
      signedArea(written) < 0.0f)
    std::ranges::reverse(written);
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
      const Vec3 root = RenderMath::worldFromPitch(lerpRenderPosition(
          player.previousPosition, player.currentPosition, alpha));
      if (PlayerRig::flatDistance(root, ballWorld) <
          Tuning::Rig::THROW_PICKUP_METRES)
        heldBy = &player;
    }
    return;
  }
}

struct GaitFrame
{
  float groundSpeed;
  float cycleMetres;
};

GaitFrame advanceGait(AnimationSlot& slot, const MatchRenderPlayer& player,
                      Vec3 root, float scale, float deltaSeconds)
{
  using P = Tuning::Player;
  using R = Tuning::Rig;
  Vec3 moved = root - slot.gait.lastPosition;
  moved.z = 0.0f;
  float distance = RenderMath::length(moved);
  if (distance > P::TELEPORT_METRES)
  {
    distance = 0.0f;
    slot.gait.feet[0].valid = false;
    slot.gait.feet[1].valid = false;
  }
  slot.gait.lastPosition = root;
  const float groundSpeed = player.speedMetresPerSecond;
  const float targetStride =
      groundSpeed < P::IDLE_SPEED
          ? 0.0f
          : std::clamp(groundSpeed / P::FULL_STRIDE_SPEED, 0.0f, 1.0f);
  slot.gait.stride += (targetStride - slot.gait.stride) *
                      RenderMath::dampingFactor(P::STRIDE_RATE, deltaSeconds);
  slot.gait.gaitSpeed +=
      ((groundSpeed < P::IDLE_SPEED ? 0.0f : groundSpeed) -
       slot.gait.gaitSpeed) *
      RenderMath::dampingFactor(Tuning::Gait::SPEED_RATE, deltaSeconds);
  const float cycleMetres =
      (P::STRIDE_BASE_METRES + P::STRIDE_PER_SPEED * groundSpeed) * scale;
  slot.gait.phase =
      std::fmod(slot.gait.phase + TWO_PI * std::min(distance / cycleMetres,
                                                    P::MAX_CYCLES_PER_FRAME),
                TWO_PI);
  if (distance > 1e-4f && groundSpeed >= P::IDLE_SPEED)
  {
    const Vec3 heading = moved * (1.0f / distance);
    slot.gait.moveDirection = RenderMath::normalize(RenderMath::lerp(
        slot.gait.moveDirection, heading,
        RenderMath::dampingFactor(R::DIRECTION_RATE, deltaSeconds)));
  }
  return {groundSpeed, cycleMetres};
}

void updateMotionRates(AnimationSlot& slot, float groundSpeed, float yaw,
                       float simSeconds)
{
  using Q = Tuning::Pose;
  if (simSeconds > 0.0f)
  {
    const float rawAcceleration = std::clamp(
        (groundSpeed - slot.gait.lastSpeed) / simSeconds, -12.0f, 12.0f);
    slot.gait.acceleration +=
        (rawAcceleration - slot.gait.acceleration) *
        RenderMath::dampingFactor(Q::ACCEL_SMOOTHING, simSeconds);
    const float rawTurn =
        slot.gait.hasYaw
            ? std::clamp(
                  RenderMath::wrapAngle(yaw - slot.gait.lastYaw) / simSeconds,
                  -Q::MAX_TURN_RATE, Q::MAX_TURN_RATE)
            : 0.0f;
    slot.gait.turnRate +=
        (rawTurn - slot.gait.turnRate) *
        RenderMath::dampingFactor(Q::TURN_SMOOTHING, simSeconds);
    if (slot.action.event != PlayerRig::Event::NONE)
    {
      slot.action.eventSeconds += simSeconds;
      if (slot.action.eventSeconds >= eventDuration(slot.action.event))
        slot.action.event = PlayerRig::Event::NONE;
    }
  }
  slot.gait.lastSpeed = groundSpeed;
  slot.gait.lastYaw = yaw;
  slot.gait.hasYaw = true;
}

template <class RendererState>
void updateKeeperPose(RendererState& state, const MatchRenderPlayer& player,
                      AnimationSlot& slot, Vec3 root, float yaw, float blend)
{
  using K = Tuning::Keeper;
  float setTarget = 0.0f;
  float holdTarget = 0.0f;
  float diveTarget = 0.0f;
  if (player.isGoalkeeper && player.onPitch)
  {
    const GoalkeeperState keeperState =
        player.isHomeTeam ? state.homeKeeperState : state.awayKeeperState;
    const Vec3 toBall = state.ballWorld - root;
    const float ballDistance = std::hypot(toBall.x, toBall.y);
    if (player.isDiving || keeperState == GoalkeeperState::DIVE)
    {
      if (slot.keeper.diveBlend < 0.05f)
      {
        const Vec3 side{-std::sin(yaw), std::cos(yaw), 0.0f};
        slot.keeper.diveSide =
            RenderMath::dot(toBall, side) >= 0.0f ? 1.0f : -1.0f;
      }
      diveTarget = 1.0f;
    }
    else if (player.possessesBall && keeperState == GoalkeeperState::HOLD)
      holdTarget = 1.0f;
    else if (state.livePlay && keeperState == GoalkeeperState::SET_POSITION &&
             ballDistance < K::SET_DISTANCE)
      setTarget = 1.0f;
  }
  slot.keeper.setBlend += (setTarget - slot.keeper.setBlend) * blend;
  slot.keeper.holdBlend += (holdTarget - slot.keeper.holdBlend) * blend;
  slot.keeper.diveBlend += (diveTarget - slot.keeper.diveBlend) * blend;
}

void updateFootPlanting(AnimationSlot& slot, Vec3 root, float yaw, float scale,
                        float cycleMetres, float groundSpeed, float simSeconds)
{
  using P = Tuning::Player;
  using R = Tuning::Rig;
  using K = Tuning::Keeper;
  const Vec3 flatSide{-std::sin(yaw), std::cos(yaw), 0.0f};
  const float duty = PlayerRig::dutyFactor(slot.gait.gaitSpeed);
  const bool idle =
      groundSpeed < P::IDLE_SPEED && slot.gait.gaitSpeed < R::IDLE_GAIT_SPEED;
  const float stance =
      R::STANCE_WIDTH + slot.keeper.setBlend * K::SET_FOOT_SPREAD;
  for (std::size_t leg = 0; leg < 2; ++leg)
  {
    PlayerRig::StrideInput stride;
    stride.rest =
        root + flatSide * ((leg == 0 ? 1.0f : -1.0f) * stance * scale);
    stride.rest.z = 0.0f;
    stride.forward = slot.gait.moveDirection;
    stride.yaw = yaw;
    stride.legPhase = std::fmod(
        slot.gait.phase + (leg == 0 ? 0.0f : std::numbers::pi_v<float>),
        TWO_PI);
    stride.duty = duty;
    stride.stanceLength = duty * cycleMetres;
    stride.frontReach = R::FRONT_REACH * scale;
    stride.liftHeight =
        (R::WALK_LIFT + (R::SPRINT_LIFT - R::WALK_LIFT) * slot.gait.stride) *
        scale;
    stride.maxDrift = R::MAX_DRIFT * scale;
    stride.idle = idle;
    stride.idleStepMetres = R::IDLE_STEP_METRES * scale;
    stride.stepDuration = R::IDLE_STEP_SECONDS;
    stride.otherStepping = slot.gait.feet[1 - leg].stepSeconds > 0.0f;
    stride.deltaSeconds = simSeconds;
    PlayerRig::stepFoot(slot.gait.feet[leg], stride);
  }
  slot.gait.stanceLength = duty * cycleMetres;
  slot.gait.duty = duty;
}

void MatchRenderer3D::State::updateMotion(const MatchRenderPlayer& player,
                                          AnimationSlot& slot, float alpha,
                                          float deltaSeconds)
{
  using P = Tuning::Player;
  using R = Tuning::Rig;
  const Vec3 root = RenderMath::worldFromPitch(lerpRenderPosition(
      player.previousPosition, player.currentPosition, alpha));
  const float yaw = RenderMath::worldYawFromFacing(RenderMath::lerpAngle(
      player.previousFacingAngle, player.currentFacingAngle, alpha));
  if (slot.player != player.player)
    initializeAnimationSlot(*this, player, slot, root, yaw);
  const float scale =
      P::SCALE * playerHeightMetres(player) / P::REFERENCE_HEIGHT_METRES;
  const GaitFrame gait = advanceGait(slot, player, root, scale, deltaSeconds);
  updateMotionRates(slot, gait.groundSpeed, yaw, simSeconds);

  // Rise to meet a high ball dropping in nearby (the header itself is the
  // touch event); keepers reach with their arms instead.
  const float jumpTarget = jumpTargetFor(*this, player, root, scale);
  if (simSeconds > 0.0f)
  {
    slot.gait.jump += (jumpTarget - slot.gait.jump) *
                      RenderMath::dampingFactor(R::JUMP_RATE, simSeconds);
  }

  const float blend =
      RenderMath::dampingFactor(Tuning::Keeper::DIVE_RATE, deltaSeconds);
  updateKeeperPose(*this, player, slot, root, yaw, blend);
  updateFootPlanting(slot, root, yaw, scale, gait.cycleMetres, gait.groundSpeed,
                     simSeconds);
}

int MatchRenderer3D::State::shirtNumberFor(const MatchRenderPlayer& player)
{
  // Shared with the 2D view, so both show the same numbers.
  return numbers.numberFor(player, playerStats);
}

GoalMood MatchRenderer3D::State::goalMoodFor(
    const MatchRenderPlayer& player) const
{
  using enum GoalMood;
  if (!goal.active || !player.onPitch) return NONE;
  if (player.isHomeTeam != goal.byHome) return DEJECTED;
  if (!goal.ownGoal && player.player && player.player->getId() == goal.scorer)
    return SCORER;
  return CELEBRATING;
}

Pose poseForTimedEvent(Pose pose, PlayerRig::Event event, float eventTime,
                       float eventWeight, std::size_t eventLeg)
{
  using R = Tuning::Rig;
  using enum PlayerRig::Event;
  switch (event)
  {
    case PASS:
    case SHOT:
    case CROSS:
    {
      using Kk = Tuning::Kick;
      Pose kick = pose;
      kick.lean = event == PASS ? Kk::PASS_LEAN : Kk::TORSO_LEAN;
      for (std::size_t index = 0; index < 2; ++index)
      {
        const float outward = index == 0 ? 1.0f : -1.0f;
        kick.armSwing[index] = index == eventLeg ? -0.25f : 0.5f;
        kick.armSpread[index] = outward * Kk::ARM_ABDUCTION;
        kick.elbow[index] = 0.4f;
      }
      return mixPose(pose, kick, eventWeight);
    }
    case HEADER:
    {
      using Kk = Tuning::Kick;
      Pose header = pose;
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
      return mixPose(pose, header, eventWeight);
    }
    case TACKLE:
    {
      Pose tackle = pose;
      tackle.lean = R::TACKLE_LEAN;
      tackle.hipDrop = R::TACKLE_HIP_DROP;
      for (std::size_t index = 0; index < 2; ++index)
      {
        const float outward = index == 0 ? 1.0f : -1.0f;
        tackle.armSwing[index] = index == eventLeg ? -0.3f : 0.35f;
        tackle.armSpread[index] = outward * 0.7f;
        tackle.elbow[index] = 0.5f;
      }
      return mixPose(pose, tackle, eventWeight);
    }
    case SLIDE:
    {
      using P = Tuning::Player;
      Pose slide = pose;
      slide.lean = R::SLIDE_LEAN;
      slide.hipDrop = P::HIP_HEIGHT - R::SLIDE_PELVIS;
      slide.lift = 0.0f;
      for (std::size_t index = 0; index < 2; ++index)
      {
        const float outward = index == 0 ? 1.0f : -1.0f;
        slide.armSwing[index] = -0.7f;
        slide.armSpread[index] = outward * 0.65f;
        slide.elbow[index] = 0.2f;
      }
      return mixPose(pose, slide, eventWeight);
    }
    case THROW:
    case NONE:
      return pose;
  }
  return pose;
}

Pose poseForKeeper(Pose pose, bool keeper, float setBlend, float diveBlend,
                   float diveSide, float stride)
{
  if (!keeper) return pose;
  using K = Tuning::Keeper;
  if (setBlend > 0.01f)
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
    pose = mixPose(pose, set, setBlend * (1.0f - stride));
  }
  if (diveBlend > 0.01f)
  {
    Pose dive = pose;
    dive.bodyRoll = diveSide * K::DIVE_ROLL;
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
    pose = mixPose(pose, dive, diveBlend);
  }
  return pose;
}

Pose poseForCelebration(Pose pose, GoalMood mood, bool reducedMotion,
                        float elapsedSeconds, std::uint32_t seed, float stride,
                        float celebrationWeight)
{
  if (mood == GoalMood::NONE) return pose;
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
        reducedMotion ? 0.0f
                      : C::HOP * (1.0f - stride) *
                            std::abs(std::sin(elapsedSeconds * C::HOP_SPEED +
                                              static_cast<float>(seed % 7U)));
    react.lift = std::max(react.lift, hop);
    react.lean = std::min(react.lean, 0.1f);
    for (std::size_t index = 0; index < 2; ++index)
    {
      const float outward = index == 0 ? 1.0f : -1.0f;
      if (mood == GoalMood::SCORER)
      {
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
  return mixPose(pose, react, celebrationWeight);
}

void applyEventFootTargets(std::array<Vec3, 2>& ankleTargets,
                           std::array<bool, 2>& planted,
                           const AnimationSlot& slot, Vec3 pelvis,
                           float ankleHeight, float scale, float eventTime,
                           float eventWeight)
{
  using R = Tuning::Rig;
  using enum PlayerRig::Event;
  if (eventWeight <= 0.0f) return;
  const std::size_t leg = slot.action.eventLeg;
  const float outward = leg == 0 ? 1.0f : -1.0f;
  Vec3 target = ankleTargets[leg];
  float weight = eventWeight;
  const Vec3 eventSide{-slot.action.eventDirection.y,
                       slot.action.eventDirection.x, 0.0f};
  switch (slot.action.event)
  {
    case PASS:
    case SHOT:
    case CROSS:
    {
      using Kk = Tuning::Kick;
      const Vec3 contact =
          slot.action.contact -
          slot.action.eventDirection * (Kk::CONTACT_BEHIND * scale) +
          UP * (Kk::CONTACT_ABOVE * scale);
      target = PlayerRig::kickAnkle(slot.action.event, eventTime, contact,
                                    slot.action.eventDirection,
                                    eventSide * -outward, kickTiming(), weight);
      break;
    }
    case TACKLE:
      target = Vec3{slot.action.contact.x, slot.action.contact.y, ankleHeight};
      break;
    case SLIDE:
    {
      const Vec3 ground{pelvis.x, pelvis.y, 0.0f};
      target = ground +
               slot.action.eventDirection * (R::SLIDE_LEG_REACH * scale) +
               UP * ankleHeight;
      const std::size_t other = 1 - leg;
      const Vec3 tucked =
          ground + slot.action.eventDirection * (R::SLIDE_TUCK_REACH * scale) +
          eventSide * (outward * -0.18f * scale) + UP * ankleHeight;
      ankleTargets[other] =
          RenderMath::lerp(ankleTargets[other], tucked, eventWeight);
      planted[other] = false;
      break;
    }
    case HEADER:
    case THROW:
    case NONE:
      weight = 0.0f;
      break;
  }
  if (weight <= 0.0f) return;
  ankleTargets[leg] = RenderMath::lerp(ankleTargets[leg], target, weight);
  planted[leg] = false;
}

template <class RendererState>
void drawPlayerLeg(RendererState& state, const AnimationSlot& slot,
                   std::size_t index, const std::array<Vec3, 2>& hipJoint,
                   const std::array<Vec3, 2>& knee,
                   const std::array<Vec3, 2>& ankle,
                   const std::array<float, 2>& heel,
                   const std::array<bool, 2>& planted, Vec3 bodySide,
                   float scale, float bulk, float objectDepth, PlayerLod lod,
                   const KitColors& kit, const PlayerLook& look, ImU32 skin)
{
  using P = Tuning::Player;
  using R = Tuning::Rig;
  const float footYaw = slot.gait.feet[index].yaw;
  Vec3 footForward{std::cos(footYaw), std::sin(footYaw), 0.0f};
  if (!planted[index])
    footForward = RenderMath::normalize(footForward - UP * R::SWING_TOE_DROP);
  else if (heel[index] > 0.0f)
    footForward = RenderMath::normalize(footForward * std::cos(heel[index]) -
                                        UP * std::sin(heel[index]));
  const Vec3 hem =
      RenderMath::lerp(hipJoint[index], knee[index], P::SHORTS_LEG_SHARE);
  const Vec3 shinHint = bodySide;
  if (lod == PlayerLod::FAR)
  {
    state.addSegment(hipJoint[index], knee[index], P::THIGH_TOP * bulk,
                     P::THIGH_BOTTOM * bulk, kit.shorts,
                     {shinHint, objectDepth, lod});
    state.addSegment(knee[index], ankle[index], P::CALF_TOP * bulk,
                     P::ANKLE_HALF_WIDTH * bulk, kit.socks,
                     {shinHint, objectDepth, lod});
    return;
  }
  state.addSegment(hipJoint[index], hem, P::SHORTS_LEG_TOP * bulk,
                   P::SHORTS_LEG_HEM * bulk, kit.shorts,
                   {shinHint, objectDepth, lod});
  state.addSegment(hem, knee[index], P::THIGH_MIDDLE * bulk,
                   P::THIGH_BOTTOM * bulk, skin, {shinHint, objectDepth, lod});
  const Vec3 band =
      RenderMath::lerp(knee[index], ankle[index], P::SOCK_BAND_SHARE);
  if (lod == PlayerLod::NEAR)
  {
    state.addSegment(knee[index], band, P::CALF_TOP * bulk * 1.06f,
                     P::CALF_TOP * bulk * 1.08f, kit.trim,
                     {shinHint, objectDepth, lod});
    state.addSegment(band, ankle[index], P::CALF_TOP * bulk,
                     P::ANKLE_HALF_WIDTH * bulk, kit.socks,
                     {shinHint, objectDepth, lod});
  }
  else
    state.addSegment(knee[index], ankle[index], P::CALF_TOP * bulk,
                     P::ANKLE_HALF_WIDTH * bulk, kit.socks,
                     {shinHint, objectDepth, lod});

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
    const float width = (toe ? P::BOOT_TOE_WIDTH : P::BOOT_HALF_WIDTH) * scale;
    float height = -P::BOOT_HALF_HEIGHT;
    if ((corner & 4U) != 0U)
      height = toe ? P::BOOT_TOE_HEIGHT : P::BOOT_HALF_HEIGHT;
    height *= scale;
    boot[corner] =
        bootCentre +
        footForward * ((toe ? 1.0f : -1.0f) * P::BOOT_HALF_LENGTH * scale) +
        footSide * (((corner & 2U) != 0U ? 1.0f : -1.0f) * width) +
        footUp * height;
  }
  state.addBox(boot, look.boots, objectDepth);
  if (lod != PlayerLod::NEAR) return;
  std::array<Vec3, 8> sole{};
  for (std::size_t corner = 0; corner < 8; ++corner)
  {
    sole[corner] =
        boot[corner & 3U] -
        footUp *
            ((corner & 4U) != 0U ? 0.0f : P::SOLE_HALF_HEIGHT * 2.0f * scale);
  }
  state.addBox(sole, P::SOLE_COLOR, objectDepth);
}

template <class RendererState>
void drawPlayerLegs(RendererState& state, const AnimationSlot& slot,
                    const std::array<Vec3, 2>& hipJoint,
                    const std::array<Vec3, 2>& knee,
                    const std::array<Vec3, 2>& ankle,
                    const std::array<float, 2>& heel,
                    const std::array<bool, 2>& planted, Vec3 bodySide,
                    float scale, float bulk, float objectDepth, PlayerLod lod,
                    const KitColors& kit, const PlayerLook& look, ImU32 skin)
{
  for (std::size_t index = 0; index < 2; ++index)
    drawPlayerLeg(state, slot, index, hipJoint, knee, ankle, heel, planted,
                  bodySide, scale, bulk, objectDepth, lod, kit, look, skin);
}

float fitPelvisToPlantedFeet(float pelvisHeight, const AnimationSlot& slot,
                             Vec3 root, Vec3 flatSide, Vec3 forward,
                             float scale, float poseLift)
{
  using P = Tuning::Player;
  using R = Tuning::Rig;
  if (poseLift > 0.01f || slot.keeper.diveBlend >= 0.05f) return pelvisHeight;
  const float lowest = pelvisHeight - R::MAX_PELVIS_DROP * scale;
  const float reach = (P::THIGH_LENGTH + P::SHIN_LENGTH) * scale * 0.995f;
  const float ankleHeight = R::ANKLE_HEIGHT * scale;
  for (std::size_t index = 0; index < 2; ++index)
  {
    const PlayerRig::FootState& foot = slot.gait.feet[index];
    if (!foot.inStance) continue;
    const Vec3 hip =
        root + flatSide * ((index == 0 ? 1.0f : -1.0f) * P::HIP_SPREAD * scale);
    if (const float ahead = RenderMath::dot(foot.position - hip, forward);
        ahead <= 0.0f)
      continue;
    const float flat = PlayerRig::flatDistance(foot.position, hip);
    const float highest =
        ankleHeight + std::sqrt(std::max(reach * reach - flat * flat, 0.0f));
    pelvisHeight = std::max(lowest, std::min(pelvisHeight, highest));
  }
  return pelvisHeight;
}

void buildPlayerAnkleTargets(const AnimationSlot& slot, const Pose& pose,
                             Vec3 root, Vec3 pelvis, Vec3 bodyUp, Vec3 bodySide,
                             Vec3 forward, float scale, float ankleHeight,
                             std::array<Vec3, 2>& hipJoint,
                             std::array<Vec3, 2>& ankleTarget,
                             std::array<bool, 2>& planted)
{
  using P = Tuning::Player;
  using R = Tuning::Rig;
  const BodyFrame legFrame{bodyUp, forward, bodySide};
  for (std::size_t index = 0; index < 2; ++index)
  {
    const float sideSign = index == 0 ? 1.0f : -1.0f;
    hipJoint[index] = pelvis + bodySide * (sideSign * P::HIP_SPREAD * scale);
    const PlayerRig::FootState& foot = slot.gait.feet[index];
    Vec3 target = foot.position + UP * ankleHeight;
    planted[index] = foot.inStance && pose.lift <= 0.01f;
    if (pose.lift > 0.0f)
    {
      target = target + UP * (pose.lift * scale * R::JUMP_TUCK) +
               (root - foot.position) *
                   (R::JUMP_GATHER * std::min(pose.lift * 4.0f, 1.0f));
      target.z = std::max(target.z, foot.position.z + ankleHeight);
    }
    if (slot.keeper.diveBlend > 0.01f)
    {
      const Vec3 thighDirection =
          limbDirection(legFrame, pose.thigh[index], pose.legSpread[index]);
      const Vec3 shinDirection =
          limbDirection(legFrame, pose.thigh[index] + pose.knee[index],
                        pose.legSpread[index]);
      const Vec3 fk = hipJoint[index] +
                      thighDirection * (P::THIGH_LENGTH * scale) +
                      shinDirection * (P::SHIN_LENGTH * scale);
      target = RenderMath::lerp(target, fk, slot.keeper.diveBlend);
      planted[index] = planted[index] && slot.keeper.diveBlend < 0.5f;
    }
    ankleTarget[index] = target;
  }
}

void solvePlayerLegJoints(const AnimationSlot& slot,
                          const std::array<Vec3, 2>& hipJoint,
                          const std::array<Vec3, 2>& ankleTarget,
                          const std::array<bool, 2>& planted, Vec3 forward,
                          Vec3 bodyUp, float scale, std::array<Vec3, 2>& knee,
                          std::array<Vec3, 2>& ankle,
                          std::array<float, 2>& heel)
{
  using P = Tuning::Player;
  using R = Tuning::Rig;
  const Vec3 kneePole = forward + bodyUp * 0.15f;
  for (std::size_t index = 0; index < 2; ++index)
  {
    Vec3 target = ankleTarget[index];
    const float footYaw = slot.gait.feet[index].yaw;
    const Vec3 footForward{std::cos(footYaw), std::sin(footYaw), 0.0f};
    if (planted[index])
      heel[index] = PlayerRig::rollOntoToes(
          target, hipJoint[index], footForward, R::TOE_LENGTH * scale,
          (P::THIGH_LENGTH + P::SHIN_LENGTH) * scale * 0.995f,
          R::MAX_HEEL_ANGLE);
    const PlayerRig::TwoBone leg = PlayerRig::solveTwoBone(
        hipJoint[index], target, P::THIGH_LENGTH * scale,
        P::SHIN_LENGTH * scale, kneePole);
    knee[index] = leg.middle;
    ankle[index] = leg.end;
  }
}

struct PlayerTorsoFrame
{
  Vec3 up;
  Vec3 forward;
  Vec3 side;
  Vec3 hips;
  Vec3 waist;
  Vec3 chest;
};

template <class RendererState>
PlayerTorsoFrame drawPlayerTorso(RendererState& state,
                                 const AnimationSlot& slot, const Pose& pose,
                                 Vec3 pelvis, Vec3 bodyUp, Vec3 bodySide,
                                 Vec3 forward, float scale, float bulk,
                                 float objectDepth, PlayerLod lod,
                                 bool detailed, std::size_t kitIndex,
                                 const KitColors& kit)
{
  using P = Tuning::Player;
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
    state.addSegment(hips - bodyUp * (P::SHORTS_HALF_HEIGHT * scale), waist,
                     P::SHORTS_HALF_WIDTH * bulk, P::SHORTS_HALF_WIDTH * bulk,
                     kit.shorts, {forward, objectDepth, lod});
  else
    state.orientedBox(hips, forward * (P::SHORTS_HALF_DEPTH * bulk),
                      bodySide * (P::SHORTS_HALF_WIDTH * bulk),
                      bodyUp * (P::SHORTS_HALF_HEIGHT * scale), kit.shorts,
                      objectDepth);
  if (lod == PlayerLod::NEAR)
  {
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
    state.addPrism(lower, middle, kit.shirt, objectDepth, false, false);
    state.addPrism(middle, upper, kit.shirt, objectDepth, false, true);
  }
  else
  {
    std::array<Vec3, 8> torso{};
    for (std::size_t corner = 0; corner < 8; ++corner)
    {
      const bool upperRing = (corner & 4U) != 0U;
      const float depth =
          (upperRing ? P::CHEST_HALF_DEPTH : P::WAIST_HALF_DEPTH) * bulk;
      const float width =
          (upperRing ? P::CHEST_HALF_WIDTH : P::WAIST_HALF_WIDTH) * bulk;
      torso[corner] = (upperRing ? chest : waist) +
                      torsoForward * ((corner & 1U) != 0U ? depth : -depth) +
                      torsoSide * ((corner & 2U) != 0U ? width : -width);
    }
    state.addBox(torso, kit.shirt, objectDepth);
  }
  if (detailed)
    state.orientedBox(chest, torsoForward * (P::COLLAR_HALF_SIZE * scale),
                      torsoSide * (P::COLLAR_HALF_SIZE * scale),
                      torsoUp * (P::COLLAR_HALF_HEIGHT * scale), kit.trim,
                      objectDepth);
  if (lod != PlayerLod::FAR)
    state.addBackPrint(slot, kitIndex, waist,
                       {torsoUp, torsoForward, torsoSide}, scale, objectDepth);
  return {torsoUp, torsoForward, torsoSide, hips, waist, chest};
}

void MatchRenderer3D::State::addSegment(Vec3 from, Vec3 to, float fromHalf,
                                        float toHalf, ImU32 color,
                                        SegmentStyle style)
{
  using P = Tuning::Player;
  const Vec3 span = to - from;
  const float spanLength = RenderMath::length(span);
  if (spanLength < 1e-4f) return;
  const Vec3 along = span * (1.0f / spanLength);
  if (style.lod == PlayerLod::FAR)
  {
    addLine(from, to, color, fromHalf + toHalf, style.objectDepth);
    return;
  }
  Vec3 acrossA = RenderMath::cross(along, style.hint);
  if (RenderMath::dot(acrossA, acrossA) < 0.04f)
    acrossA = RenderMath::cross(along, UP);
  if (RenderMath::dot(acrossA, acrossA) < 0.04f)
    acrossA = RenderMath::cross(along, Vec3{1.0f, 0.0f, 0.0f});
  acrossA = RenderMath::normalize(acrossA);
  const Vec3 acrossB = RenderMath::cross(along, acrossA);
  if (style.lod == PlayerLod::NEAR)
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
    addPrism(bottom, top, color, style.objectDepth, true, true);
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
  addBox(corners, color, style.objectDepth);
}

void MatchRenderer3D::State::addPlayer(const MatchRenderPlayer& player,
                                       AnimationSlot& slot, float alpha)
{
  using P = Tuning::Player;
  using Q = Tuning::Pose;
  using R = Tuning::Rig;
  using enum PlayerRig::Event;
  const Vec3 root = RenderMath::worldFromPitch(lerpRenderPosition(
      player.previousPosition, player.currentPosition, alpha));
  ScreenPoint anchor;
  if (!projection.project(root + UP * P::HIP_HEIGHT, anchor)) return;
  if (const RenderMath::ScreenRect& rect = projection.rect;
      anchor.x < rect.x - P::CULL_MARGIN_PIXELS ||
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
  PlayerLod lod = PlayerLod::NEAR;
  if (pixelHeight < P::ROUND_MIN_PIXELS)
    lod = pixelHeight < R::FAR_PIXELS ? PlayerLod::FAR : PlayerLod::MID;
  const bool detailed = pixelHeight >= P::HAND_MIN_PIXELS;
  // Broad-shouldered or slight: widths vary a little from player to player.
  const float bulk = scale * look.bulk;

  const float yaw = RenderMath::worldYawFromFacing(RenderMath::lerpAngle(
      player.previousFacingAngle, player.currentFacingAngle, alpha));
  const Vec3 forward{std::cos(yaw), std::sin(yaw), 0.0f};
  const Vec3 flatSide{-forward.y, forward.x, 0.0f};
  const float stride = slot.gait.stride;
  const GoalMood mood = goalMoodFor(player);
  const bool celebrating = mood != GoalMood::NONE && celebrationWeight > 0.0f;

  // --- action -------------------------------------------------------------
  PlayerRig::ActionInput actionInput;
  actionInput.speed = slot.gait.gaitSpeed;
  actionInput.turnRate = slot.gait.turnRate;
  actionInput.event = slot.action.event;
  actionInput.goalkeeper = keeper;
  actionInput.diving = slot.keeper.diveBlend > 0.5f;
  actionInput.holding = slot.keeper.holdBlend > 0.5f;
  actionInput.keeperSet = slot.keeper.setBlend > 0.5f;
  actionInput.throwIn = heldBy == &player && !keeper;
  if (!celebrating)
    actionInput.mood = PlayerRig::Mood::NONE;
  else if (mood == GoalMood::DEJECTED)
    actionInput.mood = PlayerRig::Mood::DEJECTED;
  else
    actionInput.mood = PlayerRig::Mood::CELEBRATE;
  const PlayerRig::Action action = PlayerRig::selectAction(actionInput);

  // --- upper-body pose ------------------------------------------------------
  // Walk, jog and sprint blend with the smoothed speed; below a walk the
  // swing fades into the idle stance. Arms swing against the planted legs.
  const Tuning::Gait::Shape gait = gaitShape(slot.gait.gaitSpeed);
  const float motion =
      std::clamp(slot.gait.gaitSpeed / Tuning::Gait::WALK.speed, 0.0f, 1.0f);
  const float halfStance = std::max(slot.gait.stanceLength * 0.5f, 0.1f);
  Pose pose;
  for (std::size_t index = 0; index < 2; ++index)
  {
    const float outward = index == 0 ? 1.0f : -1.0f;
    const float legForward = std::clamp(
        RenderMath::dot(slot.gait.feet[index].position - root, forward) /
            halfStance,
        -1.0f, 1.0f);
    pose.armSwing[index] = -motion * gait.arm * legForward;
    pose.armSpread[index] = outward * 0.08f;
    pose.elbow[index] = P::ELBOW_BEND + (gait.elbow - P::ELBOW_BEND) * motion;
  }
  // Lean into acceleration, sit back when braking, bank into turns.
  const float accelerationLean =
      std::clamp(slot.gait.acceleration * Q::ACCEL_LEAN, -Q::MAX_BRAKE_LEAN,
                 Q::MAX_ACCEL_LEAN);
  pose.lean = motion * gait.lean + accelerationLean;
  pose.turnRoll = std::clamp(
      slot.gait.turnRate * player.speedMetresPerSecond * Q::TURN_LEAN,
      -Q::MAX_TURN_LEAN, Q::MAX_TURN_LEAN);
  // Sharp turns lead with the shoulders.
  pose.twist = std::clamp(slot.gait.turnRate * Q::TWIST_PER_TURN_RATE,
                          -Q::MAX_TWIST, Q::MAX_TWIST);
  const float stop = std::clamp(
      (-slot.gait.acceleration - Q::STOP_DECELERATION) / Q::STOP_DECELERATION,
      0.0f, 1.0f);
  pose.hipDrop += stop * Q::STOP_HIP_DROP;
  pose.lift = slot.gait.jump;

  // Timed ball actions: weight of the curve that owns the acting leg.
  const float eventTime = slot.action.eventSeconds;
  const float eventWeight = eventEnvelope(slot.action.event, eventTime);
  pose = poseForTimedEvent(pose, slot.action.event, eventTime, eventWeight,
                           slot.action.eventLeg);
  pose = poseForKeeper(pose, keeper, slot.keeper.setBlend,
                       slot.keeper.diveBlend, slot.keeper.diveSide, stride);
  if (celebrating)
    pose = poseForCelebration(pose, mood, reducedMotion, elapsedSeconds,
                              look.seed, stride, celebrationWeight);

  // --- skeleton -----------------------------------------------------------
  // Running drops the pelvis while a foot is down and floats it in flight;
  // walking vaults over the stance leg.
  const float bobSign = slot.gait.duty > 0.5f ? 1.0f : -1.0f;
  const float bob =
      bobSign * motion * gait.bob *
      std::cos(2.0f *
               (slot.gait.phase - std::numbers::pi_v<float> * slot.gait.duty));
  const float crouch = R::RUN_CROUCH * slot.gait.stride;
  const float roll = pose.turnRoll + pose.bodyRoll;
  const Vec3 bodyUp = UP * std::cos(roll) + flatSide * std::sin(roll);
  const Vec3 bodySide = flatSide * std::cos(roll) - UP * std::sin(roll);
  // Turns bank from the boots; dives pivot around the hips.
  const Vec3 hipUp =
      UP * std::cos(pose.bodyRoll) + flatSide * std::sin(pose.bodyRoll);
  float pelvisHeight =
      (P::HIP_HEIGHT - pose.hipDrop - crouch + bob + pose.lift) * scale;
  const float ankleHeight = R::ANKLE_HEIGHT * scale;
  pelvisHeight = fitPelvisToPlantedFeet(pelvisHeight, slot, root, flatSide,
                                        forward, scale, pose.lift);
  // Stepping into a strike or a block: the body leans out towards a ball
  // beyond easy reach (drawn only; the player stays where the engine has
  // him).
  Vec3 lunge;
  if (eventWeight > 0.0f && slot.action.event != HEADER &&
      slot.action.event != THROW && slot.action.event != SLIDE)
  {
    const Vec3 toContact{slot.action.contact.x - root.x,
                         slot.action.contact.y - root.y, 0.0f};
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
  std::array<Vec3, 2> hipJoint{};
  std::array<Vec3, 2> ankleTarget{};
  std::array<float, 2> heel{};
  std::array<bool, 2> planted{};
  buildPlayerAnkleTargets(slot, pose, root, pelvis, bodyUp, bodySide, forward,
                          scale, ankleHeight, hipJoint, ankleTarget, planted);
  applyEventFootTargets(ankleTarget, planted, slot, pelvis, ankleHeight, scale,
                        eventTime, eventWeight);

  // --- legs ---------------------------------------------------------------
  std::array<Vec3, 2> knee{};
  std::array<Vec3, 2> ankle{};
  solvePlayerLegJoints(slot, hipJoint, ankleTarget, planted, forward, bodyUp,
                       scale, knee, ankle, heel);
  drawPlayerLegs(*this, slot, hipJoint, knee, ankle, heel, planted, bodySide,
                 scale, bulk, objectDepth, lod, kit, look, skin);

  const PlayerTorsoFrame torso =
      drawPlayerTorso(*this, slot, pose, pelvis, bodyUp, bodySide, forward,
                      scale, bulk, objectDepth, lod, detailed, kitIndex, kit);
  const Vec3 torsoUp = torso.up;
  const Vec3 torsoForward = torso.forward;
  const Vec3 torsoSide = torso.side;
  const Vec3 chest = torso.chest;

  // --- arms -----------------------------------------------------------------
  const Vec3 headCentre = chest + torsoUp * (P::NECK_LENGTH * scale);
  const BodyFrame armFrame{torsoUp, torsoForward, torsoSide};
  std::array<Vec3, 2> handTarget{};
  float handWeight = 0.0f;
  if (action == PlayerRig::Action::THROW_IN || slot.action.event == THROW)
  {
    // Both hands over the head; the throw whips them forward.
    const float throwShare =
        slot.action.event == THROW
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
    handWeight = slot.action.event == THROW ? eventWeight : 1.0f;
    if (heldBy == &player)
    {
      heldBallWorld = grip + torsoUp * (0.02f * scale);
      heldBallPlaced = true;
    }
  }
  else if (keeper && slot.keeper.holdBlend > 0.01f &&
           slot.keeper.diveBlend < 0.5f)
  {
    // The ball gathered into the chest.
    const Vec3 grip = chest - torsoUp * (R::KEEPER_GRIP_DOWN * scale) +
                      torsoForward * (R::KEEPER_GRIP_FORWARD * scale);
    handTarget = {grip + torsoSide * (R::HAND_GRIP_HALF * scale),
                  grip - torsoSide * (R::HAND_GRIP_HALF * scale)};
    handWeight = slot.keeper.holdBlend;
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
  else if (mood == GoalMood::DEJECTED && celebrating && slot.action.handsOnHead)
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
                 P::ELBOW_HALF_WIDTH * bulk, kit.shirt,
                 {armHint, objectDepth, lod});
      addSegment(elbow, wrist, P::ELBOW_HALF_WIDTH * bulk,
                 P::WRIST_HALF_WIDTH * bulk, keeper ? kit.shirt : skin,
                 {armHint, objectDepth, lod});
      continue;
    }
    if (keeper)
    {
      addSegment(shoulder, elbow, P::UPPER_ARM_TOP * bulk,
                 P::ELBOW_HALF_WIDTH * bulk, kit.shirt,
                 {armHint, objectDepth, lod});
      addSegment(elbow, wrist, P::ELBOW_HALF_WIDTH * bulk,
                 P::WRIST_HALF_WIDTH * bulk, kit.shirt,
                 {armHint, objectDepth, lod});
    }
    else
    {
      addSegment(shoulder, cuff, P::UPPER_ARM_TOP * bulk * 1.08f,
                 P::SLEEVE_HEM * bulk, kit.shirt, {armHint, objectDepth, lod});
      addSegment(cuff, elbow, P::UPPER_ARM_MIDDLE * bulk,
                 P::ELBOW_HALF_WIDTH * bulk, skin, {armHint, objectDepth, lod});
      addSegment(elbow, wrist, P::ELBOW_HALF_WIDTH * bulk,
                 P::WRIST_HALF_WIDTH * bulk, skin, {armHint, objectDepth, lod});
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
               shadeColor(skin, 0.9f), {torsoForward, objectDepth, lod});
  }
  if (ScreenPoint head; projection.project(headCentre, head))
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
        static_cast<std::uint32_t>(std::to_underlying(look.hairStyle)) |
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
                                          const BodyFrame& frame, float scale,
                                          float objectDepth)
{
  using N = Tuning::Number;
  using P = Tuning::Player;
  if (slot.number <= 0) return;
  const float backDepth =
      (P::WAIST_HALF_DEPTH + P::CHEST_HALF_DEPTH) * 0.5f * scale + 0.004f;
  const Vec3 centre =
      waist + frame.up * (N::CENTRE_UP * scale) - frame.forward * backDepth;
  if (const Vec3 toEye = RenderMath::normalize(projection.eye - centre);
      RenderMath::dot(frame.forward * -1.0f, toEye) < N::MIN_FACING)
    return;
  // Seen from behind, the player's right is the viewer's right.
  const Vec3 across = frame.side * -1.0f;
  const Vec3 down = frame.up * -1.0f;
  const BackPrintContext context{projection, points, primitives,
                                 keys,       texts,  numberColors[kitIndex],
                                 across,     down,   objectDepth};
  const auto number = static_cast<std::size_t>(std::clamp(slot.number, 0, 99));
  appendBackPrint(context, centre, N::HEIGHT * scale, N::MIN_PIXELS,
                  NUMBER_TEXT[number].data());
  if (!slot.backName.empty())
  {
    appendBackPrint(
        context,
        centre + frame.up *
                     ((N::HEIGHT * 0.5f + N::NAME_GAP + N::NAME_HEIGHT * 0.5f) *
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

template <class RendererState>
void detectTackleActions(RendererState& state,
                         const MatchRenderSnapshot& snapshot, float alpha)
{
  using R = Tuning::Rig;
  for (std::size_t index = 0; index < snapshot.players.size(); ++index)
  {
    const MatchRenderPlayer& player = snapshot.players[index];
    AnimationSlot& slot = state.slots[index];
    const bool challenged =
        player.tackleCooldown >
        slot.action.lastTackleCooldown + R::TACKLE_COOLDOWN_RISE;
    slot.action.lastTackleCooldown = player.tackleCooldown;
    if (!challenged || !state.livePlay || !player.onPitch) continue;
    if (slot.action.event != PlayerRig::Event::NONE &&
        slot.action.event != PlayerRig::Event::TACKLE)
      continue;
    const Vec3 root = RenderMath::worldFromPitch(lerpRenderPosition(
        player.previousPosition, player.currentPosition, alpha));
    const Vec3 toBall = state.ballWorld - root;
    const bool sliding =
        PlayerRig::flatDistance(state.ballWorld, root) > R::SLIDE_DISTANCE ||
        player.speedMetresPerSecond > R::SLIDE_SPEED;
    slot.action.event =
        sliding ? PlayerRig::Event::SLIDE : PlayerRig::Event::TACKLE;
    slot.action.eventSeconds = 0.0f;
    slot.action.contact = {state.ballWorld.x, state.ballWorld.y, 0.0f};
    const float reach = std::hypot(toBall.x, toBall.y);
    slot.action.eventDirection =
        reach > 0.05f ? Vec3{toBall.x / reach, toBall.y / reach, 0.0f}
                      : slot.gait.moveDirection;
    const Vec3 side{-slot.gait.moveDirection.y, slot.gait.moveDirection.x,
                    0.0f};
    slot.action.eventLeg = RenderMath::dot(toBall, side) >= 0.0f ? 0U : 1U;
  }
}

template <class RendererState>
void detectKickAction(RendererState& state, const MatchRenderSnapshot& snapshot,
                      float alpha)
{
  using Kk = Tuning::Kick;
  const MatchRenderBall& ball = snapshot.ball;
  const bool struck = ball.kickerLockout > state.lastKickerLockout + 1e-4f;
  state.lastKickerLockout = ball.kickerLockout;
  if (!struck || !ball.kicker) return;
  if (ball.kickerLockout < MatchTuning::Passing::KICKER_LOCKOUT_SECONDS - 0.01f)
    return;
  for (std::size_t index = 0; index < snapshot.players.size(); ++index)
  {
    const MatchRenderPlayer& player = snapshot.players[index];
    if (player.player != ball.kicker) continue;
    AnimationSlot& slot = state.slots[index];
    const Vec3 from = RenderMath::worldFromPitch(ball.previousPosition,
                                                 ball.previousHeightMetres);
    const Vec3 to = RenderMath::worldFromPitch(ball.currentPosition,
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
    slot.action.event = event;
    slot.action.eventSeconds = 0.0f;
    slot.action.contact = from;
    slot.action.eventDirection = flightLength > 1e-3f
                                     ? flight * (1.0f / flightLength)
                                     : slot.gait.moveDirection;
    const Vec3 root = RenderMath::worldFromPitch(lerpRenderPosition(
        player.previousPosition, player.currentPosition, alpha));
    const float yaw = RenderMath::worldYawFromFacing(RenderMath::lerpAngle(
        player.previousFacingAngle, player.currentFacingAngle, alpha));
    const Vec3 left{-std::sin(yaw), std::cos(yaw), 0.0f};
    if (const float offset = RenderMath::dot(from - root, left);
        offset > Kk::WRONG_FOOT_METRES)
      slot.action.eventLeg = 0U;
    else if (offset < -Kk::WRONG_FOOT_METRES)
      slot.action.eventLeg = 1U;
    else
      slot.action.eventLeg = slot.leftFooted ? 0U : 1U;
    return;
  }
}

void MatchRenderer3D::State::detectTouches(const MatchRenderSnapshot& snapshot)
{
  const float alpha = snapshot.interpolationAlpha;
  detectTackleActions(*this, snapshot, alpha);
  detectKickAction(*this, snapshot, alpha);
}

template <class RendererState>
void captureGoalEvent(RendererState& state, const MatchRenderSnapshot& snapshot)
{
  using enum MatchEventType;
  if (snapshot.events)
  {
    const std::vector<MatchEvent>& events = *snapshot.events;
    const std::size_t oldest = events.size() > 64 ? events.size() - 64 : 0;
    for (std::size_t index = events.size(); index-- > oldest;)
    {
      const MatchEvent& event = events[index];
      if (event.type != GOAL && event.type != OWN_GOAL) continue;
      state.goal.ownGoal = event.type == OWN_GOAL;
      state.goal.scorer = event.primaryPlayerId;
      state.goal.minute = MatchClock::minuteLabel(
          event.timeMinute, event.period, event.addedMinute > 0.0f);
      break;
    }
  }
  for (const MatchRenderPlayer& player : snapshot.players)
  {
    if (!state.goal.ownGoal && player.player &&
        player.player->getId() == state.goal.scorer)
      state.goal.scorerName = player.player->getName();
  }
}

void MatchRenderer3D::State::updateGoalMoment(
    const MatchRenderSnapshot& snapshot)
{
  using enum MatchEventType;
  using C = Tuning::Celebration;
  if (const bool scoring = snapshot.state == MatchState::GOAL; !scoring)
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
    captureGoalEvent(*this, snapshot);
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
  std::array<char, 16> score{};
  formatText(score, "{} - {}", snapshot.homeScore, snapshot.awayScore);
  // Scorer and minute in a fixed buffer: no allocation per frame.
  std::array<char, 96> detail{};
  const std::string_view separator =
      goal.scorerName.empty() || goal.minute.empty() ? "" : "  ";
  formatText(detail, "{}{}{}", goal.scorerName, separator, goal.minute);
  const bool hasDetail = detail[0] != '\0';
  const ImVec2 callSize = font->CalcTextSizeA(titleSize, FLT_MAX, 0.0f, call);
  const ImVec2 scoreSize =
      font->CalcTextSizeA(titleSize, FLT_MAX, 0.0f, score.data());
  const ImVec2 detailSize =
      font->CalcTextSizeA(em, FLT_MAX, 0.0f, detail.data());
  const float bar = em * S::BAR_EM;
  const float titleWidth = callSize.x + padding * 2.0f + scoreSize.x;
  const float detailWidth =
      detailSize.x + (hasDetail ? detailSize.y + padding * 0.4f : 0.0f);
  const float width = bar + padding * 2.0f + std::max(titleWidth, detailWidth);
  const float height = callSize.y +
                       (hasDetail ? detailSize.y + padding * 0.5f : 0.0f) +
                       padding * 1.5f;
  // Centre the banner on the view.
  const float x =
      projection.rect.x + (projection.rect.width - width) * 0.5f - slide * em;
  const float y = projection.rect.y + (projection.rect.height - height) * 0.5f;
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
  // "GOAL!" flashes.
  const float flash = 0.5f + 0.5f * std::sin(t * S::FLASH_HZ * 2.0f *
                                             std::numbers::pi_v<float>);
  const ImU32 callColor = fade(withAlpha(
      S::CALL_COLOR,
      static_cast<std::uint8_t>(255.0f * std::clamp(flash, 0.15f, 1.0f))));
  drawList->AddText(font, titleSize, {textX, textY}, callColor, call);
  drawList->AddText(font, titleSize,
                    {textX + callSize.x + padding * 2.0f, textY},
                    fade(S::TEXT_COLOR), score.data());
  if (hasDetail)
  {
    // A football to the left of the scorer's name (the game font has no emoji
    // glyph, so it is drawn: white ball, black pentagon and seams).
    const float ballR = detailSize.y * 0.5f;
    const float detailY = textY + callSize.y + padding * 0.5f;
    const ImVec2 ballCentre{textX + ballR, detailY + detailSize.y * 0.5f};
    const ImU32 ballColor = fade(IM_COL32(240, 243, 248, 255));
    const ImU32 patchColor = fade(IM_COL32(15, 18, 24, 255));
    drawList->AddCircleFilled(ballCentre, ballR, ballColor, 24);
    std::array<ImVec2, 5> pentagon{};
    for (int i = 0; i < 5; ++i)
    {
      const float angle =
          -std::numbers::pi_v<float> * 0.5f +
          static_cast<float>(i) * 2.0f * std::numbers::pi_v<float> / 5.0f;
      pentagon[static_cast<std::size_t>(i)] = {
          ballCentre.x + std::cos(angle) * ballR * 0.44f,
          ballCentre.y + std::sin(angle) * ballR * 0.44f};
    }
    drawList->AddConvexPolyFilled(pentagon.data(), 5, patchColor);
    for (const ImVec2& vertex : pentagon)
    {
      const float dx = vertex.x - ballCentre.x;
      const float dy = vertex.y - ballCentre.y;
      const float len = std::max(std::hypot(dx, dy), 1e-4f);
      drawList->AddLine(
          vertex,
          {ballCentre.x + dx / len * ballR, ballCentre.y + dy / len * ballR},
          patchColor, std::max(1.0f, ballR * 0.14f));
    }
    drawList->AddCircle(ballCentre, ballR, patchColor, 24,
                        std::max(1.0f, ballR * 0.14f));
    drawList->AddText(font, em,
                      {textX + ballR * 2.0f + padding * 0.6f, detailY},
                      fade(S::DETAIL_COLOR), detail.data());
  }
}

template <class RendererState>
void addGoalNetPanel(RendererState& state, const Stadium3D::NetPanel& net,
                     bool rippling, float age, Vec3 mouth)
{
  using R = Tuning::Ripple;
  std::array<ScreenPoint, 4> corners{};
  for (std::size_t index = 0; index < corners.size(); ++index)
    if (!state.projection.project(net.corners[index], corners[index])) return;
  Primitive primitive;
  primitive.firstPoint = static_cast<std::uint32_t>(state.points.size());
  primitive.kind = PrimitiveKind::NET;
  primitive.color = Tuning::Goal::NET_FILL_COLOR;
  primitive.secondaryColor = Tuning::Goal::NET_LINE_COLOR;
  for (const ScreenPoint& corner : corners)
    state.points.push_back(toImVec(corner));
  if (const std::span<ImVec2> quad(state.points.data() + primitive.firstPoint,
                                   4);
      signedArea(quad) < 0.0f)
    std::ranges::reverse(quad);
  const auto bilinear = [&net](float u, float v)
  {
    const Vec3 bottom = RenderMath::lerp(net.corners[0], net.corners[1], u);
    const Vec3 top = RenderMath::lerp(net.corners[3], net.corners[2], u);
    return RenderMath::lerp(bottom, top, v);
  };
  Vec3 outward = RenderMath::normalize(RenderMath::cross(
      net.corners[1] - net.corners[0], net.corners[3] - net.corners[0]));
  if (const Vec3 panelCentre = (net.corners[0] + net.corners[2]) * 0.5f;
      RenderMath::dot(outward, panelCentre - mouth) < 0.0f)
    outward = outward * -1.0f;
  const auto displaced = [&state, outward, age](Vec3 point)
  {
    const float distance = RenderMath::length(point - state.goal.impact);
    const float push =
        R::AMPLITUDE * std::exp(-R::DECAY * age) *
        std::exp(-distance / R::REACH) *
        std::cos(R::WAVE_NUMBER * distance - R::WAVE_SPEED * age);
    return point + outward * push;
  };
  for (int column = 1; column < net.columns; ++column)
  {
    const float u =
        static_cast<float>(column) / static_cast<float>(net.columns);
    appendNetStrand(state.projection, state.points, rippling, R::SEGMENTS,
                    bilinear(u, 0.0f), bilinear(u, 1.0f), displaced);
  }
  for (int row = 1; row < net.rows; ++row)
  {
    const float v = static_cast<float>(row) / static_cast<float>(net.rows);
    appendNetStrand(state.projection, state.points, rippling, R::SEGMENTS,
                    bilinear(0.0f, v), bilinear(1.0f, v), displaced);
  }
  primitive.pointCount =
      static_cast<std::uint16_t>(state.points.size() - primitive.firstPoint);
  const float depth = (corners[0].depth + corners[1].depth + corners[2].depth +
                       corners[3].depth) *
                      0.25f;
  state.keys.push_back(
      {depth, depth, static_cast<std::uint32_t>(state.primitives.size())});
  state.primitives.push_back(primitive);
}

template <class RendererState>
void addGoalFrame(RendererState& state, const Stadium3D::Goal& frame,
                  std::size_t goalIndex)
{
  using R = Tuning::Ripple;
  const bool rippling = state.goal.active &&
                        state.goal.goalIndex == goalIndex &&
                        state.goal.shownSeconds < R::SECONDS;
  const float age = state.goal.shownSeconds;
  const Vec3 mouth{goalIndex == 0 ? 0.0f : LENGTH, WIDTH * 0.5f,
                   Tuning::Goal::HEIGHT * 0.5f};
  for (const Stadium3D::NetPanel& net : frame.nets)
    addGoalNetPanel(state, net, rippling, age, mouth);
  for (const auto& support : frame.supports)
  {
    const float depth =
        state.projection.depth((support[0] + support[1]) * 0.5f);
    state.addLine(support[0], support[1], Tuning::Goal::SUPPORT_COLOR,
                  Tuning::Goal::POST_SIZE * 0.5f, depth);
  }
  for (const Stadium3D::Box& part : frame.frame)
  {
    const float depth =
        state.projection.depth((part.corners[0] + part.corners[7]) * 0.5f);
    state.addBox(part.corners, part.color, depth);
  }
}

template <class RendererState>
void updateBallRotation(RendererState& state, Vec3 world, Vec3 moved,
                        float distance, const ScreenPoint& screen)
{
  if (distance <= 1e-4f || distance >= Tuning::Player::TELEPORT_METRES) return;
  ScreenPoint ahead;
  if (!state.projection.project(world + moved * (1.0f / distance), ahead))
    return;
  const float dx = ahead.x - screen.x;
  const float dy = ahead.y - screen.y;
  if (const float length = std::hypot(dx, dy); length > 1e-3f)
    state.ballRoll = {dx / length, dy / length};
  state.ballSpin =
      std::fmod(state.ballSpin + std::min(distance / Tuning::Ball::RADIUS,
                                          Tuning::Ball::SPIN_MAX_PER_FRAME),
                TWO_PI);
}

void MatchRenderer3D::State::addGoals()
{
  for (std::size_t goalIndex = 0; goalIndex < geometry.goals.size();
       ++goalIndex)
    addGoalFrame(*this, geometry.goals[goalIndex], goalIndex);
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
    for (std::size_t index = 0; index < flag.size(); ++index)
    {
      if (!visible) break;
      visible = projection.project(flag[index], screen[index]);
    }
    if (visible) addPolygon(screen, C::FLAG_COLOR, depth, depth - 0.01f);
  }
}

void MatchRenderer3D::State::addBoards()
{
  using B = Tuning::Boards;
  for (std::uint32_t index = 0;
       index < static_cast<std::uint32_t>(geometry.boards.size()); ++index)
  {
    const Stadium3D::AdBoard& board = geometry.boards[index];
    const std::array<Vec3, 4> corners{
        board.origin, board.origin + board.right * B::LENGTH,
        board.origin + board.right * B::LENGTH + UP * B::HEIGHT,
        board.origin + UP * B::HEIGHT};
    std::array<ScreenPoint, 4> screen{};
    bool visible = true;
    for (std::size_t corner = 0; corner < 4; ++corner)
    {
      if (!visible) break;
      visible = projection.project(corners[corner], screen[corner]);
    }
    if (!visible) continue;
    const float depth = (screen[0].depth + screen[1].depth + screen[2].depth +
                         screen[3].depth) *
                        0.25f;
    if (const bool front =
            RenderMath::dot(board.normal, projection.eye - board.origin) > 0.0f;
        !front)
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
  const Vector2F ball = lerpRenderPosition(
      snapshot.ball.previousPosition, snapshot.ball.currentPosition, alpha);
  const float height = ballHeightMetres(snapshot.ball, alpha);
  // Held in the hands (keeper, throw-in) it is drawn where the hands are.
  const Vec3 world = heldBallPlaced
                         ? heldBallWorld
                         : RenderMath::worldFromPitch(ball, height + B::RADIUS);
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
  updateBallRotation(*this, world, moved, distance, screen);
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
  if (const float maximumWidth = B::LENGTH * B::TEXT_WIDTH_RATIO;
      textWidth > maximumWidth)
  {
    textHeight *= maximumWidth / textWidth;
    textWidth = maximumWidth;
  }
  const Vec3 textOrigin = board.origin +
                          board.right * ((B::LENGTH - textWidth) * 0.5f) +
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
  std::ranges::sort(keys,
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
  ImU32 hairColor = hair;
  if (style == HairStyle::BUZZ)
    hairColor = mixColor(hair, skin, 0.4f);
  else if (style == HairStyle::SHAVED)
    hairColor = mixColor(skin, hair, 0.18f);
  if (const bool fromBehind = facing < -0.35f; fromBehind)
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
  float capRadius = 0.86f;
  if (style == HairStyle::VOLUME)
    capRadius = 0.98f;
  else if (style == HairStyle::LONG)
    capRadius = 0.95f;
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
  std::array<char, 16> score{};
  formatText(score, "{} - {}", snapshot.homeScore, snapshot.awayScore);
  const auto minute = static_cast<int>(snapshot.matchTimeMinutes);
  const auto second = static_cast<int>(
      (snapshot.matchTimeMinutes - static_cast<float>(minute)) * 60.0f);
  std::array<char, 16> clock{};
  formatText(clock, "{:02}:{:02}", minute, second);

  const ImVec2 homeSize = ImGui::CalcTextSize(options.homeLabel);
  const ImVec2 awaySize = ImGui::CalcTextSize(options.awayLabel);
  const ImVec2 scoreSize = ImGui::CalcTextSize(score.data());
  const ImVec2 clockSize = ImGui::CalcTextSize(clock.data());
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
                    score.data());
  x += scoreWidth;
  team(options.awayLabel, awaySize, kits.away.shirt);
  x += H::PADDING;
  const float clockWidth = clockSize.x + H::PADDING * 2.0f;
  panel(clockWidth, H::PANEL_COLOR);
  drawList->AddText({x + H::PADDING, y + H::PADDING}, H::TEXT_COLOR,
                    clock.data());
}

MatchRenderer3D::MatchRenderer3D() : state(std::make_unique<State>()) {}

MatchRenderer3D::~MatchRenderer3D() = default;

bool MatchRenderer3D::projectPitch(Vector2F pitch, float heightMetres,
                                   float& screenX, float& screenY) const
{
  const State& s = *state;
  if (!s.hasProjection) return false;
  ScreenPoint point;
  if (!s.projection.project(RenderMath::worldFromPitch(pitch, heightMetres),
                            point))
    return false;
  screenX = point.x;
  screenY = point.y;
  return true;
}

template <class RendererState>
void updateMatchSimulation(RendererState& state,
                           const MatchRenderSnapshot& snapshot,
                           float deltaSeconds, float alpha)
{
  state.prepareMatch(snapshot);
  if (state.slots.size() != snapshot.players.size())
    state.slots.resize(snapshot.players.size());
  const float rawSimSeconds =
      state.lastMatchMinutes >= 0.0f
          ? (snapshot.matchTimeMinutes - state.lastMatchMinutes) * 60.0f
          : 0.0f;
  state.simSeconds =
      std::clamp(rawSimSeconds, 0.0f, Tuning::Pose::MAX_SIM_SECONDS);
  const bool firstFrame = state.lastMatchMinutes < 0.0f;
  state.lastMatchMinutes = snapshot.matchTimeMinutes;
  state.livePlay = snapshot.state == MatchState::PLAYING;
  const Vec3 previousBall = state.ballWorld;
  state.ballWorld = RenderMath::worldFromPitch(
      lerpRenderPosition(snapshot.ball.previousPosition,
                         snapshot.ball.currentPosition, alpha),
      ballHeightMetres(snapshot.ball, alpha));
  state.jumped = !firstFrame &&
                 (std::abs(rawSimSeconds) > Tuning::Camera::JUMP_SIM_SECONDS ||
                  RenderMath::length(state.ballWorld - previousBall) >
                      Tuning::Camera::JUMP_METRES);
  state.findBallHolder(snapshot, alpha);
  state.heldBallPlaced = false;
  state.homeKeeperState = snapshot.homeGoalkeeperState;
  state.awayKeeperState = snapshot.awayGoalkeeperState;
  state.playerStats = snapshot.playerStats;
  state.updateGoalMoment(snapshot);
  for (std::size_t index = 0; index < snapshot.players.size(); ++index)
    state.updateMotion(snapshot.players[index], state.slots[index], alpha,
                       deltaSeconds);
  state.detectTouches(snapshot);
  using C = Tuning::Crowd;
  const bool chance = state.livePlay && snapshot.ball.isShot;
  const float rate = chance ? C::RISE_RATE : C::SETTLE_RATE;
  state.crowdTension += ((chance ? 1.0f : 0.0f) - state.crowdTension) *
                        RenderMath::dampingFactor(rate, deltaSeconds);
  state.updateCrowdMotion();
}

template <class RendererState>
void updateRenderCamera(RendererState& state,
                        const MatchRenderSnapshot& snapshot,
                        const MatchRenderOptions& options,
                        const MatchViewport& viewport, float deltaSeconds,
                        float alpha)
{
  MatchCameraFocus focus = state.computeFocus(snapshot, deltaSeconds);
  if (options.activePlayer != 0)
  {
    for (const MatchRenderPlayer& player : snapshot.players)
    {
      if (!player.player || !player.onPitch ||
          player.player->getId() != options.activePlayer)
        continue;
      focus.hasActive = true;
      focus.active = RenderMath::worldFromPitch(lerpRenderPosition(
          player.previousPosition, player.currentPosition, alpha));
    }
  }
  if (state.jumped && options.cameraMode != MatchCameraMode::FREE)
    state.camera.snap(focus, options.cameraMode);
  else
    state.camera.update(focus, options.cameraMode,
                        state.cameraControl(options.cameraInput), deltaSeconds);
  state.projection = RenderMath::Projection::make(
      state.camera.eye(), state.camera.target(), state.camera.verticalFov(),
      Tuning::Camera::NEAR_PLANE, Tuning::Camera::FAR_PLANE,
      {viewport.x, viewport.y, viewport.width, viewport.height});
  state.hasProjection = true;
}

template <class RendererState>
void drawWorldLayers(RendererState& state, const MatchRenderSnapshot& snapshot,
                     [[maybe_unused]] const MatchRenderOptions& options,
                     float alpha)
{
  state.drawSky();
  const auto drawPolygons =
      [&state](const std::vector<Stadium3D::GroundPolygon>& polygons,
               bool antiAliased)
  {
    for (const Stadium3D::GroundPolygon& polygon : polygons)
      state.drawWorldPolygon(
          std::span<const Vec3>(polygon.points.data(), polygon.count),
          std::span<const ImU32>(polygon.colors.data(), polygon.count),
          antiAliased);
  };
  drawPolygons(state.geometry.ground, false);
  state.drawPitch();
  drawPolygons(state.geometry.wear, false);
  drawPolygons(state.geometry.markings, true);
  drawPolygons(state.geometry.standShadows, false);
  state.drawShadows(snapshot, alpha);
#ifdef DEBUG
  if (options.showAiDebug)
  {
    for (const MatchRenderPlayer& player : snapshot.players)
    {
      ScreenPoint from;
      ScreenPoint to;
      if (state.projection.project(
              RenderMath::worldFromPitch(lerpRenderPosition(
                  player.previousPosition, player.currentPosition, alpha)),
              from) &&
          state.projection.project(
              RenderMath::worldFromPitch(player.movementTarget), to))
        state.drawList->AddLine(toImVec(from), toImVec(to),
                                Tuning::Hud::DEBUG_TARGET_COLOR, 1.5f);
    }
  }
#endif
  state.drawStadium();
}

template <class RendererState>
void drawSortedLayers(RendererState& state, const MatchRenderSnapshot& snapshot,
                      const MatchRenderOptions& options, float alpha)
{
  state.points.clear();
  state.primitives.clear();
  state.keys.clear();
  state.texts.clear();
  state.playersOnScreen.clear();
  for (std::size_t index = 0; index < snapshot.players.size(); ++index)
    state.addPlayer(snapshot.players[index], state.slots[index], alpha);
  state.addGoals();
  state.addCornerFlags();
  state.addBoards();
  state.addBall(snapshot, alpha);
  state.flushSorted();
  state.drawVignette();
  state.drawLabelsAndHover(options);
  state.drawScoreBug(snapshot, options);
  state.drawGoalSting(snapshot);
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

  const float alpha = snapshot.interpolationAlpha;
  updateMatchSimulation(s, snapshot, deltaSeconds, alpha);
  updateRenderCamera(s, snapshot, options, viewport, deltaSeconds, alpha);

  drawList->PushClipRect(
      {viewport.x, viewport.y},
      {viewport.x + viewport.width, viewport.y + viewport.height}, true);
  drawWorldLayers(s, snapshot, options, alpha);
  drawSortedLayers(s, snapshot, options, alpha);
  drawList->PopClipRect();
}
