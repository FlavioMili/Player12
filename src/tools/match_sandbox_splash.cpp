// Football Management Project — see LICENSE in the project root.
#include "tools/match_sandbox_splash.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <numbers>
#include <utility>

#include "global/background_task.h"
#include "global/paths.h"
#include "gui/widgets/theme.h"
#include "tools/match_sandbox_scene.h"
#include "tools/match_sandbox_typography.h"

namespace
{
constexpr float DESIGN_W = 1672.0F;
constexpr float DESIGN_H = 941.0F;
// Animate for 1.2 seconds, then hold before pulling the panels apart.
constexpr float HOLD_UNTIL_SECONDS = 2.6F;
constexpr float EXIT_SECONDS = 0.4F;
constexpr ImU32 BACKGROUND = IM_COL32(22, 19, 16, 255);
constexpr ImU32 PANEL = IM_COL32(42, 35, 28, 255);
constexpr ImU32 CREAM = IM_COL32(240, 234, 226, 255);
constexpr ImU32 MUTED = IM_COL32(160, 149, 138, 255);
constexpr ImU32 LIME = IM_COL32(155, 208, 107, 255);
constexpr ImU32 YELLOW = IM_COL32(230, 203, 63, 255);
constexpr ImU32 PITCH_LINE = IM_COL32(125, 109, 91, 255);
constexpr ImU32 BLOCK = IM_COL32(57, 47, 37, 255);
constexpr float MAX_ALPHA = 255.0F;
constexpr float CENTRE = 0.5F;
constexpr float SPLASH_FONT_SIZE = 42.0F;
constexpr float PI = std::numbers::pi_v<float>;

// The artwork, in design-canvas pixels (DESIGN_W x DESIGN_H).
/** Left, top, right, bottom. */
using Box = std::array<float, 4>;
/** Centre x and y, radius, from and to (radians). */
constexpr std::size_t ARC_FIELDS = 5;
using Arc = std::array<float, ARC_FIELDS>;
struct Words
{
  float x = 0.0F;
  float y = 0.0F;
  float size = 0.0F;
  const char* value = "";
  ImU32 ink = 0;
};

/** The left panel's right edge, where the two halves part. */
constexpr float SEAM_X = 755.0F;
/** Square, low-contrast blocks echo the reference's card geometry. */
constexpr std::array<Box, 4> BLOCKS{{{623, 0, 755, 99},
                                     {573, 99, 623, 149},
                                     {706, 852, 755, 906},
                                     {656, 906, 706, 941}}};
constexpr float TITLE_SIZE = 170.0F;
constexpr std::array<Words, 3> TITLE{
    {{.x = 72, .y = 80, .size = TITLE_SIZE, .value = "We", .ink = CREAM},
     {.x = 72, .y = 222, .size = TITLE_SIZE, .value = "Go", .ink = CREAM},
     {.x = 72, .y = 364, .size = TITLE_SIZE, .value = "Again", .ink = CREAM}}};
/** The lime rule under the title: its left, top and bottom (it spans
 * "Again"). */
constexpr float TITLE_RULE_LEFT = 76.0F;
constexpr float TITLE_RULE_TOP = 571.0F;
constexpr float TITLE_RULE_BOTTOM = 581.0F;
constexpr std::array<Words, 3> CAPTIONS{
    {{.x = 76,
      .y = 608,
      .size = 50,
      .value = "Match Engine Sandbox",
      .ink = CREAM},
     {.x = 112, .y = 771, .size = 44, .value = "Player 12", .ink = LIME},
     {.x = 112,
      .y = 826,
      .size = 28,
      .value = "An open source football manager game",
      .ink = MUTED}}};
constexpr Box NAME_BAR{76, 777, 83, 853};
constexpr Words PREPARING{.x = 112,
                          .y = 888,
                          .size = 20,
                          .value = "Preparing sandbox...",
                          .ink = MUTED};
constexpr float PREPARING_AFTER_SECONDS = 3.0F;

// Full pitch, with the side attacking towards the top goal.
constexpr std::array<Box, 7> PITCH_BOXES{{{867, 57, 1587, 880},
                                          {1048, 57, 1407, 181},
                                          {1142, 57, 1312, 106},
                                          {1176, 29, 1278, 57},
                                          {1048, 761, 1407, 880},
                                          {1142, 834, 1312, 880},
                                          {1176, 880, 1278, 908}}};
constexpr Box HALFWAY_LINE{867, 460, 1587, 460};
/** The penalty arcs leave out this much of their circle (radians). */
constexpr float ARC_CUT = 0.55F;
constexpr std::array<Arc, 7> PITCH_ARCS{
    {{1227, 460, 89, 0, 2 * PI},
     {1227, 143, 73, ARC_CUT, PI - ARC_CUT},
     {1227, 799, 73, PI + ARC_CUT, (2 * PI) - ARC_CUT},
     {867, 57, 22, 0, PI / 2},
     {1587, 57, 22, PI / 2, PI},
     {867, 880, 22, -PI / 2, 0},
     {1587, 880, 22, PI, 1.5F * PI}}};
constexpr float SPOT_X = 1227.0F;
constexpr std::array<float, 3> SPOT_YS{143.0F, 460.0F, 799.0F};
constexpr float SPOT_RADIUS = 3.5F;
constexpr float PITCH_LINE_THICKNESS = 2.0F;
constexpr int ARC_SEGMENTS = 48;

/** GK; flat back four; DM; two CMs; LW, RW and striker. Each is a
 * triangle: its top this far above the point, its base this far below and
 * this wide either side. */
constexpr std::array<ImVec2, 11> PLAYERS{{{1227, 850},
                                          {970, 670},
                                          {1140, 670},
                                          {1315, 670},
                                          {1485, 670},
                                          {1227, 550},
                                          {1115, 425},
                                          {1340, 425},
                                          {975, 285},
                                          {1480, 285},
                                          {1227, 225}}};
constexpr float PLAYER_TOP = 25.0F;
constexpr float PLAYER_BASE = 19.0F;
constexpr float PLAYER_HALF_WIDTH = 25.0F;

/** A single long diagonal, LB to RW, dashed; its open head (this far back
 * and to each side) tells it apart from the players. */
constexpr ImVec2 PASS_START{987, 638};
constexpr ImVec2 PASS_END{1454, 310};
constexpr float DASH_EVERY = 21.0F;
constexpr float DASH_LENGTH = 13.0F;
constexpr float PASS_THICKNESS = 3.0F;
constexpr float HEAD_BACK = 25.0F;
constexpr float HEAD_SIDE = 12.0F;

/** The reveal: the pitch, then the players, then the pass (delay and
 * duration, s). */
constexpr float PITCH_REVEAL_SECONDS = 0.65F;
constexpr float PLAYERS_DELAY_SECONDS = 0.3F;
constexpr float PLAYERS_REVEAL_SECONDS = 0.4F;
constexpr float PASS_DELAY_SECONDS = 0.7F;
constexpr float PASS_REVEAL_SECONDS = 0.5F;

/** Smoothstep, 3t^2 - 2t^3. */
constexpr float SMOOTHSTEP_SQUARE = 3.0F;
constexpr float SMOOTHSTEP_CUBE = 2.0F;

float reveal(float elapsed, float delay, float duration)
{
  const float t = std::clamp((elapsed - delay) / duration, 0.0F, 1.0F);
  return t * t * (SMOOTHSTEP_SQUARE - (SMOOTHSTEP_CUBE * t));
}

ImU32 opacity(ImU32 color, float alpha)
{
  return (color & ~IM_COL32_A_MASK) |
         (static_cast<ImU32>(MAX_ALPHA * std::clamp(alpha, 0.0F, 1.0F))
          << IM_COL32_A_SHIFT);
}
}  // namespace

