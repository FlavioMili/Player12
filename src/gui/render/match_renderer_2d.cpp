// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/render/match_renderer_2d.h"

#include <fmt/printf.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <format>
#include <numbers>
#include <span>
#include <string>
#include <vector>

#include "global/language_manager.h"
#include "gui/render/match_kit_colors.h"
#include "gui/render/match_render_2d_tuning.h"
#include "gui/render/match_render_math.h"
#include "gui/render/match_shirt_numbers.h"
#include "gui/render/match_stadium_3d.h"
#include "gui/widgets/theme.h"
#include "model/player.h"
#include "model/role_utils.h"

namespace
{
using Tuning = MatchRender2DTuning;

constexpr float LENGTH = MatchTuning::Pitch::LENGTH_METRES;
constexpr float WIDTH = MatchTuning::Pitch::WIDTH_METRES;
constexpr float PI = std::numbers::pi_v<float>;
constexpr float TWO_PI = 2.0f * PI;
constexpr int BATCH_CHUNK_VERTICES = 2048;

/** Real pitch geometry in metres (Law 1). */
namespace PitchMetres
{
constexpr float CENTRE_CIRCLE = 9.15f;
constexpr float PENALTY_AREA_DEPTH = 16.5f;
constexpr float PENALTY_AREA_WIDTH = 40.32f;
constexpr float GOAL_AREA_DEPTH = 5.5f;
constexpr float GOAL_AREA_WIDTH = 18.32f;
constexpr float PENALTY_SPOT = 11.0f;
constexpr float SPOT_RADIUS = 0.2f;
constexpr float CORNER_ARC = 1.0f;
constexpr float GOAL_WIDTH = 7.32f;
constexpr float GOAL_DEPTH = 2.0f;
}  // namespace PitchMetres

/** Player names are drawn at this share of the UI font size. */
constexpr float NAME_FONT_SCALE = 0.82f;

/** "0" to "99" for the tokens. */
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

float unitHash(std::uint32_t a, std::uint32_t b, std::uint32_t salt)
{
  return static_cast<float>(cosmeticHash(a * 73856093U ^ b * 19349663U ^ salt) &
                            0xFFFFU) /
         65535.0f;
}

/// Smooth value noise on a unit lattice (static pitch mottling).
float valueNoise(float x, float y, std::uint32_t salt)
{
  const float fx = std::floor(x);
  const float fy = std::floor(y);
  const auto ix = static_cast<std::uint32_t>(static_cast<std::int32_t>(fx));
  const auto iy = static_cast<std::uint32_t>(static_cast<std::int32_t>(fy));
  const float tx = x - fx;
  const float ty = y - fy;
  const float sx = tx * tx * (3.0f - 2.0f * tx);
  const float sy = ty * ty * (3.0f - 2.0f * ty);
  const float a = unitHash(ix, iy, salt);
  const float b = unitHash(ix + 1U, iy, salt);
  const float c = unitHash(ix, iy + 1U, salt);
  const float d = unitHash(ix + 1U, iy + 1U, salt);
  return std::lerp(std::lerp(a, b, sx), std::lerp(c, d, sx), sy);
}

#ifdef DEBUG
ImU32 intentDebugColor(PlayerIntent intent)
{
  using enum PlayerIntent;
  if (intent == RUN_IN_BEHIND || intent == ATTACK_BOX || intent == OVERLAP)
    return MatchSceneTuning::Marker::DEBUG_RUN_COLOR;
  if (intent == PRESS_BALL || intent == CLAIM_LOOSE_BALL ||
      intent == COVER_PRESS || intent == BLOCK_PASSING_LANE)
  {
    return MatchSceneTuning::Marker::DEBUG_PRESS_COLOR;
  }
  if (intent == OFFER_SUPPORT || intent == RECEIVE_PASS || intent == CARRY_BALL)
  {
    return MatchSceneTuning::Marker::DEBUG_SUPPORT_COLOR;
  }
  return MatchSceneTuning::Marker::DEBUG_TARGET_COLOR;
}
#endif

/** A point of the ball's path, in pitch metres (y from the top touchline). */
struct PathSample
{
  float x = 0.0f;
  float y = 0.0f;
  float height = 0.0f;
  float age = 0.0f;
};

/** One struck ball: its path so far and how long ago it arrived. */
struct Flight
{
  std::array<PathSample, Tuning::Flight::SAMPLES> samples{};
  int count = 0;
  float sampleTimer = 0.0f;
  /** Real seconds since the ball arrived; negative while still travelling. */
  float endedSeconds = -1.0f;
  bool shot = false;
  bool used = false;
};

/** Where a player is drawn this frame. */
struct TokenOnScreen
{
  const MatchRenderPlayer* player = nullptr;
  ImVec2 position;
  ImVec2 direction;
  int number = 0;
};
}  // namespace

struct MatchRenderer2D::State
{
  MatchKits kits;
  TeamID homeTeam = 0;
  TeamID awayTeam = 0;
  bool kitsChosen = false;
  /** Token rings and overlay colours derived from the kits. */
  ImU32 homeOverlay = 0;
  ImU32 awayOverlay = 0;
  MatchShirtNumbers numbers;

  ImDrawList* drawList = nullptr;
  ImVec2 whitePixel;
  MatchViewport viewport;
  float ppm = 1.0f;
  float uiScale = 1.0f;
  float elapsedSeconds = 0.0f;
  float frameSeconds = 0.0f;
  bool reducedMotion = false;
  int batchVertices = 0;
  int batchIndices = 0;

  // --- ball ------------------------------------------------------------------
  PathSample ball;
  float ballSpeed = 0.0f;
  ImVec2 ballDirection{1.0f, 0.0f};
  float ballSpin = 0.0f;
  std::array<PathSample, Tuning::Ball::TRAIL_SAMPLES> trail{};
  int trailNext = 0;
  float trailTimer = 0.0f;

  // --- kicks, flights and offside --------------------------------------------
  const Player* lastPossessor = nullptr;
  Vector2F lastBallPosition{-1.0f, -1.0f};
  float lastBallStepSpeed = 0.0f;
  std::array<Flight, 2> flights{};
  std::size_t currentFlight = 0;
  /** Defending line (metres) when the last ball was struck, and by whom. */
  float lineAtKick = -1.0f;
  bool kickByHome = false;
  std::size_t seenEvents = 0;
  bool eventsSeen = false;
  float offsideSeconds = -1.0f;
  float offsideX = 0.0f;

  // --- pitch control ---------------------------------------------------------
  std::array<float,
             (Tuning::Pressure::COLUMNS + 1) * (Tuning::Pressure::ROWS + 1)>
      pressure{};
  bool pressureValid = false;

  // --- cached static layers --------------------------------------------------
  /** Grass cell colours (fixed grid, built once). */
  std::vector<ImU32> grassColors;
  /** Stand crowd and boards as quads relative to the viewport origin. */
  std::vector<ImDrawVert> standQuads;
  float standWidth = 0.0f;
  float standHeight = 0.0f;
  float standApron = 0.0f;

  std::vector<TokenOnScreen> tokens;

  State()
  {
    tokens.reserve(32);
    standQuads.reserve(24000);
    buildGrassColors();
  }

  // --- helpers ---------------------------------------------------------------
  ImVec2 atMetres(float x, float y) const
  {
    return {viewport.x + x * ppm, viewport.y + y * ppm};
  }
  float px(float logicalPixels) const { return logicalPixels * uiScale; }

  void reserveBatch(int vertices, int indices);
  void releaseBatch();
  void batchQuad(ImVec2 a, ImVec2 b, ImVec2 c, ImVec2 d, ImU32 ca, ImU32 cb,
                 ImU32 cc, ImU32 cd);
  void batchFan(ImVec2 centre, float radiusX, float radiusY, int segments,
                ImU32 inner, ImU32 outer, std::uint32_t salt);

  void prepareMatch(const MatchRenderSnapshot& snapshot);
  void buildGrassColors();
  void buildStands(float apron);
  void updateBall(const MatchRenderSnapshot& snapshot, float alpha);
  void updateEvents(const MatchRenderSnapshot& snapshot);
  void detectKick(const MatchRenderSnapshot& snapshot);
  void updateFlights(const MatchRenderSnapshot& snapshot);
  void updatePressure(const MatchRenderSnapshot& snapshot, float alpha);
  float defendingLine(const MatchRenderSnapshot& snapshot, bool attackersHome,
                      float ballX) const;

  void drawStadium(float apron);
  void drawGrass();
  void drawWear();
  void drawPressure();
  void drawMarkings();
  void drawGoal(bool left);
  void drawOffside();
  void drawFlights();
  void drawTrail();
  void drawTokens(const MatchRenderSnapshot& snapshot,
                  const MatchRenderOptions& options, float alpha);
  void drawToken(const TokenOnScreen& token, float radius, bool hovered);
  void drawBall();
};

void MatchRenderer2D::State::reserveBatch(int vertices, int indices)
{
  if (vertices <= batchVertices && indices <= batchIndices) return;
  releaseBatch();
  // Chunks stay far below the 16-bit index limit of one reservation.
  batchVertices = std::max(vertices, BATCH_CHUNK_VERTICES);
  batchIndices = std::max(indices, BATCH_CHUNK_VERTICES * 3 / 2);
  drawList->PrimReserve(batchIndices, batchVertices);
}

