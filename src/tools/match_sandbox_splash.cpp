// Football Management Project — see LICENSE in the project root.
#include "tools/match_sandbox_splash.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <utility>

#include "global/paths.h"
#include "gui/widgets/theme.h"
#include "tools/match_sandbox_scene.h"
#include "tools/match_sandbox_typography.h"

namespace
{
constexpr float DESIGN_W = 1672.0f;
constexpr float DESIGN_H = 941.0f;
// Animate for 1.2 seconds, then hold before pulling the panels apart.
constexpr float HOLD_UNTIL_SECONDS = 2.6f;
constexpr float EXIT_SECONDS = 0.4f;
constexpr ImU32 BACKGROUND = IM_COL32(22, 19, 16, 255);
constexpr ImU32 PANEL = IM_COL32(42, 35, 28, 255);
constexpr ImU32 CREAM = IM_COL32(240, 234, 226, 255);
constexpr ImU32 MUTED = IM_COL32(160, 149, 138, 255);
constexpr ImU32 LIME = IM_COL32(155, 208, 107, 255);
constexpr ImU32 YELLOW = IM_COL32(230, 203, 63, 255);
constexpr ImU32 PITCH_LINE = IM_COL32(125, 109, 91, 255);

float reveal(float elapsed, float delay, float duration)
{
  const float t = std::clamp((elapsed - delay) / duration, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

ImU32 opacity(ImU32 color, float alpha)
{
  return (color & ~IM_COL32_A_MASK) |
         (static_cast<ImU32>(255.0f * std::clamp(alpha, 0.0f, 1.0f))
          << IM_COL32_A_SHIFT);
}
}  // namespace

SandboxSplashScene::SandboxSplashScene(GUIView* view,
                                       std::function<void()> load)
    : GUIScene(view), loader(std::move(load))
{
}

SandboxSplashScene::~SandboxSplashScene() = default;

void SandboxSplashScene::onEnter()
{
  if (!splash_font)
  {
    const auto path = AssetPaths::file("assets/fonts/Manrope-Bold.ttf");
    const auto fontConfig = SandboxTypography::manropeConfig();
    splash_font = ImGui::GetIO().Fonts->AddFontFromFileTTF(path.c_str(), 42.0f, &fontConfig);
  }
  SDL_SetWindowTitle(getWindow(), "Player12 match sandbox");
  SDL_MaximizeWindow(getWindow());
}

void SandboxSplashScene::handleEvent(const SDL_Event& event)
{
  // F12 remains available for screenshots without dismissing the splash.
  if ((event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
       event.key.key != SDLK_F12) ||
      event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
      event.type == SDL_EVENT_FINGER_DOWN)
    skip = true;
}

void SandboxSplashScene::update(float deltaTime)
{
  elapsed += deltaTime;
  // Present the first frame before beginning potentially expensive startup.
  if (!rendered) return;
  if (!loading.valid() && !ready)
    loading = std::async(std::launch::async, loader);
  if (!ready && loading.wait_for(std::chrono::seconds::zero()) ==
                    std::future_status::ready)
  {
    loading.get();  // Propagate startup failures to main's error handler.
    ready = true;
  }
  if (!ready) return;
  if (!skip && !Theme::reducedMotion() && elapsed < HOLD_UNTIL_SECONDS) return;
  if (skip || Theme::reducedMotion())
  {
    if (setup)
      exit_elapsed = EXIT_SECONDS;
    else
      changeScene(std::make_unique<MatchSandboxScene>(guiView));
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

void SandboxSplashScene::render()
{
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  if (setup)
  {
    // Show the destination at full brightness, but prevent clicks through the
    // opening panels from activating setup controls before the handoff.
    ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.0f);
    ImGui::BeginDisabled();
    setup->render();
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
  }
  ImDrawList* draw = ImGui::GetForegroundDrawList();
  const ImVec2 origin = viewport->Pos;
  const ImVec2 size = viewport->Size;
  const float scale = std::min(size.x / DESIGN_W, size.y / DESIGN_H);
  const ImVec2 offset(origin.x + (size.x - DESIGN_W * scale) * 0.5f,
                      origin.y + (size.y - DESIGN_H * scale) * 0.5f);
  const float seam = offset.x + 755.0f * scale;
  const float opening = reveal(exit_elapsed, 0.0f, EXIT_SECONDS);
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
    const auto point = [&](float x, float y)
    { return ImVec2(offset.x + x * scale + shift, offset.y + y * scale); };
    const bool still = Theme::reducedMotion() || skip;
    const auto color = [&](ImU32 value, float alpha = 1.0f)
    { return opacity(value, alpha); };
    const float pitchAlpha = still ? 1.0f : reveal(elapsed, 0.0f, 0.65f);
    const auto line = [&](float x1, float y1, float x2, float y2)
    {
      draw->AddLine(point(x1, y1), point(x2, y2), color(PITCH_LINE, pitchAlpha),
                    2.0f * scale);
    };
    const auto box = [&](float x1, float y1, float x2, float y2)
    {
      draw->AddRect(point(x1, y1), point(x2, y2), color(PITCH_LINE, pitchAlpha),
                    0.0f, 0, 2.0f * scale);
    };
    const auto arc = [&](float x, float y, float radius, float from, float to)
    {
      draw->PathArcTo(point(x, y), radius * scale, from, to, 48);
      draw->PathStroke(color(PITCH_LINE, pitchAlpha), 0, 2.0f * scale);
    };
    const auto text =
        [&](float x, float y, float fontSize, const char* value, ImU32 ink)
    {
      ImGui::PushFont(splash_font, fontSize * scale);
      draw->AddText(ImGui::GetFont(), fontSize * scale, point(x, y), color(ink),
                    value);
      ImGui::PopFont();
    };

    draw->AddRectFilled(ImVec2(origin.x + shift, origin.y),
                        ImVec2(origin.x + size.x + shift, origin.y + size.y),
                        BACKGROUND);
    draw->AddRectFilled(ImVec2(origin.x + shift, origin.y),
                        ImVec2(point(755.0f, 0.0f).x, origin.y + size.y),
                        color(PANEL));
    // Square, low-contrast blocks echo the reference's card geometry.
    draw->AddRectFilled(point(623, 0), point(755, 99),
                        color(IM_COL32(57, 47, 37, 255)));
    draw->AddRectFilled(point(573, 99), point(623, 149),
                        color(IM_COL32(57, 47, 37, 255)));
    draw->AddRectFilled(point(706, 852), point(755, 906),
                        color(IM_COL32(57, 47, 37, 255)));
    draw->AddRectFilled(point(656, 906), point(706, 941),
                        color(IM_COL32(57, 47, 37, 255)));

    text(72, 80, 170, "We", CREAM);
    text(72, 222, 170, "Go", CREAM);
    text(72, 364, 170, "Again", CREAM);
    // Measure the actual title font so the rule always spans Again exactly.
    ImGui::PushFont(splash_font, 170.0f * scale);
    const float titleWidth = ImGui::CalcTextSize("Again").x;
    ImGui::PopFont();
    draw->AddRectFilled(point(76, 571),
                        ImVec2(point(76, 581).x + titleWidth, point(76, 581).y),
                        color(LIME));
    text(76, 608, 50, "Match Engine Sandbox", CREAM);
    draw->AddRectFilled(point(76, 777), point(83, 853), color(PITCH_LINE));
    text(112, 771, 44, "Player 12", LIME);
    text(112, 826, 28, "An open source football manager game", MUTED);

    // Full pitch, with the side attacking towards the top goal.
    constexpr float PI = 3.14159265358979323846f;
    box(867, 57, 1587, 880);
    line(867, 460, 1587, 460);
    arc(1227, 460, 89, 0, 2 * PI);
    box(1048, 57, 1407, 181);
    box(1142, 57, 1312, 106);
    box(1176, 29, 1278, 57);
    box(1048, 761, 1407, 880);
    box(1142, 834, 1312, 880);
    box(1176, 880, 1278, 908);
    arc(1227, 143, 73, 0.55f, PI - 0.55f);
    arc(1227, 799, 73, PI + 0.55f, 2 * PI - 0.55f);
    arc(867, 57, 22, 0, PI / 2);
    arc(1587, 57, 22, PI / 2, PI);
    arc(867, 880, 22, -PI / 2, 0);
    arc(1587, 880, 22, PI, 1.5f * PI);
    for (float y : {143.0f, 460.0f, 799.0f})
      draw->AddCircleFilled(point(1227, y), 3.5f * scale,
                            color(PITCH_LINE, pitchAlpha));

    // GK; flat back four; DM; two CMs; LW, RW and striker.
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
    const float playersAlpha = still ? 1.0f : reveal(elapsed, 0.3f, 0.4f);
    for (const ImVec2 player : PLAYERS)
      draw->AddTriangleFilled(
          point(player.x, player.y - 25), point(player.x - 25, player.y + 19),
          point(player.x + 25, player.y + 19), color(LIME, playersAlpha));

    // A single long diagonal: LB to RW. Open head distinguishes it from
    // players.
    const ImVec2 start(987, 638);
    const ImVec2 end(1454, 310);
    const float dx = end.x - start.x;
    const float dy = end.y - start.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    const float ux = dx / length;
    const float uy = dy / length;
    const float progress = still ? 1.0f : reveal(elapsed, 0.7f, 0.5f);
    for (float distance = 0; distance < length * progress; distance += 21.0f)
    {
      const float to = std::min(distance + 13.0f, length * progress);
      draw->AddLine(point(start.x + ux * distance, start.y + uy * distance),
                    point(start.x + ux * to, start.y + uy * to), color(YELLOW),
                    3.0f * scale);
    }
    if (progress >= 1.0f)
      for (float side : {-1.0f, 1.0f})
        draw->AddLine(point(end.x, end.y),
                      point(end.x - ux * 25 + side * uy * 12,
                            end.y - uy * 25 - side * ux * 12),
                      color(YELLOW), 3.0f * scale);

    if (!ready && elapsed > 3.0f)
      text(112, 888, 20, "Preparing sandbox...", MUTED);
    draw->PopClipRect();
  }
  rendered = true;
  // Handoff after rendering the fully revealed destination, avoiding an empty
  // frame when ownership moves into GUIView's deferred scene change.
  if (setup && exit_elapsed >= EXIT_SECONDS) changeScene(std::move(setup));
}