SandboxSplashScene::SandboxSplashScene(GUIView* view,
                                       std::function<void(std::stop_token)> load)
    : GUIScene(view), loader(std::move(load))
{
}

SandboxSplashScene::~SandboxSplashScene() = default;

void SandboxSplashScene::onEnter()
{
  if (splash_font == nullptr)
  {
    const auto path = AssetPaths::file("assets/fonts/Manrope-Bold.ttf");
    const auto fontConfig = SandboxTypography::manropeConfig();
    splash_font = ImGui::GetIO().Fonts->AddFontFromFileTTF(
        path.c_str(), SPLASH_FONT_SIZE, &fontConfig);
  }
  SDL_SetWindowTitle(getWindow(), "Player12 match sandbox");
  SDL_MaximizeWindow(getWindow());
}

void SandboxSplashScene::handleEvent(const SDL_Event& event)
{
  if (event.type == SDL_EVENT_QUIT)
  {
    closing = true;
    loading_worker.request_stop();
    return;
  }
  // F12 remains available for screenshots without dismissing the splash.
  if ((event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
       event.key.key != SDLK_F12) ||
      event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
      event.type == SDL_EVENT_FINGER_DOWN)
  {
    skip = true;
  }
}

void SandboxSplashScene::update(float deltaTime)
{
  elapsed += deltaTime;
  // Present the first frame before beginning potentially expensive startup.
  if (closing || !rendered)
  {
    return;
  }
  if (!loading.valid() && !ready)
  {
    loading = launchBackgroundTask(loading_worker, loader);
  }
  if (!ready && loading.wait_for(std::chrono::seconds::zero()) ==
                    std::future_status::ready)
  {
    loading.get();  // Propagate startup failures to main's error handler.
    ready = true;
  }
  if (!ready)
  {
    return;
  }
  if (!skip && !Theme::reducedMotion() && elapsed < HOLD_UNTIL_SECONDS)
  {
    return;
  }
  if (skip || Theme::reducedMotion())
  {
    if (setup)
    {
      exit_elapsed = EXIT_SECONDS;
    }
    else
    {
      changeScene(std::make_unique<MatchSandboxScene>(guiView));
    }
    return;
  }
  if (!setup)
  {
    setup = std::make_unique<MatchSandboxScene>(guiView);
    // Prepare the actual destination before revealing it. Its onEnter is
    // idempotent, so the normal scene handoff can enter it again safely.
    setup->onEnter();
  }
  exit_elapsed += deltaTime;
}