void MatchRenderer2D::State::releaseBatch()
{
  if (batchVertices > 0 || batchIndices > 0)
    drawList->PrimUnreserve(batchIndices, batchVertices);
  batchVertices = 0;
  batchIndices = 0;
}

void MatchRenderer2D::State::batchQuad(ImVec2 a, ImVec2 b, ImVec2 c, ImVec2 d,
                                       ImU32 ca, ImU32 cb, ImU32 cc, ImU32 cd)
{
  reserveBatch(4, 6);
  const auto first = static_cast<ImDrawIdx>(drawList->_VtxCurrentIdx);
  drawList->PrimWriteVtx(a, whitePixel, ca);
  drawList->PrimWriteVtx(b, whitePixel, cb);
  drawList->PrimWriteVtx(c, whitePixel, cc);
  drawList->PrimWriteVtx(d, whitePixel, cd);
  drawList->PrimWriteIdx(first);
  drawList->PrimWriteIdx(static_cast<ImDrawIdx>(first + 1));
  drawList->PrimWriteIdx(static_cast<ImDrawIdx>(first + 2));
  drawList->PrimWriteIdx(first);
  drawList->PrimWriteIdx(static_cast<ImDrawIdx>(first + 2));
  drawList->PrimWriteIdx(static_cast<ImDrawIdx>(first + 3));
  batchVertices -= 4;
  batchIndices -= 6;
}

void MatchRenderer2D::State::batchFan(ImVec2 centre, float radiusX,
                                      float radiusY, int segments, ImU32 inner,
                                      ImU32 outer, std::uint32_t salt)
{
  // A soft blob: full colour at the centre, clear at an irregular rim.
  reserveBatch(segments + 1, segments * 3);
  const auto first = static_cast<ImDrawIdx>(drawList->_VtxCurrentIdx);
  drawList->PrimWriteVtx(centre, whitePixel, inner);
  for (int index = 0; index < segments; ++index)
  {
    const float angle =
        TWO_PI * static_cast<float>(index) / static_cast<float>(segments);
    const float wobble =
        1.0f +
        (unitHash(salt, static_cast<std::uint32_t>(index), 41U) - 0.5f) * 0.35f;
    drawList->PrimWriteVtx({centre.x + std::cos(angle) * radiusX * wobble,
                            centre.y + std::sin(angle) * radiusY * wobble},
                           whitePixel, outer);
  }
  for (int index = 0; index < segments; ++index)
  {
    drawList->PrimWriteIdx(first);
    drawList->PrimWriteIdx(static_cast<ImDrawIdx>(first + 1 + index));
    drawList->PrimWriteIdx(
        static_cast<ImDrawIdx>(first + 1 + (index + 1) % segments));
  }
  batchVertices -= segments + 1;
  batchIndices -= segments * 3;
}

void MatchRenderer2D::State::prepareMatch(const MatchRenderSnapshot& snapshot)
{
  TeamID home = snapshot.homeTeam;
  TeamID away = snapshot.awayTeam;
  for (const MatchRenderPlayer& player : snapshot.players)
  {
    if (!player.player) continue;
    if (player.isHomeTeam && home == 0) home = player.player->getTeamId();
    if (!player.isHomeTeam && away == 0) away = player.player->getTeamId();
  }
  if (kitsChosen && home == homeTeam && away == awayTeam) return;
  // The same strips as the 3D view, chosen once per fixture.
  kits = chooseMatchKits(home, away);
  homeTeam = home;
  awayTeam = away;
  kitsChosen = true;
  numbers.reset(snapshot);
  flights = {};
  trail = {};
  lastPossessor = nullptr;
  eventsSeen = false;
  offsideSeconds = -1.0f;
  pressureValid = false;
  standWidth = 0.0f;

  // Overlay colours must stand out on the grass and from each other.
  const auto onGrass = [](const KitColors& kit)
  {
    return kitColorDistance(kit.shirt, Tuning::Grass::PITCH_COLOR) <
                   KIT_CLASH_DISTANCE
               ? kit.trim
               : kit.shirt;
  };
  homeOverlay = onGrass(kits.home);
  awayOverlay = onGrass(kits.away);
  if (kitColorDistance(homeOverlay, awayOverlay) < KIT_CLASH_DISTANCE)
  {
    awayOverlay = kits.away.trim;
    if (kitColorDistance(homeOverlay, awayOverlay) < KIT_CLASH_DISTANCE)
      awayOverlay =
          kitColorDistance(homeOverlay, IM_COL32_WHITE) > KIT_CLASH_DISTANCE
              ? IM_COL32(240, 242, 245, 255)
              : IM_COL32(20, 22, 28, 255);
  }
}

void MatchRenderer2D::State::buildGrassColors()
{
  using G = Tuning::Grass;
  constexpr int SPLIT = G::CELL_SPLIT;
  constexpr int SIDE = SPLIT + 1;
  grassColors.resize(
      static_cast<std::size_t>(G::STRIPES * G::BANDS * SIDE * SIDE));
  std::size_t index = 0;
  for (int stripe = 0; stripe < G::STRIPES; ++stripe)
  {
    for (int band = 0; band < G::BANDS; ++band)
    {
      // Mown stripes and faint cross bands give the classic checker.
      const float cellShade =
          (stripe % 2 == 0 ? 1.0f + G::STRIPE_CONTRAST
                           : 1.0f - G::STRIPE_CONTRAST) *
          (band % 2 == 0 ? 1.0f + G::BAND_CONTRAST : 1.0f - G::BAND_CONTRAST);
      for (int row = 0; row < SIDE; ++row)
      {
        for (int column = 0; column < SIDE; ++column)
        {
          const float x = LENGTH *
                          (static_cast<float>(stripe) +
                           static_cast<float>(column) / SPLIT) /
                          static_cast<float>(G::STRIPES);
          const float y =
              WIDTH *
              (static_cast<float>(band) + static_cast<float>(row) / SPLIT) /
              static_cast<float>(G::BANDS);
          const float dx = x / LENGTH * 2.0f - 1.0f;
          const float dy = y / WIDTH * 2.0f - 1.0f;
          const float reach = std::min(1.0f, 0.55f * dx * dx + 0.45f * dy * dy);
          const float light = std::lerp(G::LIGHT_CENTRE, G::LIGHT_EDGE, reach);
          const float mottle =
              (valueNoise(x / G::NOISE_METRES, y / G::NOISE_METRES, 31U) -
               0.5f) *
                  G::NOISE_STRENGTH +
              (valueNoise(x / G::FINE_NOISE_METRES, y / G::FINE_NOISE_METRES,
                          37U) -
               0.5f) *
                  G::FINE_NOISE_STRENGTH;
          grassColors[index++] =
              shadeColor(G::PITCH_COLOR, light * cellShade * (1.0f + mottle));
        }
      }
    }
  }
}

void MatchRenderer2D::State::buildStands(float apron)
{
  // Rebuilt only when the view size, UI scale or fixture changes: the crowd
  // and boards are stored relative to the viewport origin.
  using S = Tuning::Stadium;
  standQuads.clear();
  standWidth = viewport.width;
  standHeight = viewport.height;
  standApron = apron;
  const float width = viewport.width;
  const float height = viewport.height;
  const float runOff = apron * S::RUN_OFF_SHARE;
  const float board = std::max(1.0f, apron * S::BOARD_SHARE);
  const float goalClear = PitchMetres::GOAL_DEPTH * ppm + px(3.0f);
  const float endRunOff = std::max(runOff, goalClear);
  const auto quad = [&](float x0, float y0, float x1, float y1, ImU32 color)
  {
    for (const ImVec2 corner :
         {ImVec2{x0, y0}, ImVec2{x1, y0}, ImVec2{x1, y1}, ImVec2{x0, y1}})
    {
      ImDrawVert& vertex = standQuads.emplace_back();
      vertex.pos = corner;
      vertex.uv = {0.0f, 0.0f};
      vertex.col = color;
    }
  };

  // Advertising boards between the run-off and the stands.
  const float boardPixels = S::BOARD_LENGTH_METRES * ppm;
  const auto boards =
      [&](float from, float to, float fixed, bool alongX, std::uint32_t side)
  {
    for (std::size_t boardIndex = 0;; ++boardIndex)
    {
      const float start = from + static_cast<float>(boardIndex) * boardPixels;
      if (!(start < to)) break;
      const float end = std::min(to, start + boardPixels - px(1.0f));
      const ImU32 color =
          shadeColor(Stadium3D::SPONSORS[(static_cast<std::size_t>(side) * 3U +
                                          boardIndex) %
                                         Stadium3D::SPONSORS.size()]
                         .background,
                     0.85f);
      if (alongX)
        quad(start, fixed, end, fixed + board, color);
      else
        quad(fixed, start, fixed + board, end, color);
    }
  };
  if (apron >= runOff + board)
  {
    boards(0.0f, width, -runOff - board, true, 0U);
    boards(0.0f, width, height + runOff, true, 1U);
    if (apron >= endRunOff + board)
    {
      boards(0.0f, height, -endRunOff - board, false, 2U);
      boards(0.0f, height, width + endRunOff, false, 3U);
    }
  }

  // Spectators: a jittered seat grid with club colours behind the goals.
  const float step = std::max(2.0f, px(S::CROWD_STEP_PIXELS));
  const float dot = step * S::CROWD_SIZE_SHARE;
  constexpr std::array<ImU32, 8> CLOTHES{
      IM_COL32(46, 50, 60, 255),    IM_COL32(68, 72, 82, 255),
      IM_COL32(96, 98, 106, 255),   IM_COL32(150, 152, 158, 255),
      IM_COL32(200, 200, 204, 255), IM_COL32(120, 64, 54, 255),
      IM_COL32(42, 54, 92, 255),    IM_COL32(206, 178, 148, 255)};
  const auto crowd = [&](float x0, float y0, float x1, float y1,
                         std::uint32_t side, float fanShare,
                         const KitColors* fans)
  {
    if (x1 - x0 < step || y1 - y0 < step) return;
    const auto columns = static_cast<int>((x1 - x0) / step);
    const auto rows = static_cast<int>((y1 - y0) / step);
    const float originX =
        x0 + ((x1 - x0) - static_cast<float>(columns) * step) * 0.5f;
    const float originY =
        y0 + ((y1 - y0) - static_cast<float>(rows) * step) * 0.5f;
    for (int row = 0; row < rows; ++row)
    {
      for (int column = 0; column < columns; ++column)
      {
        const auto cx = static_cast<std::uint32_t>(column);
        const auto cy = static_cast<std::uint32_t>(row) + side * 4096U;
        if (unitHash(cx, cy, S::SEED) < S::EMPTY_SEAT_SHARE) continue;
        const float jitterX =
            (unitHash(cx, cy, S::SEED + 1U) - 0.5f) * step * 0.3f;
        const float jitterY =
            (unitHash(cx, cy, S::SEED + 2U) - 0.5f) * step * 0.3f;
        const float size =
            dot * (0.85f + 0.3f * unitHash(cx, cy, S::SEED + 3U));
        const float pick = unitHash(cx, cy, S::SEED + 4U);
        ImU32 color = CLOTHES[static_cast<std::size_t>(
            unitHash(cx, cy, S::SEED + 5U) * 7.99f)];
        if (fans && pick < fanShare)
          color = pick < fanShare * 0.6f ? fans->shirt : fans->trim;
        color =
            shadeColor(color, 0.72f + 0.2f * unitHash(cx, cy, S::SEED + 6U));
        const float x = originX + (static_cast<float>(column) + 0.5f) * step +
                        jitterX - size * 0.5f;
        const float y = originY + (static_cast<float>(row) + 0.5f) * step +
                        jitterY - size * 0.5f;
        quad(x, y, x + size, y + size, color);
      }
    }
  };
  const float sideInner = runOff + board;
  const float endInner = endRunOff + board;
  crowd(-apron, -apron, width + apron, -sideInner, 0U, S::SIDE_FAN_SHARE,
        &kits.home);
  crowd(-apron, height + sideInner, width + apron, height + apron, 1U,
        S::SIDE_FAN_SHARE, &kits.home);
  // The home end sits behind the left goal, the away fans behind the right.
  crowd(-apron, -sideInner, -endInner, height + sideInner, 2U, S::END_FAN_SHARE,
        &kits.home);
  crowd(width + endInner, -sideInner, width + apron, height + sideInner, 3U,
        S::END_FAN_SHARE, &kits.away);
}

void MatchRenderer2D::State::updateBall(const MatchRenderSnapshot& snapshot,
                                        float alpha)
{
  const MatchRenderBall& source = snapshot.ball;
  const Vector2F position = lerpRenderPosition(source.previousPosition,
                                               source.currentPosition, alpha);
  const float previousX = ball.x;
  const float previousY = ball.y;
  ball.x = position.x * LENGTH;
  ball.y = position.y * WIDTH;
  ball.height = std::max(
      0.0f,
      source.previousHeightMetres +
          (source.currentHeightMetres - source.previousHeightMetres) * alpha);
  const float stepX =
      (source.currentPosition.x - source.previousPosition.x) * LENGTH;
  const float stepY =
      (source.currentPosition.y - source.previousPosition.y) * WIDTH;
  ballSpeed =
      std::hypot(stepX, stepY) / MatchTuning::Timing::FIXED_STEP_SECONDS;
  if (std::hypot(stepX, stepY) > 1e-4f)
  {
    const float length = std::hypot(stepX, stepY);
    ballDirection = {stepX / length, stepY / length};
  }
  // The patches roll with the distance the drawn ball covers.
  const float moved = std::hypot(ball.x - previousX, ball.y - previousY);
  if (moved < Tuning::Ball::JUMP_METRES)
    ballSpin = std::fmod(
        ballSpin + std::min(moved / Tuning::Ball::RADIUS_METRES * 0.3f, 0.9f),
        TWO_PI);

  // A short trail of where the drawn ball has just been (real time).
  using B = Tuning::Ball;
  const bool teleported = moved >= Tuning::Ball::JUMP_METRES;
  if (teleported)
  {
    // A restart or playback jump: nothing from before carries over.
    trail = {};
    flights = {};
  }
  for (PathSample& sample : trail) sample.age += frameSeconds;
  trailTimer += frameSeconds;
  if (trailTimer >= B::TRAIL_SECONDS / static_cast<float>(B::TRAIL_SAMPLES) ||
      teleported)
  {
    trailTimer = 0.0f;
    trail[static_cast<std::size_t>(trailNext)] = {ball.x, ball.y, ball.height,
                                                  0.0f};
    trailNext = (trailNext + 1) % B::TRAIL_SAMPLES;
  }
}

float MatchRenderer2D::State::defendingLine(const MatchRenderSnapshot& snapshot,
                                            bool attackersHome,
                                            float ballX) const
{
  // The second-last defender (or the ball, or halfway, whichever is nearer
  // the goal line) of the side defending against the attackers.
  float keeperX = attackersHome ? LENGTH : 0.0f;
  for (const MatchRenderPlayer& player : snapshot.players)
  {
    if (player.isHomeTeam == attackersHome || !player.onPitch) continue;
    if (player.isGoalkeeper) keeperX = player.currentPosition.x * LENGTH;
  }
  // The defenders' own goal is the one their keeper guards.
  const bool towardsHigh = keeperX > LENGTH * 0.5f;
  float first = FLT_MAX;
  float second = FLT_MAX;
  for (const MatchRenderPlayer& player : snapshot.players)
  {
    if (player.isHomeTeam == attackersHome || !player.onPitch) continue;
    const float x = player.currentPosition.x * LENGTH;
    const float fromGoal = towardsHigh ? LENGTH - x : x;
    if (fromGoal < first)
    {
      second = first;
      first = fromGoal;
    }
    else if (fromGoal < second)
    {
      second = fromGoal;
    }
  }
  if (second == FLT_MAX) return -1.0f;
  const float ballFromGoal = towardsHigh ? LENGTH - ballX : ballX;
  const float line = std::min({second, ballFromGoal, LENGTH * 0.5f});
  return towardsHigh ? LENGTH - line : line;
}

void MatchRenderer2D::State::updateEvents(const MatchRenderSnapshot& snapshot)
{
  if (offsideSeconds >= 0.0f) offsideSeconds += frameSeconds;
  if (offsideSeconds > Tuning::Offside::SECONDS) offsideSeconds = -1.0f;
  if (!snapshot.events) return;
  const std::vector<MatchEvent>& events = *snapshot.events;
  if (!eventsSeen || events.size() < seenEvents)
  {
    // Nothing flashes for what happened before the view opened.
    seenEvents = events.size();
    eventsSeen = true;
    return;
  }
  for (std::size_t index = seenEvents; index < events.size(); ++index)
  {
    const MatchEvent& event = events[index];
    if (event.type != MatchEventType::OFFSIDE) continue;
    // The line is where the defence stood when the pass was played.
    const bool fromKick =
        lineAtKick >= 0.0f && event.hasTeam && event.isHomeTeam == kickByHome;
    offsideX = fromKick ? lineAtKick : event.position.x * LENGTH;
    offsideSeconds = 0.0f;
  }
  seenEvents = events.size();
}