namespace
{
/** Where the splash's animations have got to, for one frame. */
struct SplashTiming
{
  float elapsed = 0.0F;
  bool still = false;
  bool ready = false;
};

/** Where a half's design goes: its corner, its scale, and how far the
 * panels have slid it aside. */
struct SplashPlacement
{
  ImVec2 offset;
  float scale = 1.0F;
  float shift = 0.0F;
};

/** One half of the opening panels: the design drawn at its scale, shifted
 * aside as the panels open. */
class SplashHalf
{
 public:
  SplashHalf(ImDrawList* drawList, const SplashPlacement& placement,
             ImFont* titleFont)
      : draw(drawList),
        offset(placement.offset),
        scale(placement.scale),
        shift(placement.shift),
        font(titleFont)
  {
  }

  void drawPanel(ImVec2 origin, ImVec2 size) const
  {
    draw->AddRectFilled(ImVec2(origin.x + shift, origin.y),
                        ImVec2(origin.x + size.x + shift, origin.y + size.y),
                        BACKGROUND);
    draw->AddRectFilled(ImVec2(origin.x + shift, origin.y),
                        ImVec2(point(SEAM_X, 0.0F).x, origin.y + size.y),
                        opacity(PANEL, 1.0F));
    for (const auto& [x1, y1, x2, y2] : BLOCKS)
    {
      draw->AddRectFilled(point(x1, y1), point(x2, y2), opacity(BLOCK, 1.0F));
    }
  }