void MatchRenderer2D::State::detectKick(const MatchRenderSnapshot& snapshot)
{
  // The same touch heuristics as the 3D view: possession leaving a player
  // at speed, or a sudden jump in ball speed next to someone.
  const MatchRenderBall& source = snapshot.ball;
  const Player* possessor = source.possessedBy;
  if (const bool newStep = source.currentPosition.x != lastBallPosition.x ||
                           source.currentPosition.y != lastBallPosition.y;
      !newStep)
  {
    if (possessor) lastPossessor = possessor;
    return;
  }
  lastBallPosition = source.currentPosition;
  const float fromX = source.previousPosition.x * LENGTH;
  const float fromY = source.previousPosition.y * WIDTH;
  const float stepMetres = std::hypot(source.currentPosition.x * LENGTH - fromX,
                                      source.currentPosition.y * WIDTH - fromY);
  const float speed = stepMetres / MatchTuning::Timing::FIXED_STEP_SECONDS;
  const float previousSpeed = lastBallStepSpeed;
  lastBallStepSpeed = speed;
  const bool livePlay = snapshot.state == MatchState::PLAYING;
  const MatchRenderPlayer* kicker = nullptr;
  constexpr float MIN_KICK_SPEED = 6.5f;
  constexpr float SPEED_JUMP = 5.0f;
  constexpr float REACH_METRES = 2.2f;
  if (constexpr float TELEPORT_METRES = 6.0f;
      livePlay && stepMetres < TELEPORT_METRES && speed >= MIN_KICK_SPEED)
  {
    if (lastPossessor && possessor != lastPossessor)
    {
      for (const MatchRenderPlayer& player : snapshot.players)
        if (player.player == lastPossessor) kicker = &player;
    }
    else if (!possessor && speed - previousSpeed >= SPEED_JUMP)
    {
      float best = REACH_METRES;
      for (const MatchRenderPlayer& player : snapshot.players)
      {
        if (!player.onPitch) continue;
        const float distance =
            std::hypot(player.previousPosition.x * LENGTH - fromX,
                       player.previousPosition.y * WIDTH - fromY);
        if (distance < best)
        {
          best = distance;
          kicker = &player;
        }
      }
    }
  }
  lastPossessor = possessor;
  if (!kicker) return;

  kickByHome = kicker->isHomeTeam;
  lineAtKick = defendingLine(snapshot, kickByHome, fromX);
  if (Flight& previous = flights[currentFlight];
      previous.used && previous.endedSeconds < 0.0f)
    previous.endedSeconds = 0.0f;
  currentFlight = (currentFlight + 1) % flights.size();
  Flight& flight = flights[currentFlight];
  flight = Flight{};
  flight.used = true;
  flight.shot = source.isShot;
  flight.samples[0] = {fromX, fromY, source.previousHeightMetres, 0.0f};
  flight.count = 1;
}

void MatchRenderer2D::State::updateFlights(const MatchRenderSnapshot& snapshot)
{
  using F = Tuning::Flight;
  const bool travelling = snapshot.state == MatchState::PLAYING &&
                          !snapshot.ball.possessedBy &&
                          ballSpeed >= F::STOP_SPEED;
  for (std::size_t index = 0; index < flights.size(); ++index)
  {
    Flight& flight = flights[index];
    if (!flight.used) continue;
    if (flight.endedSeconds >= 0.0f)
    {
      flight.endedSeconds += frameSeconds;
      if (flight.endedSeconds > F::FADE_SECONDS) flight.used = false;
      continue;
    }
    if (snapshot.ball.isShot) flight.shot = true;
    flight.sampleTimer += frameSeconds;
    const bool full = flight.count >= F::SAMPLES;
    if (!full && flight.sampleTimer >= F::SAMPLE_SECONDS)
    {
      flight.sampleTimer = 0.0f;
      flight.samples[static_cast<std::size_t>(flight.count++)] = ball;
    }
    // The first frames after the strike may still show the kicker in
    // possession; only a later touch or a stop ends the flight.
    if (index == currentFlight && (travelling || flight.count < 3) && !full)
      continue;
    if (flight.count < F::SAMPLES)
      flight.samples[static_cast<std::size_t>(flight.count++)] = ball;
    flight.endedSeconds = 0.0f;
  }
}

void MatchRenderer2D::State::updatePressure(const MatchRenderSnapshot& snapshot,
                                            float alpha)
{
  using P = Tuning::Pressure;
  const float blend =
      pressureValid ? RenderMath::dampingFactor(P::RESPONSE_RATE, frameSeconds)
                    : 1.0f;
  // Players are projected a little along their run: who gets there first.
  std::array<std::array<float, 3>, 32> sources{};
  std::size_t count = 0;
  for (const MatchRenderPlayer& player : snapshot.players)
  {
    if (!player.onPitch || count >= sources.size()) continue;
    const Vector2F at = lerpRenderPosition(player.previousPosition,
                                           player.currentPosition, alpha);
    const float vx = (player.currentPosition.x - player.previousPosition.x) *
                     LENGTH / MatchTuning::Timing::FIXED_STEP_SECONDS;
    const float vy = (player.currentPosition.y - player.previousPosition.y) *
                     WIDTH / MatchTuning::Timing::FIXED_STEP_SECONDS;
    sources[count++] = {at.x * LENGTH + vx * P::LOOKAHEAD_SECONDS,
                        at.y * WIDTH + vy * P::LOOKAHEAD_SECONDS,
                        player.isHomeTeam ? 1.0f : -1.0f};
  }
  const float inverseReach = 1.0f / (P::REACH_METRES * P::REACH_METRES);
  std::size_t index = 0;
  for (int row = 0; row <= P::ROWS; ++row)
  {
    const float y =
        WIDTH * static_cast<float>(row) / static_cast<float>(P::ROWS);
    for (int column = 0; column <= P::COLUMNS; ++column)
    {
      const float x =
          LENGTH * static_cast<float>(column) / static_cast<float>(P::COLUMNS);
      float home = 0.0f;
      float away = 0.0f;
      for (std::size_t source = 0; source < count; ++source)
      {
        const float dx = sources[source][0] - x;
        const float dy = sources[source][1] - y;
        const float weight = 1.0f / (1.0f + (dx * dx + dy * dy) * inverseReach);
        (sources[source][2] > 0.0f ? home : away) += weight;
      }
      const float control = (home - away) / (home + away + 1e-3f);
      pressure[index] += (control - pressure[index]) * blend;
      ++index;
    }
  }
  pressureValid = true;
}

void MatchRenderer2D::State::drawStadium(float apron)
{
  using S = Tuning::Stadium;
  const float x0 = viewport.x - apron;
  const float y0 = viewport.y - apron;
  const float x1 = viewport.x + viewport.width + apron;
  const float y1 = viewport.y + viewport.height + apron;
  const float runOff = apron * S::RUN_OFF_SHARE;
  const float board = std::max(1.0f, apron * S::BOARD_SHARE);
  const float endRunOff =
      std::max(runOff, PitchMetres::GOAL_DEPTH * ppm + px(3.0f));
  const float sideInner = runOff + board;
  const float endInner = endRunOff + board;
  // Stands: lit at the front, fading into the dark back rows.
  const ImU32 front = S::STAND_FRONT_COLOR;
  const ImU32 back = S::STAND_BACK_COLOR;
  drawList->AddRectFilledMultiColor({x0, y0}, {x1, viewport.y - sideInner},
                                    back, back, front, front);
  drawList->AddRectFilledMultiColor(
      {x0, viewport.y + viewport.height + sideInner}, {x1, y1}, front, front,
      back, back);
  drawList->AddRectFilledMultiColor(
      {x0, viewport.y - sideInner},
      {viewport.x - endInner, viewport.y + viewport.height + sideInner}, back,
      front, front, back);
  drawList->AddRectFilledMultiColor(
      {viewport.x + viewport.width + endInner, viewport.y - sideInner},
      {x1, viewport.y + viewport.height + sideInner}, front, back, back, front);
  // Run-off grass up to the boards.
  drawList->AddRectFilled(
      {viewport.x - std::min(apron, endInner),
       viewport.y - std::min(apron, sideInner)},
      {viewport.x + viewport.width + std::min(apron, endInner),
       viewport.y + viewport.height + std::min(apron, sideInner)},
      S::BOARD_COLOR);
  drawList->AddRectFilled(
      {viewport.x - std::min(apron, endRunOff),
       viewport.y - std::min(apron, runOff)},
      {viewport.x + viewport.width + std::min(apron, endRunOff),
       viewport.y + viewport.height + std::min(apron, runOff)},
      Tuning::Grass::RUN_OFF_COLOR);

  if (standWidth != viewport.width || standHeight != viewport.height ||
      standApron != apron)
    buildStands(apron);
  const auto total = static_cast<int>(standQuads.size());
  for (int first = 0; first < total; first += 4)
  {
    reserveBatch(4, 6);
    const auto base = static_cast<ImDrawIdx>(drawList->_VtxCurrentIdx);
    for (int corner = 0; corner < 4; ++corner)
    {
      const ImDrawVert& vertex =
          standQuads[static_cast<std::size_t>(first + corner)];
      drawList->PrimWriteVtx(
          {vertex.pos.x + viewport.x, vertex.pos.y + viewport.y}, whitePixel,
          vertex.col);
    }
    drawList->PrimWriteIdx(base);
    drawList->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1));
    drawList->PrimWriteIdx(static_cast<ImDrawIdx>(base + 2));
    drawList->PrimWriteIdx(base);
    drawList->PrimWriteIdx(static_cast<ImDrawIdx>(base + 2));
    drawList->PrimWriteIdx(static_cast<ImDrawIdx>(base + 3));
    batchVertices -= 4;
    batchIndices -= 6;
  }
  releaseBatch();
}

void MatchRenderer2D::State::drawGrass()
{
  using G = Tuning::Grass;
  constexpr int SPLIT = G::CELL_SPLIT;
  constexpr int SIDE = SPLIT + 1;
  const float cellWidth = viewport.width / static_cast<float>(G::STRIPES);
  const float cellHeight = viewport.height / static_cast<float>(G::BANDS);
  std::size_t colorIndex = 0;
  for (int stripe = 0; stripe < G::STRIPES; ++stripe)
  {
    const float left = viewport.x + cellWidth * static_cast<float>(stripe);
    for (int band = 0; band < G::BANDS; ++band)
    {
      const float top = viewport.y + cellHeight * static_cast<float>(band);
      reserveBatch(SIDE * SIDE, SPLIT * SPLIT * 6);
      const auto base = static_cast<ImDrawIdx>(drawList->_VtxCurrentIdx);
      for (int row = 0; row < SIDE; ++row)
      {
        for (int column = 0; column < SIDE; ++column)
        {
          drawList->PrimWriteVtx(
              {left + cellWidth * static_cast<float>(column) / SPLIT,
               top + cellHeight * static_cast<float>(row) / SPLIT},
              whitePixel, grassColors[colorIndex++]);
        }
      }
      for (int row = 0; row < SPLIT; ++row)
      {
        for (int column = 0; column < SPLIT; ++column)
        {
          const auto corner =
              static_cast<ImDrawIdx>(base + row * SIDE + column);
          drawList->PrimWriteIdx(corner);
          drawList->PrimWriteIdx(static_cast<ImDrawIdx>(corner + 1));
          drawList->PrimWriteIdx(static_cast<ImDrawIdx>(corner + SIDE + 1));
          drawList->PrimWriteIdx(corner);
          drawList->PrimWriteIdx(static_cast<ImDrawIdx>(corner + SIDE + 1));
          drawList->PrimWriteIdx(static_cast<ImDrawIdx>(corner + SIDE));
        }
      }
      batchVertices -= SIDE * SIDE;
      batchIndices -= SPLIT * SPLIT * 6;
    }
  }
  releaseBatch();
}

void MatchRenderer2D::State::drawWear()
{
  using G = Tuning::Grass;
  const ImU32 inner = withAlpha(G::WEAR_COLOR, G::WEAR_ALPHA);
  const ImU32 outer = withAlpha(G::WEAR_COLOR, 0);
  batchFan(atMetres(LENGTH * 0.5f, WIDTH * 0.5f), 1.6f * ppm, 1.3f * ppm,
           G::WEAR_SEGMENTS, inner, outer, 1U);
  for (const bool left : {true, false})
  {
    const float line = left ? 0.0f : LENGTH;
    const float inward = left ? 1.0f : -1.0f;
    // The keeper's patch in the goalmouth is the most worn ground there is.
    batchFan(atMetres(line + inward * 2.2f, WIDTH * 0.5f), 2.2f * ppm,
             3.4f * ppm, G::WEAR_SEGMENTS, inner, outer, left ? 2U : 3U);
    batchFan(atMetres(line + inward * PitchMetres::PENALTY_SPOT, WIDTH * 0.5f),
             1.2f * ppm, 1.0f * ppm, G::WEAR_SEGMENTS, inner, outer,
             left ? 4U : 5U);
  }
  releaseBatch();
}

void MatchRenderer2D::State::drawPressure()
{
  using P = Tuning::Pressure;
  constexpr int SIDE = P::COLUMNS + 1;
  const auto color = [this](float control)
  {
    const float strength = std::min(1.0f, std::abs(control));
    const auto alpha =
        static_cast<std::uint8_t>(P::MAX_ALPHA * strength * strength);
    return withAlpha(control >= 0.0f ? homeOverlay : awayOverlay, alpha);
  };
  for (int row = 0; row < P::ROWS; ++row)
  {
    for (int column = 0; column < P::COLUMNS; ++column)
    {
      const auto at = [&](int c, int r)
      {
        return ImVec2{viewport.x + viewport.width * static_cast<float>(c) /
                                       static_cast<float>(P::COLUMNS),
                      viewport.y + viewport.height * static_cast<float>(r) /
                                       static_cast<float>(P::ROWS)};
      };
      const auto value = [&](int c, int r)
      { return pressure[static_cast<std::size_t>(r * SIDE + c)]; };
      batchQuad(at(column, row), at(column + 1, row), at(column + 1, row + 1),
                at(column, row + 1), color(value(column, row)),
                color(value(column + 1, row)),
                color(value(column + 1, row + 1)),
                color(value(column, row + 1)));
    }
  }
  releaseBatch();
}

void MatchRenderer2D::State::drawMarkings()
{
  namespace M = PitchMetres;
  using L = Tuning::Lines;
  const ImU32 color = L::COLOR;
  const float line = std::max(px(L::MIN_PIXELS), L::WIDTH_METRES * ppm);
  const float half = line * 0.5f;
  const ImVec2 topLeft{viewport.x, viewport.y};
  const ImVec2 bottomRight{viewport.x + viewport.width,
                           viewport.y + viewport.height};
  // Lines are painted inside the field of play (the touchline belongs to it).
  drawList->AddRect({topLeft.x + half, topLeft.y + half},
                    {bottomRight.x - half, bottomRight.y - half}, color, 0.0f,
                    ImDrawFlags_None, line);
  const ImVec2 centre = atMetres(LENGTH * 0.5f, WIDTH * 0.5f);
  drawList->AddLine({centre.x, topLeft.y}, {centre.x, bottomRight.y}, color,
                    line);
  drawList->AddCircle(centre, M::CENTRE_CIRCLE * ppm, color, 0, line);
  const float spot = std::max(line, M::SPOT_RADIUS * ppm);
  drawList->AddCircleFilled(centre, spot, color);

  const float arcHalf =
      std::acos((M::PENALTY_AREA_DEPTH - M::PENALTY_SPOT) / M::CENTRE_CIRCLE);
  for (const bool left : {true, false})
  {
    const float goalLine = left ? 0.0f : LENGTH;
    const float inward = left ? 1.0f : -1.0f;
    const auto box = [&](float depth, float width)
    {
      const ImVec2 a = atMetres(goalLine, (WIDTH - width) * 0.5f);
      const ImVec2 b =
          atMetres(goalLine + inward * depth, (WIDTH + width) * 0.5f);
      drawList->AddRect({std::min(a.x, b.x), a.y}, {std::max(a.x, b.x), b.y},
                        color, 0.0f, ImDrawFlags_None, line);
    };
    box(M::PENALTY_AREA_DEPTH, M::PENALTY_AREA_WIDTH);
    box(M::GOAL_AREA_DEPTH, M::GOAL_AREA_WIDTH);
    const ImVec2 penaltySpot =
        atMetres(goalLine + inward * M::PENALTY_SPOT, WIDTH * 0.5f);
    drawList->AddCircleFilled(penaltySpot, spot, color);
    const float facing = left ? 0.0f : PI;
    drawList->PathArcTo(penaltySpot, M::CENTRE_CIRCLE * ppm, facing - arcHalf,
                        facing + arcHalf);
    drawList->PathStroke(color, ImDrawFlags_None, line);
  }
  const float corner = M::CORNER_ARC * ppm;
  drawList->PathArcTo(topLeft, corner, 0.0f, PI * 0.5f);
  drawList->PathStroke(color, ImDrawFlags_None, line);
  drawList->PathArcTo({bottomRight.x, topLeft.y}, corner, PI * 0.5f, PI);
  drawList->PathStroke(color, ImDrawFlags_None, line);
  drawList->PathArcTo({topLeft.x, bottomRight.y}, corner, -PI * 0.5f, 0.0f);
  drawList->PathStroke(color, ImDrawFlags_None, line);
  drawList->PathArcTo(bottomRight, corner, PI, PI * 1.5f);
  drawList->PathStroke(color, ImDrawFlags_None, line);
}

void MatchRenderer2D::State::drawGoal(bool left)
{
  namespace M = PitchMetres;
  using G = Tuning::Goal;
  const float line = left ? 0.0f : LENGTH;
  const float outward = left ? -1.0f : 1.0f;
  const ImVec2 postA = atMetres(line, (WIDTH - M::GOAL_WIDTH) * 0.5f);
  const ImVec2 postB = atMetres(line, (WIDTH + M::GOAL_WIDTH) * 0.5f);
  const float backX = postA.x + outward * M::GOAL_DEPTH * ppm;
  const float minX = std::min(postA.x, backX);
  const float maxX = std::max(postA.x, backX);
  // Shadow of the frame and net cast towards the bottom right.
  const float shadow = std::max(px(1.5f), 0.35f * ppm);
  drawList->AddRectFilled({minX + shadow, postA.y + shadow},
                          {maxX + shadow, postB.y + shadow}, G::SHADOW_COLOR);
  drawList->AddRectFilled({minX, postA.y}, {maxX, postB.y}, G::NET_FILL_COLOR);
  const float strand = std::max(1.0f, px(0.75f));
  for (int column = 1; column <= G::NET_COLUMNS; ++column)
  {
    const float x = postA.x + outward * M::GOAL_DEPTH * ppm *
                                  static_cast<float>(column) /
                                  static_cast<float>(G::NET_COLUMNS);
    drawList->AddLine({x, postA.y}, {x, postB.y}, G::NET_LINE_COLOR,
                      column == G::NET_COLUMNS ? strand * 1.6f : strand);
  }
  for (int row = 1; row < G::NET_ROWS; ++row)
  {
    const float y = postA.y + (postB.y - postA.y) * static_cast<float>(row) /
                                  static_cast<float>(G::NET_ROWS);
    drawList->AddLine({minX, y}, {maxX, y}, G::NET_LINE_COLOR, strand);
  }
  // Side netting edges, then the crossbar over the goal line and the posts.
  drawList->AddLine(postA, {backX, postA.y}, G::NET_LINE_COLOR, strand * 1.6f);
  drawList->AddLine(postB, {backX, postB.y}, G::NET_LINE_COLOR, strand * 1.6f);
  const float post = std::max(px(G::MIN_POST_PIXELS), G::POST_METRES * ppm);
  drawList->AddLine(postA, postB, G::POST_COLOR, post);
  drawList->AddCircleFilled(postA, post * 0.9f, G::POST_COLOR);
  drawList->AddCircleFilled(postB, post * 0.9f, G::POST_COLOR);
}