  void drawTitle() const
  {
    for (const Words& words : TITLE)
    {
      text(words);
    }
    // Measure the actual title font so the rule always spans Again exactly.
    ImGui::PushFont(font, TITLE_SIZE * scale);
    const float titleWidth = ImGui::CalcTextSize(TITLE.back().value).x;
    ImGui::PopFont();
    const ImVec2 ruleEnd = point(TITLE_RULE_LEFT, TITLE_RULE_BOTTOM);
    draw->AddRectFilled(point(TITLE_RULE_LEFT, TITLE_RULE_TOP),
                        ImVec2(ruleEnd.x + titleWidth, ruleEnd.y),
                        opacity(LIME, 1.0F));
    text(CAPTIONS[0]);
    const auto [x1, y1, x2, y2] = NAME_BAR;
    draw->AddRectFilled(point(x1, y1), point(x2, y2),
                        opacity(PITCH_LINE, 1.0F));
    text(CAPTIONS[1]);
    text(CAPTIONS[2]);
  }

  /** The outline, halfway line and centre circle, then the boxes, arcs and
   * spots. */
  void drawPitch(float alpha) const
  {
    const ImU32 ink = opacity(PITCH_LINE, alpha);
    box(PITCH_BOXES.front(), ink);
    const auto [x1, y1, x2, y2] = HALFWAY_LINE;
    draw->AddLine(point(x1, y1), point(x2, y2), ink,
                  PITCH_LINE_THICKNESS * scale);
    arc(PITCH_ARCS.front(), ink);
    for (std::size_t index = 1; index < PITCH_BOXES.size(); ++index)
    {
      box(PITCH_BOXES.at(index), ink);
    }
    for (std::size_t index = 1; index < PITCH_ARCS.size(); ++index)
    {
      arc(PITCH_ARCS.at(index), ink);
    }
    for (const float y : SPOT_YS)
    {
      draw->AddCircleFilled(point(SPOT_X, y), SPOT_RADIUS * scale, ink);
    }
  }

  void drawPlayers(float alpha) const
  {
    for (const ImVec2 player : PLAYERS)
    {
      draw->AddTriangleFilled(
          point(player.x, player.y - PLAYER_TOP),
          point(player.x - PLAYER_HALF_WIDTH, player.y + PLAYER_BASE),
          point(player.x + PLAYER_HALF_WIDTH, player.y + PLAYER_BASE),
          opacity(LIME, alpha));
    }
  }

  /** The pass, dashed out as far as it has got; its head once it is all
   * there. */
  void drawPass(float progress) const
  {
    const ImVec2 start = PASS_START;
    const ImVec2 end = PASS_END;
    const float dx = end.x - start.x;
    const float dy = end.y - start.y;
    const float length = std::sqrt((dx * dx) + (dy * dy));
    const float ux = dx / length;
    const float uy = dy / length;
    const int dashes =
        static_cast<int>(std::ceil(length * progress / DASH_EVERY));
    for (int dash = 0; dash < dashes; ++dash)
    {
      const float distance = static_cast<float>(dash) * DASH_EVERY;
      const float to = std::min(distance + DASH_LENGTH, length * progress);
      draw->AddLine(point(start.x + (ux * distance), start.y + (uy * distance)),
                    point(start.x + (ux * to), start.y + (uy * to)),
                    opacity(YELLOW, 1.0F), PASS_THICKNESS * scale);
    }
    if (progress < 1.0F)
    {
      return;
    }
    for (float side : {-1.0F, 1.0F})
    {
      draw->AddLine(point(end.x, end.y),
                    point(end.x - (ux * HEAD_BACK) + (side * uy * HEAD_SIDE),
                          end.y - (uy * HEAD_BACK) - (side * ux * HEAD_SIDE)),
                    opacity(YELLOW, 1.0F), PASS_THICKNESS * scale);
    }
  }

  void text(const Words& words) const
  {
    ImGui::PushFont(font, words.size * scale);
    draw->AddText(ImGui::GetFont(), words.size * scale, point(words.x, words.y),
                  opacity(words.ink, 1.0F), words.value);
    ImGui::PopFont();
  }