void MatchRenderer2D::State::drawOffside()
{
  using O = Tuning::Offside;
  if (offsideSeconds < 0.0f) return;
  const float fadeIn = std::clamp(offsideSeconds / 0.15f, 0.0f, 1.0f);
  const float fadeOut =
      std::clamp((O::SECONDS - offsideSeconds) / 0.6f, 0.0f, 1.0f);
  const float pulse =
      reducedMotion ? 1.0f
                    : 0.7f + 0.3f * std::cos(offsideSeconds * O::PULSE_SPEED);
  const float opacity = std::min(fadeIn, fadeOut) * pulse;
  const auto alpha = static_cast<std::uint8_t>(
      static_cast<float>((O::COLOR >> IM_COL32_A_SHIFT) & 0xFFU) * opacity);
  const ImU32 color = withAlpha(O::COLOR, alpha);
  const float x = viewport.x + std::clamp(offsideX, 0.0f, LENGTH) * ppm;
  const float thickness = std::max(px(2.0f), 0.2f * ppm);
  // A dashed line across the pitch with a soft glow either side.
  drawList->AddRectFilled(
      {x - thickness * 2.5f, viewport.y},
      {x + thickness * 2.5f, viewport.y + viewport.height},
      withAlpha(O::COLOR,
                static_cast<std::uint8_t>(static_cast<float>(alpha) * 0.22f)));
  const float dash = O::DASH_METRES * ppm;
  const float bottom = viewport.y + viewport.height;
  for (std::size_t dashIndex = 0;; ++dashIndex)
  {
    const float y = viewport.y + static_cast<float>(dashIndex) * (dash * 2.0f);
    if (!(y < bottom)) break;
    drawList->AddLine({x, y}, {x, std::min(y + dash, bottom)}, color,
                      thickness);
  }
}

void MatchRenderer2D::State::drawFlights()
{
  using F = Tuning::Flight;
  const float thickness = std::max(px(1.5f), F::WIDTH_METRES * ppm);
  const auto screen = [this](const PathSample& sample)
  {
    return atMetres(sample.x,
                    sample.y - sample.height * Tuning::Ball::LIFT_SHARE);
  };
  for (const Flight& flight : flights)
  {
    if (!flight.used || flight.count < 1) continue;
    const bool travelling = flight.endedSeconds < 0.0f;
    const int count = flight.count + (travelling ? 1 : 0);
    const auto sampleAt = [&](int index)
    {
      return index < flight.count
                 ? flight.samples[static_cast<std::size_t>(index)]
                 : ball;
    };
    // Very short touches are not worth a path.
    const PathSample& first = flight.samples[0];
    if (const PathSample last = sampleAt(count - 1);
        std::hypot(last.x - first.x, last.y - first.y) < 3.0f)
      continue;
    const float fade =
        travelling ? 1.0f
                   : std::clamp(1.0f - flight.endedSeconds / F::FADE_SECONDS,
                                0.0f, 1.0f);
    const ImU32 base = flight.shot ? F::SHOT_COLOR : F::PASS_COLOR;
    const float baseAlpha =
        static_cast<float>((base >> IM_COL32_A_SHIFT) & 0xFFU) * fade;
    ImVec2 previous = screen(first);
    for (int index = 1; index < count; ++index)
    {
      const ImVec2 next = screen(sampleAt(index));
      // Older parts of the path are fainter.
      const float along =
          static_cast<float>(index) / static_cast<float>(count - 1);
      const auto alpha =
          static_cast<std::uint8_t>(baseAlpha * (0.3f + 0.7f * along));
      drawList->AddLine(previous, next, withAlpha(base, alpha), thickness);
      previous = next;
    }
    const ImVec2 origin = screen(first);
    drawList->AddCircle(origin, thickness * 2.2f,
                        withAlpha(base, static_cast<std::uint8_t>(baseAlpha)),
                        0, std::max(1.0f, thickness * 0.7f));
    if (!travelling && count >= 2)
    {
      // An arrow head where the ball arrived.
      const ImVec2 tip = screen(sampleAt(count - 1));
      const ImVec2 from = screen(sampleAt(std::max(0, count - 3)));
      float dx = tip.x - from.x;
      float dy = tip.y - from.y;
      const float length = std::hypot(dx, dy);
      if (length > 1e-3f)
      {
        dx /= length;
        dy /= length;
        const float size = thickness * 4.0f;
        drawList->AddTriangleFilled(
            tip,
            {tip.x - dx * size - dy * size * 0.6f,
             tip.y - dy * size + dx * size * 0.6f},
            {tip.x - dx * size + dy * size * 0.6f,
             tip.y - dy * size - dx * size * 0.6f},
            withAlpha(base, static_cast<std::uint8_t>(baseAlpha)));
      }
    }
  }
}

void MatchRenderer2D::State::drawTrail()
{
  using B = Tuning::Ball;
  if (reducedMotion || ballSpeed < B::TRAIL_MIN_SPEED) return;
  const float radius =
      std::max(px(B::MIN_RADIUS_PIXELS), B::RADIUS_METRES * ppm);
  // Oldest to newest, widening and brightening towards the ball.
  ImVec2 previous{};
  bool hasPrevious = false;
  for (int step = 0; step <= B::TRAIL_SAMPLES; ++step)
  {
    const PathSample* sample = nullptr;
    if (step < B::TRAIL_SAMPLES)
    {
      sample = &trail[static_cast<std::size_t>((trailNext + step) %
                                               B::TRAIL_SAMPLES)];
      if (sample->age > B::TRAIL_SECONDS ||
          (sample->x == 0.0f && sample->y == 0.0f))
        continue;
    }
    else
    {
      sample = &ball;
    }
    const ImVec2 point =
        atMetres(sample->x, sample->y - sample->height * B::LIFT_SHARE);
    if (hasPrevious)
    {
      const float freshness =
          std::clamp(1.0f - sample->age / B::TRAIL_SECONDS, 0.0f, 1.0f);
      const auto alpha = static_cast<std::uint8_t>(
          static_cast<float>((B::TRAIL_COLOR >> IM_COL32_A_SHIFT) & 0xFFU) *
          freshness);
      drawList->AddLine(previous, point, withAlpha(B::TRAIL_COLOR, alpha),
                        std::max(1.0f, radius * 1.6f * freshness));
    }
    previous = point;
    hasPrevious = true;
  }
}