 private:
  /** A design point on the screen. */
  // Argument order: x then y, as points go.
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  [[nodiscard]] ImVec2 point(float x, float y) const
  {
    return {offset.x + (x * scale) + shift, offset.y + (y * scale)};
  }

  void box(const Box& at, ImU32 ink) const
  {
    const auto [x1, y1, x2, y2] = at;
    draw->AddRect(point(x1, y1), point(x2, y2), ink, 0.0F, 0,
                  PITCH_LINE_THICKNESS * scale);
  }

  void arc(const Arc& at, ImU32 ink) const
  {
    const auto [x, y, radius, from, to] = at;
    draw->PathArcTo(point(x, y), radius * scale, from, to, ARC_SEGMENTS);
    draw->PathStroke(ink, 0, PITCH_LINE_THICKNESS * scale);
  }

  ImDrawList* draw;
  ImVec2 offset;
  float scale;
  float shift;
  ImFont* font;
};

/** Everything on one half, revealed as far as the timing has got. */
void drawSplashHalf(const SplashHalf& half, const SplashTiming& timing,
                    ImVec2 origin, ImVec2 size)
{
  half.drawPanel(origin, size);
  half.drawTitle();
  half.drawPitch(
      timing.still ? 1.0F : reveal(timing.elapsed, 0.0F, PITCH_REVEAL_SECONDS));
  half.drawPlayers(timing.still ? 1.0F
                                : reveal(timing.elapsed, PLAYERS_DELAY_SECONDS,
                                         PLAYERS_REVEAL_SECONDS));
  half.drawPass(timing.still ? 1.0F
                             : reveal(timing.elapsed, PASS_DELAY_SECONDS,
                                      PASS_REVEAL_SECONDS));
  if (!timing.ready && timing.elapsed > PREPARING_AFTER_SECONDS)
  {
    half.text(PREPARING);
  }
}
}  // namespace

void SandboxSplashScene::render()
{
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  if (setup)
  {
    // Show the destination at full brightness, but prevent clicks through the
    // opening panels from activating setup controls before the handoff.
    ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.0F);
    ImGui::BeginDisabled();
    setup->render();
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
  }
  ImDrawList* draw = ImGui::GetForegroundDrawList();
  const ImVec2 origin = viewport->Pos;
  const ImVec2 size = viewport->Size;
  const float scale = std::min(size.x / DESIGN_W, size.y / DESIGN_H);
  const ImVec2 offset(origin.x + ((size.x - DESIGN_W * scale) * CENTRE),
                      origin.y + ((size.y - DESIGN_H * scale) * CENTRE));
  const float seam = offset.x + (SEAM_X * scale);
  const float opening = reveal(exit_elapsed, 0.0F, EXIT_SECONDS);
  const SplashTiming timing{.elapsed = elapsed,
                            .still = Theme::reducedMotion() || skip,
                            .ready = ready};
  for (int half = 0; half < 2; ++half)
  {
    const float shift = half == 0 ? -(seam - origin.x) * opening
                                  : (origin.x + size.x - seam) * opening;
    const float clipLeft = half == 0 ? origin.x : seam;
    const float clipRight = half == 0 ? seam : origin.x + size.x;
    draw->PushClipRect(ImVec2(std::max(origin.x, clipLeft + shift), origin.y),
                       ImVec2(std::min(origin.x + size.x, clipRight + shift),
                              origin.y + size.y),
                       true);
    drawSplashHalf(
        SplashHalf(draw, {.offset = offset, .scale = scale, .shift = shift},
                   splash_font),
        timing, origin, size);
    draw->PopClipRect();
  }
  rendered = true;
  // Handoff after rendering the fully revealed destination, avoiding an empty
  // frame when ownership moves into GUIView's deferred scene change.
  if (setup && exit_elapsed >= EXIT_SECONDS)
  {
    changeScene(std::move(setup));
  }
}