void MatchRenderer2D::State::drawToken(const TokenOnScreen& token, float radius,
                                       bool hovered)
{
  using T = Tuning::Token;
  const MatchRenderPlayer& player = *token.player;
  const KitColors& teamKit = player.isHomeTeam ? kits.home : kits.away;
  const KitColors& goalkeeperKit =
      player.isHomeTeam ? kits.homeGoalkeeper : kits.awayGoalkeeper;
  const KitColors& kit = player.isGoalkeeper ? goalkeeperKit : teamKit;
  const float fade = player.onPitch ? 1.0f : T::OFF_PITCH_ALPHA;
  const auto faded = [fade](ImU32 color)
  {
    const auto alpha = static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFFU);
    return withAlpha(color, static_cast<std::uint8_t>(alpha * fade));
  };
  // The shorts show as a ring round the shirt; a ring too close to the
  // shirt falls back to the trim, then to a darker shirt.
  ImU32 ring = kit.shorts;
  if (kitColorDistance(ring, kit.shirt) < 70.0f) ring = kit.trim;
  if (kitColorDistance(ring, kit.shirt) < 70.0f)
    ring = shadeColor(kit.shirt, 0.6f);
  const bool grassy =
      kitColorDistance(kit.shirt, Tuning::Grass::PITCH_COLOR) <
          KIT_CLASH_DISTANCE &&
      kitColorDistance(ring, Tuning::Grass::PITCH_COLOR) < KIT_CLASH_DISTANCE;
  const ImU32 outline = grassy ? T::LIGHT_OUTLINE_COLOR : T::OUTLINE_COLOR;
  const ImVec2 at = token.position;
  const float outlineWidth = std::max(1.0f, radius * T::OUTLINE_SHARE);

  // Facing wedge first, so only its tip shows past the token.
  const ImVec2 dir = token.direction;
  const auto rotate = [&](float angle, float reach)
  {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return ImVec2{at.x + (dir.x * c - dir.y * s) * reach,
                  at.y + (dir.x * s + dir.y * c) * reach};
  };
  const ImVec2 tip{at.x + dir.x * radius * T::WEDGE_REACH,
                   at.y + dir.y * radius * T::WEDGE_REACH};
  const ImVec2 tipOutline{
      at.x + dir.x * (radius * T::WEDGE_REACH + outlineWidth),
      at.y + dir.y * (radius * T::WEDGE_REACH + outlineWidth)};
  drawList->AddTriangleFilled(
      tipOutline, rotate(T::WEDGE_HALF_ANGLE + 0.08f, radius),
      rotate(-T::WEDGE_HALF_ANGLE - 0.08f, radius), faded(outline));
  drawList->AddTriangleFilled(tip, rotate(T::WEDGE_HALF_ANGLE, radius * 0.9f),
                              rotate(-T::WEDGE_HALF_ANGLE, radius * 0.9f),
                              faded(T::WEDGE_COLOR));

  drawList->AddCircleFilled(at, radius + outlineWidth, faded(outline));
  drawList->AddCircleFilled(at, radius, faded(ring));
  drawList->AddCircleFilled(at, radius * T::SHIRT_SHARE, faded(kit.shirt));
  // A soft gloss from the top-left light.
  drawList->AddCircleFilled({at.x - radius * 0.2f, at.y - radius * 0.22f},
                            radius * 0.4f, faded(IM_COL32(255, 255, 255, 30)));

  if (const float fontSize = radius * T::NUMBER_SHARE;
      token.number > 0 && fontSize >= px(T::MIN_NUMBER_PIXELS))
  {
    const char* text =
        NUMBER_TEXT[static_cast<std::size_t>(std::clamp(token.number, 0, 99))]
            .data();
    ImFont* font = ImGui::GetFont();
    const ImVec2 size = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
    const ImVec2 origin{at.x - size.x * 0.5f, at.y - size.y * 0.5f};
    const ImU32 color = faded(kitNumberColor(kit));
    drawList->AddText(font, fontSize, origin, color, text);
    drawList->AddText(font, fontSize, {origin.x + fontSize * 0.05f, origin.y},
                      color, text);
  }
  if (player.yellowCards > 0)
  {
    const ImVec2 card{at.x + radius * 0.55f, at.y - radius * 1.1f};
    drawList->AddRectFilled(
        {card.x - 1.0f, card.y - 1.0f},
        {card.x + radius * 0.36f + 1.0f, card.y + radius * 0.5f + 1.0f},
        faded(T::OUTLINE_COLOR), radius * 0.06f);
    drawList->AddRectFilled(card,
                            {card.x + radius * 0.36f, card.y + radius * 0.5f},
                            faded(T::CARD_COLOR), radius * 0.05f);
  }
  if (player.possessesBall)
  {
    const float pulse =
        reducedMotion
            ? 1.0f
            : 0.72f + 0.28f * std::sin(elapsedSeconds * T::PULSE_SPEED);
    drawList->AddCircle(
        at, radius * T::CARRIER_RING_SHARE,
        withAlpha(T::CARRIER_COLOR, static_cast<std::uint8_t>(255.0f * pulse)),
        0, std::max(px(1.5f), radius * 0.17f));
  }
  if (hovered)
  {
    drawList->AddCircle(at, radius * T::HOVER_RING_SHARE, T::HOVER_COLOR, 0,
                        std::max(px(1.0f), radius * 0.1f));
  }
}

void MatchRenderer2D::State::drawTokens(const MatchRenderSnapshot& snapshot,
                                        const MatchRenderOptions& options,
                                        float alpha)
{
  using T = Tuning::Token;
  const float fontSize = ImGui::GetFontSize();
  const float radius =
      std::max(T::RADIUS_METRES * ppm, fontSize * T::MIN_RADIUS_FONT_SHARE);
  tokens.clear();
  for (const MatchRenderPlayer& player : snapshot.players)
  {
    const Vector2F interpolated = lerpRenderPosition(
        player.previousPosition, player.currentPosition, alpha);
    // Facing is measured in normalised pitch space: scale it to metres.
    const float facing = RenderMath::lerpAngle(
        player.previousFacingAngle, player.currentFacingAngle, alpha);
    float dx = std::cos(facing) * LENGTH;
    float dy = std::sin(facing) * WIDTH;
    const float length = std::max(std::hypot(dx, dy), 1e-4f);
    dx /= length;
    dy /= length;
    tokens.push_back({&player,
                      atMetres(interpolated.x * LENGTH, interpolated.y * WIDTH),
                      {dx, dy},
                      numbers.numberFor(player, snapshot.playerStats)});
  }

  // Hover picks the nearest token under the mouse.
  const ImVec2 mouse = ImGui::GetMousePos();
  const TokenOnScreen* hovered = nullptr;
  float nearest = (radius * 1.3f) * (radius * 1.3f);
  for (const TokenOnScreen& token : tokens)
  {
    const float dx = mouse.x - token.position.x;
    const float dy = mouse.y - token.position.y;
    const float distance = dx * dx + dy * dy;
    if (distance < nearest)
    {
      nearest = distance;
      hovered = &token;
    }
  }

  // Shadows first so no token's shadow falls on a neighbour.
  const float offset = radius * T::SHADOW_OFFSET_SHARE;
  for (const TokenOnScreen& token : tokens)
  {
    drawList->AddCircleFilled(
        {token.position.x + offset * 1.6f, token.position.y + offset * 2.0f},
        radius * 1.18f, withAlpha(T::SHADOW_COLOR, 30));
    drawList->AddCircleFilled(
        {token.position.x + offset, token.position.y + offset * 1.3f},
        radius * 1.05f, T::SHADOW_COLOR);
  }
  const TokenOnScreen* carrier = nullptr;
  for (const TokenOnScreen& token : tokens)
  {
    if (token.player->possessesBall)
    {
      carrier = &token;
      continue;
    }
    if (&token == hovered) continue;
    drawToken(token, radius, false);
  }
  if (carrier && carrier != hovered) drawToken(*carrier, radius, false);
  if (hovered) drawToken(*hovered, radius, true);

  // Names sit under the token: by default only the ball carrier and a
  // hovered player are labelled so formations stay readable.
  const float nameSize = fontSize * NAME_FONT_SCALE;
  for (const TokenOnScreen& token : tokens)
  {
    const Player* source = token.player->player;
    if (!source || !(options.showPlayerNames || token.player->possessesBall ||
                     &token == hovered))
      continue;
    const std::string& name = source->getLastName();
    const ImVec2 size =
        ImGui::GetFont()->CalcTextSizeA(nameSize, FLT_MAX, 0.0f, name.c_str());
    const ImVec2 origin{token.position.x - size.x * 0.5f,
                        token.position.y + radius * 1.35f + px(2.0f)};
    const float padding = px(3.0f);
    drawList->AddRectFilled(
        {origin.x - padding, origin.y - padding * 0.4f},
        {origin.x + size.x + padding, origin.y + size.y + padding * 0.4f},
        T::LABEL_BACK_COLOR, px(3.0f));
    drawList->AddText(ImGui::GetFont(), nameSize, origin, T::LABEL_TEXT_COLOR,
                      name.c_str());
  }

  if (hovered && hovered->player->player)
  {
    const MatchRenderPlayer& player = *hovered->player;
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(player.player->getName().c_str());
    ImGui::TextUnformatted(
        fmt::sprintf(
            LOC("MATCH_PLAYER_TOOLTIP"),
            RoleUtils::shortName(player.player->getRole()),
            playerIntentLabel(player.intent),
            static_cast<double>(player.stamina *
                                MatchSceneTuning::Scoreboard::PERCENT_SCALE),
            static_cast<double>(player.speedMetresPerSecond))
            .c_str());
    ImGui::EndTooltip();
  }
}

void MatchRenderer2D::State::drawBall()
{
  using B = Tuning::Ball;
  const float height = ball.height;
  // Ground shadow, sliding away from the sun and softening with height.
  const float shadowRadius =
      std::max(px(B::MIN_RADIUS_PIXELS), B::RADIUS_METRES * ppm) *
      (1.0f + height * B::SHADOW_GROWTH);
  const auto shadowAlpha = static_cast<std::uint8_t>(
      static_cast<float>(B::SHADOW_ALPHA) / (1.0f + height * B::SHADOW_FADE));
  const ImVec2 shadow = atMetres(ball.x + height * B::SHADOW_SLIDE,
                                 ball.y + height * B::SHADOW_SLIDE * 0.6f);
  drawList->AddCircleFilled(shadow, shadowRadius * 1.35f,
                            IM_COL32(0, 0, 0, shadowAlpha / 3));
  drawList->AddCircleFilled(shadow, shadowRadius,
                            IM_COL32(0, 0, 0, shadowAlpha));

  const ImVec2 at = atMetres(ball.x, ball.y - height * B::LIFT_SHARE);
  const float radius =
      std::max(px(B::MIN_RADIUS_PIXELS), B::RADIUS_METRES * ppm) *
      (1.0f + height * B::GROWTH_PER_METRE);
  drawList->AddCircleFilled(at, radius + std::max(1.0f, radius * 0.16f),
                            B::OUTLINE_COLOR);
  drawList->AddCircleFilled(at, radius, B::COLOR);
  if (radius >= px(3.0f))
  {
    // Panels rolling across the ball along its path.
    const ImVec2 along = ballDirection;
    const ImVec2 across{-along.y, along.x};
    for (int patch = 0; patch < 3; ++patch)
    {
      const float phase = ballSpin + TWO_PI * static_cast<float>(patch) / 3.0f;
      const float facing = std::cos(phase);
      if (facing < -0.15f) continue;
      const float shift = std::sin(phase) * radius * 0.55f;
      const float side = static_cast<float>(patch - 1) * radius * 0.3f;
      drawList->AddCircleFilled(
          {at.x + along.x * shift + across.x * side,
           at.y + along.y * shift + across.y * side},
          radius * 0.26f * (0.55f + 0.45f * std::max(facing, 0.0f)),
          B::PATCH_COLOR);
    }
  }
  drawList->AddCircleFilled({at.x - radius * 0.35f, at.y - radius * 0.38f},
                            radius * 0.24f, IM_COL32(255, 255, 255, 200));
}

const char* playerIntentLabel(PlayerIntent intent)
{
  using enum PlayerIntent;
  switch (intent)
  {
    case HOLD_SHAPE:
      return LOC("MATCH_INTENT_HOLD_SHAPE");
    case CARRY_BALL:
      return LOC("MATCH_INTENT_CARRY_BALL");
    case OFFER_SUPPORT:
      return LOC("MATCH_INTENT_OFFER_SUPPORT");
    case RECEIVE_PASS:
      return LOC("MATCH_INTENT_RECEIVE_PASS");
    case RUN_IN_BEHIND:
      return LOC("MATCH_INTENT_RUN_IN_BEHIND");
    case ATTACK_BOX:
      return LOC("MATCH_INTENT_ATTACK_BOX");
    case OVERLAP:
      return LOC("MATCH_INTENT_OVERLAP");
    case PRESS_BALL:
      return LOC("MATCH_INTENT_PRESS_BALL");
    case COVER_PRESS:
      return LOC("MATCH_INTENT_COVER_PRESS");
    case BLOCK_PASSING_LANE:
      return LOC("MATCH_INTENT_BLOCK_PASSING_LANE");
    case MARK_OPPONENT:
      return LOC("MATCH_INTENT_MARK_OPPONENT");
    case CLAIM_LOOSE_BALL:
      return LOC("MATCH_INTENT_CLAIM_LOOSE_BALL");
    case RECOVER_SHAPE:
      return LOC("MATCH_INTENT_RECOVER_SHAPE");
    case GOALKEEP:
      return LOC("MATCH_INTENT_GOALKEEP");
  }
  return "";
}

MatchViewport computeMatchViewport(float topLeftX, float topLeftY,
                                   float availableWidth, float availableHeight)
{
  // The pitch keeps its real 105 x 68 proportions at any size: fit the
  // available area and letterbox the rest (the caller centres it).
  constexpr float MIN_PIXELS_PER_METRE = 2.0f;
  const float pixelsPerMetreFit =
      std::max(MIN_PIXELS_PER_METRE,
               std::min(availableWidth / LENGTH, availableHeight / WIDTH));
  MatchViewport viewport;
  viewport.x = topLeftX;
  viewport.y = topLeftY;
  viewport.width = LENGTH * pixelsPerMetreFit;
  viewport.height = WIDTH * pixelsPerMetreFit;
  return viewport;
}
void drawGoalCelebration(ImDrawList& drawList, const MatchViewport& viewport,
                         int homeScore, int awayScore,
                         float celebrationRemaining, float celebrationDuration)
{
  const float total = celebrationDuration;
  const float progress =
      total > 0.0f ? std::clamp(1.0f - celebrationRemaining / total, 0.0f, 1.0f)
                   : 0.0f;
  // A fading golden flash behind the banner makes the goal unmistakable.
  const float pulse =
      0.5f +
      0.5f * std::sin(progress *
                      MatchSceneTuning::Celebration::PULSE_PERIOD_SECONDS *
                      MatchSceneTuning::CELEBRATION_RADIANS_PER_PERIOD);
  const float flashAlpha = MatchSceneTuning::Celebration::FLASH_MIN_ALPHA +
                           (MatchSceneTuning::Celebration::FLASH_MAX_ALPHA -
                            MatchSceneTuning::Celebration::FLASH_MIN_ALPHA) *
                               (1.0f - progress) * pulse;
  const ImU32 flashColor =
      IM_COL32(255, 255, 150, static_cast<unsigned int>(flashAlpha) & 0xFF);
  drawList.AddRectFilled(
      {viewport.x, viewport.y},
      {viewport.x + viewport.width, viewport.y + viewport.height}, flashColor,
      0.0f, ImDrawFlags_None);

  const float centerX =
      viewport.x + viewport.width * MatchSceneTuning::Pitch::CENTRE_RATIO;
  const float centerY =
      viewport.y + viewport.height * MatchSceneTuning::Pitch::CENTRE_RATIO;
  const float baseFontSize = ImGui::GetStyle().FontSizeBase;
  const std::string banner = LOC("MATCH_GOAL_BANNER");
  const std::string score = std::format("{} - {}", homeScore, awayScore);

  ImGui::PushFont(nullptr,
                  baseFontSize * MatchSceneTuning::Celebration::BANNER_SCALE);
  const ImVec2 bannerSize = ImGui::CalcTextSize(banner.c_str());
  drawList.AddText({centerX - bannerSize.x * 0.5f, centerY - bannerSize.y},
                   MatchSceneTuning::Celebration::BANNER_TEXT_COLOR,
                   banner.c_str());
  ImGui::PopFont();

  ImGui::PushFont(nullptr,
                  baseFontSize * MatchSceneTuning::Celebration::SCORE_SCALE);
  const ImVec2 scoreSize = ImGui::CalcTextSize(score.c_str());
  drawList.AddText(
      {centerX - scoreSize.x * 0.5f,
       centerY +
           bannerSize.y * MatchSceneTuning::Celebration::SCORE_OFFSET_RATIO},
      MatchSceneTuning::Celebration::SCORE_TEXT_COLOR, score.c_str());
  ImGui::PopFont();
}

MatchRenderer2D::MatchRenderer2D() : state(std::make_unique<State>()) {}

MatchRenderer2D::~MatchRenderer2D() = default;

bool MatchRenderer2D::projectPitch(Vector2F pitch, float /*heightMetres*/,
                                   float& screenX, float& screenY) const
{
  // Top-down: height does not move a point on the plan.
  const State& s = *state;
  if (s.ppm <= 0.0f || s.drawList == nullptr) return false;
  const ImVec2 point = s.atMetres(pitch.x * LENGTH, pitch.y * WIDTH);
  screenX = point.x;
  screenY = point.y;
  return true;
}

void MatchRenderer2D::render(const MatchRenderSnapshot& snapshot,
                             const MatchRenderOptions& options,
                             const MatchViewport& viewport)
{
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  if (!drawList || viewport.width < 2.0f || viewport.height < 2.0f) return;
  State& s = *state;
  s.drawList = drawList;
  s.whitePixel = ImGui::GetFontTexUvWhitePixel();
  s.viewport = viewport;
  s.ppm = viewport.width / LENGTH;
  s.uiScale = std::max(Theme::scale(), 0.5f);
  s.frameSeconds = std::clamp(options.frameSeconds, 0.0f, 0.1f);
  s.elapsedSeconds += s.frameSeconds;
  s.reducedMotion = Theme::reducedMotion();
  const float alpha = snapshot.interpolationAlpha;

  s.prepareMatch(snapshot);
  s.updateBall(snapshot, alpha);
  s.detectKick(snapshot);
  s.updateFlights(snapshot);
  s.updateEvents(snapshot);
  if (options.pressureOverlay)
    s.updatePressure(snapshot, alpha);
  else
    s.pressureValid = false;

  // The scene insets the pitch by this apron (scaled) around the view.
  const float apron = s.px(MatchSceneTuning::Stadium::APRON_WIDTH);
  s.drawStadium(apron);
  s.drawGrass();
  s.drawWear();
  if (options.pressureOverlay) s.drawPressure();
  s.drawMarkings();
  s.drawGoal(true);
  s.drawGoal(false);

#ifdef DEBUG
  if (options.showAiDebug)
  {
    for (const MatchRenderPlayer& player : snapshot.players)
    {
      const Vector2F interpolated = lerpRenderPosition(
          player.previousPosition, player.currentPosition, alpha);
      const ImVec2 position =
          s.atMetres(interpolated.x * LENGTH, interpolated.y * WIDTH);
      const ImVec2 target = s.atMetres(player.movementTarget.x * LENGTH,
                                       player.movementTarget.y * WIDTH);
      const ImU32 debugColor = intentDebugColor(player.intent);
      drawList->AddLine(position, target, debugColor,
                        MatchSceneTuning::Marker::DEBUG_LINE_THICKNESS);
      drawList->AddCircle(target, MatchSceneTuning::Marker::DEBUG_TARGET_RADIUS,
                          debugColor);
    }
  }
#endif

  s.drawOffside();
  s.drawFlights();
  s.drawTokens(snapshot, options, alpha);
  s.drawTrail();
  s.drawBall();

  if (snapshot.state == MatchState::GOAL)
  {
    drawGoalCelebration(*drawList, viewport, snapshot.homeScore,
                        snapshot.awayScore, snapshot.goalCelebrationRemaining,
                        snapshot.goalCelebrationDuration);
  }
}
