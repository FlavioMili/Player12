// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_true_view.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>
#include <string>
#include <string_view>

#include "global/paths.h"
#include "gui/gui_view.h"
#include "gui/render/match_render_math.h"
#include "gui/widgets/text.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/match_body_shape_binturong.h"
#include "model/match_derived_stats.h"
#include "tools/match_sandbox_page_ui.h"
#include "tools/match_sandbox_tuning.h"

namespace
{
using RenderMath::Vec3;

constexpr float PI = std::numbers::pi_v<float>;
/** Pauses either side of an animated move (s). */
constexpr float REST_SECONDS = 0.6F;
constexpr float DEGREES_PER_HALF_TURN = 180.0F;
constexpr float FULL_TURN = 2.0F * PI;
constexpr float HALF = 0.5F;
constexpr float MAX_CHANNEL = 255.0F;
/** The grass grid: 3 m each way, a faint brown. */
constexpr int GRID_METRES = 3;
constexpr float GRID_EXTENT = static_cast<float>(GRID_METRES);
constexpr float GRID_ALPHA = 0.35F;
/** A roll goes out and comes back. */
constexpr float ROLL_LEGS = 2.0F;
/** Camera presets and the orbit (radians, m, per pixel of drag). */
constexpr float LEVEL_PITCH = 0.05F;
constexpr float TOP_PITCH = 1.5F;
constexpr float DRAG_PER_PIXEL = 0.01F;
constexpr float MIN_PITCH = -0.2F;
constexpr float MAX_PITCH = 1.55F;
constexpr float MIN_TARGET_HEIGHT = -0.5F;
constexpr float MAX_TARGET_HEIGHT = 3.0F;
constexpr float ZOOM_PER_WHEEL_TICK = 0.9F;
constexpr float MIN_DISTANCE = 1.0F;
constexpr float MAX_DISTANCE = 25.0F;
/** Controls: slider limits (degrees, playback speed), layout (pixels). */
constexpr float MAX_FACING_DEGREES = 180.0F;
constexpr float MAX_LYING_DEGREES = 90.0F;
constexpr float MIN_PLAYBACK_SPEED = 0.05F;
constexpr float FIELD_PADDING_X = 8.0F;
constexpr float FIELD_PADDING_Y = 4.0F;
constexpr float FIELD_CELL_PADDING = 3.0F;
constexpr float TWO_COLUMN_FIELDS_WIDTH = 360.0F;
/** The caption and get-up bar over the canvas (pixels). */
constexpr float CAPTION_LEFT = 10.0F;
constexpr float CAPTION_TOP = 8.0F;
constexpr float CAPTION_MARGINS = 20.0F;
constexpr float CAPTION_LINE_GAP = 6.0F;
constexpr float BAR_HEIGHT = 8.0F;
constexpr float BAR_MAX_WIDTH = 200.0F;

float radians(float degrees) { return degrees * PI / DEGREES_PER_HALF_TURN; }

/** A stat (1-99) as the Binturong handshake's rating (the stat / 100). */
float rating(int stat) { return static_cast<float>(stat) / 100.0F; }

ImU32 colour(int r, int g, int b, float alpha)
{
  return IM_COL32(
      r, g, b, static_cast<int>(std::clamp(alpha, 0.0F, 1.0F) * MAX_CHANNEL));
}

struct Rgb
{
  int r = 0;
  int g = 0;
  int b = 0;
};
constexpr Rgb GRID_RGB{.r = 125, .g = 109, .b = 91};

ImU32 colour(Rgb rgb, float alpha)
{
  return colour(rgb.r, rgb.g, rgb.b, alpha);
}

constexpr std::array<const char*, 7> POSE_NAMES{
    "Standing",   "Standing jump", "Running jump",         "Fallen",
    "Getting up", "Stumbling",     "Planted (thigh touch)"};
}  // namespace

TrueViewScene::TrueViewScene(GUIView* guiView_ptr) : GUIScene(guiView_ptr) {}

SceneID TrueViewScene::getID() const { return SceneID::MATCH_SANDBOX; }

void TrueViewScene::onEnter()
{
  // Reuse the launch page's fonts. Direct tool entry loads them once into
  // the current atlas, so reopening True View does not add font sources.
  auto* atlas = ImGui::GetIO().Fonts;
  const auto font = [&](const char* file, float size)
  {
    const auto config = SandboxTypography::manropeConfig();
    for (auto* existing : atlas->Fonts)
    {
      if (std::string_view(existing->GetDebugName()).find(file) !=
              std::string_view::npos &&
          !existing->Sources.empty() &&
          existing->Sources[0]->ExtraSizeScale == config.ExtraSizeScale)
      {
        return existing;
      }
    }
    return atlas->AddFontFromFileTTF(
        AssetPaths::file((std::string("assets/fonts/") + file).c_str()).c_str(),
        size, &config);
  };
  regularFont = font("Manrope-Regular.ttf", SandboxPageUI::REGULAR_FONT_SIZE);
  boldFont = font("Manrope-Bold.ttf", SandboxPageUI::BOLD_FONT_SIZE);
}

void TrueViewScene::update(float deltaTime)
{
  if (playing)
  {
    clock += deltaTime * playbackSpeed;
  }
}

void TrueViewScene::render()
{
  namespace Page = SandboxPageUI;
  const auto* viewport = ImGui::GetMainViewport();
  const float scale = Theme::scale();
  const Page::Style style(regularFont, scale);
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, Page::PAGE);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(SandboxPageUI::PAGE_PADDING * scale,
                             SandboxPageUI::PAGE_PADDING * scale));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
  ImGui::Begin("##true_view", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor();
  const ImVec2 available = ImGui::GetContentRegionAvail();
  const bool columns = available.x >= 1000 * scale;
  const float gutter =
      (SandboxPageUI::PANEL_GAP * scale) + ImGui::GetStyle().ScrollbarSize;
  const float panel =
      columns ? (available.x >= 1400 * scale ? 440.0F : 400.0F) * scale
              : std::max(scale, available.x - gutter);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  if (columns)
  {
    const ImVec2 origin = ImGui::GetCursorPos();
    ImGui::SetCursorPos(
        ImVec2(origin.x + panel + gutter + (SandboxPageUI::PANEL_GAP * scale),
               origin.y));
    // Build this frame's measures with the model, before the values tab reads
    // them.
    renderCanvas(ImVec2(
        available.x - panel - gutter - (SandboxPageUI::PANEL_GAP * scale),
        available.y));
    ImGui::SetCursorPos(origin);
    ImGui::BeginChild("##true_view_controls",
                      ImVec2(panel + gutter, available.y),
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    renderPanel(panel);
    ImGui::EndChild();
  }
  else
  {
    ImGui::BeginChild("##true_view_controls", available,
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    renderCanvas(ImVec2(
        panel, std::max(SandboxPageUI::MIN_CANVAS_HEIGHT * scale,
                        available.y * SandboxPageUI::NARROW_CANVAS_SHARE)));
    renderPanel(panel);
    ImGui::EndChild();
  }
  ImGui::PopStyleVar();
  ImGui::End();
}

namespace
{
/** A control's label above it, the control full width. */
void trueViewField(const char* label)
{
  UI::textWrapped("{}", label);
  ImGui::SetNextItemWidth(-1);
}
}  // namespace

/** Back, and the camera's presets. */
void TrueViewScene::renderViewButtons(float width, float scale)
{
  namespace Page = SandboxPageUI;
  Page::beginCard("##true_view_navigation", "VIEW", width, Page::DARK, scale,
                  boldFont);
  if (Page::button("Back to sandbox", false, scale, boldFont))
  {
    guiView->popScene();
  }
  if (Page::button("3/4", false, scale, boldFont))
  {
    camera = Camera{};
  }
  ImGui::SameLine(0, SandboxPageUI::PANEL_GAP * scale);
  if (Page::button("Side", false, scale, boldFont))
  {
    camera.yaw = 0;
    camera.pitch = LEVEL_PITCH;
  }
  ImGui::SameLine(0, SandboxPageUI::PANEL_GAP * scale);
  if (Page::button("Front", false, scale, boldFont))
  {
    camera.yaw = PI * HALF;
    camera.pitch = LEVEL_PITCH;
  }
  ImGui::SameLine(0, SandboxPageUI::PANEL_GAP * scale);
  if (Page::button("Top", false, scale, boldFont))
  {
    camera.pitch = TOP_PITCH;
  }
  Page::endCard(scale);
}

/** Controls or the engine's values; the controls' tabs. */
void TrueViewScene::renderDetailTabs(float width, float scale)
{
  namespace Page = SandboxPageUI;
  Page::beginCard("##true_view_tabs", "DETAILS", width, Page::MID, scale,
                  boldFont);
  if (Page::button("Controls", !showValues, scale, boldFont))
  {
    showValues = false;
  }
  ImGui::SameLine(0, SandboxPageUI::PANEL_GAP * scale);
  if (Page::button("Engine values", showValues, scale, boldFont))
  {
    showValues = true;
  }
  if (!showValues)
  {
    const std::array<const char*, 4> tabs{"Player", "Pose", "Technique",
                                          "Layers"};
    for (std::size_t index = 0; index < tabs.size(); ++index)
    {
      if (index > 0)
      {
        ImGui::SameLine(0, SandboxPageUI::PANEL_GAP * scale);
      }
      const auto tab = static_cast<ControlTab>(index);
      if (Page::button(tabs.at(index), controlTab == tab, scale, boldFont))
      {
        controlTab = tab;
      }
    }
  }
  Page::endCard(scale);
}

/** His measures and skills, his stronger foot and his facing. */
void TrueViewScene::renderPlayerControls(float width, float scale)
{
  namespace Page = SandboxPageUI;
  Page::beginCard("##true_view_player", "PLAYER", width, Page::LIGHT, scale,
                  boldFont);
  struct StatField
  {
    const char* label;
    int Stats::* value;
    int min, max;
    const char* tip;
  };
  const std::array<StatField, 7> fields{
      {{.label = "Height (cm)",
        .value = &Stats::heightCentimetres,
        .min = 150,
        .max = 210,
        .tip = nullptr},
       {.label = "Jump",
        .value = &Stats::jump,
        .min = 1,
        .max = 99,
        .tip = "Jumping reach's blend: the jump on top of his height (Pace and "
               "Physicality stand in until the game carries it)."},
       {.label = "Strength",
        .value = &Stats::strength,
        .min = 1,
        .max = 99,
        .tip = nullptr},
       {.label = "Agility",
        .value = &Stats::agility,
        .min = 1,
        .max = 99,
        .tip = nullptr},
       {.label = "Dribbling",
        .value = &Stats::dribbling,
        .min = 1,
        .max = 99,
        .tip = nullptr},
       {.label = "Defending",
        .value = &Stats::defending,
        .min = 1,
        .max = 99,
        .tip = nullptr},
       {.label = "Physicality",
        .value = &Stats::physicality,
        .min = 1,
        .max = 99,
        .tip = "Only the old shot-block rule reads it here."}}};
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                      ImVec2(FIELD_PADDING_X * scale, FIELD_PADDING_Y * scale));
  ImGui::PushStyleVar(
      ImGuiStyleVar_CellPadding,
      ImVec2(FIELD_CELL_PADDING * scale, FIELD_CELL_PADDING * scale));
  if (ImGui::BeginTable(
          "##player_fields", width >= TWO_COLUMN_FIELDS_WIDTH * scale ? 2 : 1,
          ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_RowBg |
              ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_BordersInnerV))
  {
    for (const auto& stat : fields)
    {
      ImGui::TableNextColumn();
      ImGui::PushID(stat.label);
      ImGui::Dummy(ImVec2(0, 0));
      trueViewField(stat.label);
      const bool tip = stat.tip != nullptr;
      if (tip && ImGui::IsItemHovered())
      {
        UI::tooltip("{}", stat.tip);
      }
      ImGui::SliderInt("##value", &(stats.*stat.value), stat.min, stat.max);
      if (tip && ImGui::IsItemHovered())
      {
        UI::tooltip("{}", stat.tip);
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  ImGui::PopStyleVar(2);
  if (Page::button("Right-footed", stats.rightFooted, scale, boldFont))
  {
    stats.rightFooted = true;
  }
  ImGui::SameLine(0, SandboxPageUI::PANEL_GAP * scale);
  if (Page::button("Left-footed", !stats.rightFooted, scale, boldFont))
  {
    stats.rightFooted = false;
  }
  trueViewField("Facing");
  ImGui::SliderFloat("##facing", &stats.facing, -MAX_FACING_DEGREES,
                     MAX_FACING_DEGREES, "%.0f deg");
  Page::endCard(scale);
}

/** The pose, its options, and its playback. */
void TrueViewScene::renderPoseControls(float width, float scale)
{
  namespace Page = SandboxPageUI;
  Page::beginCard("##true_view_pose", "POSE & PLAYBACK", width, Page::MID,
                  scale, boldFont);
  trueViewField("Pose");
  int poseIndex = static_cast<int>(pose);
  if (SandboxPageUI::combo("##pose", &poseIndex, POSE_NAMES.data(),
                           static_cast<int>(POSE_NAMES.size())))
  {
    pose = static_cast<Pose>(poseIndex);
    clock = 0;
  }
  if (pose == Pose::STANDING || pose == Pose::STUMBLING)
  {
    ImGui::Checkbox("Moving faster than his thigh allows", &moving);
  }
  if (pose == Pose::FALLEN || pose == Pose::GETTING_UP)
  {
    trueViewField("Lying");
    ImGui::SliderFloat("##lying", &lyingOffset, -MAX_LYING_DEGREES,
                       MAX_LYING_DEGREES, "%.0f deg off facing");
    ImGui::Checkbox("Fell on his front", &fellOnFront);
  }
  if (pose == Pose::FALLEN)
  {
    ImGui::Checkbox("Rolling about (big hit)", &rolling);
  }
  if (pose != Pose::STANDING && pose != Pose::PLANTED &&
      pose != Pose::STUMBLING)
  {
    ImGui::Checkbox("Play", &playing);
    ImGui::SameLine(0, SandboxPageUI::PANEL_GAP * scale);
    if (Page::button("Restart", false, scale, boldFont))
    {
      clock = 0;
    }
    trueViewField("Speed");
    ImGui::SliderFloat("##playback_speed", &playbackSpeed, MIN_PLAYBACK_SPEED,
                       1, "%.2fx");
  }
  Page::endCard(scale);
}

/** The technique picked out, and for a foot strike, which strike. */
void TrueViewScene::renderTechniqueControls(float width, float scale)
{
  namespace Page = SandboxPageUI;
  Page::beginCard("##true_view_technique", "TECHNIQUE", width, Page::MID, scale,
                  boldFont);
  trueViewField("Pick out");
  int techniqueIndex = static_cast<int>(technique);
  if (SandboxPageUI::combo("##technique", &techniqueIndex,
                           TrueView::TECHNIQUE_NAMES.data(),
                           static_cast<int>(TrueView::TECHNIQUE_NAMES.size())))
  {
    technique = static_cast<TrueView::Technique>(techniqueIndex);
  }
  if (technique == TrueView::Technique::FOOT_STRIKE)
  {
    std::array<const char*, STRIKE_KIND_COUNT> kinds{};
    for (std::size_t kind = 0; kind < STRIKE_KIND_COUNT; ++kind)
    {
      kinds.at(kind) = STRIKE_KIND_NAMES.at(kind).data();
    }
    trueViewField("Strike");
    SandboxPageUI::combo("##strike", &strikeKind, kinds.data(),
                         static_cast<int>(kinds.size()));
  }
  Page::endCard(scale);
}

void TrueViewScene::renderLayerToggles(float width, float scale)
{
  namespace Page = SandboxPageUI;
  Page::beginCard("##true_view_layers", "LAYERS", width, Page::DARK, scale,
                  boldFont);
  const std::array<std::pair<const char*, bool*>, 10> toggles{
      {{"Body", &layers.body},
       {"Body lines", &layers.lines},
       {"Feet", &layers.feet},
       {"Thigh, chest", &layers.thighChest},
       {"Head", &layers.head},
       {"Send cones", &layers.sendCones},
       {"Technique", &layers.technique},
       {"Personal space", &layers.personalSpace},
       {"Facing, foot", &layers.facing},
       {"Labels", &layers.labels}}};
  if (ImGui::BeginTable("##layers", 2, ImGuiTableFlags_SizingStretchSame))
  {
    for (const auto& [name, value] : toggles)
    {
      ImGui::TableNextColumn();
      ImGui::Checkbox(name, value);
    }
    ImGui::EndTable();
  }
  Page::endCard(scale);
}

void TrueViewScene::renderPanel(float width)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  Page::header(
      "True view",
      "One player as the Binturong body models him: body, reach, contact and "
      "personal space.",
      width, scale, boldFont, regularFont,
      "Drag to orbit / Wheel to zoom / Right-drag to raise or lower the view.",
      true);
  renderViewButtons(width, scale);
  renderDetailTabs(width, scale);
  if (showValues)
  {
    renderTable(width);
    return;
  }
  switch (controlTab)
  {
    case ControlTab::PLAYER:
      renderPlayerControls(width, scale);
      break;
    case ControlTab::POSE:
      renderPoseControls(width, scale);
      break;
    case ControlTab::TECHNIQUE:
      renderTechniqueControls(width, scale);
      break;
    default:
      renderLayerToggles(width, scale);
      break;
  }
}

namespace
{
/** The pose's caption over the model, a getting-up bar, and the airborne
 * note. */
void drawPoseCaption(ImDrawList* draw, ImVec2 origin, float width,
                     const TrueViewMoment& moment, bool airborne)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  const float textWidth = std::max(scale, width - (CAPTION_MARGINS * scale));
  const float line =
      ImGui::CalcTextSize(moment.caption.c_str(), nullptr, false, textWidth).y +
      (CAPTION_LINE_GAP * scale);
  draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                ImVec2(origin.x + (CAPTION_LEFT * scale),
                       origin.y + (CAPTION_TOP * scale)),
                Page::CREAM, moment.caption.c_str(), nullptr, textWidth);
  if (moment.getUpShare >= 0)
  {
    const ImVec2 bar(origin.x + (CAPTION_LEFT * scale),
                     origin.y + (CAPTION_TOP * scale) + line);
    const float barWidth = std::min(BAR_MAX_WIDTH * scale, textWidth);
    draw->AddRect(bar, ImVec2(bar.x + barWidth, bar.y + (BAR_HEIGHT * scale)),
                  Page::CREAM);
    draw->AddRectFilled(bar,
                        ImVec2(bar.x + (barWidth * moment.getUpShare),
                               bar.y + (BAR_HEIGHT * scale)),
                        Page::LIME);
  }
  if (airborne)
  {
    draw->AddText(
        ImGui::GetFont(), ImGui::GetFontSize(),
        ImVec2(origin.x + (CAPTION_LEFT * scale),
               origin.y + (CAPTION_TOP * scale) + line),
        Page::MUTED,
        "In the air only his head plays the ball (the touch sweep skips him)",
        nullptr, textWidth);
  }
}

/** The grass: a metre grid. */
void drawGrassGrid(const TrueView::Painter& paint)
{
  for (int metre = -GRID_METRES; metre <= GRID_METRES; ++metre)
  {
    const auto m = static_cast<float>(metre);
    paint.line({.x = m, .y = -GRID_EXTENT, .z = 0.0F},
               {.x = m, .y = GRID_EXTENT, .z = 0.0F},
               colour(GRID_RGB, GRID_ALPHA), 1.0F);
    paint.line({.x = -GRID_EXTENT, .y = m, .z = 0.0F},
               {.x = GRID_EXTENT, .y = m, .z = 0.0F},
               colour(GRID_RGB, GRID_ALPHA), 1.0F);
  }
}
}  // namespace

/** Drag to orbit, right-drag to raise or lower the view, wheel to zoom. */
void TrueViewScene::orbitCamera()
{
  const ImGuiIO& io = ImGui::GetIO();
  if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
  {
    camera.yaw -= io.MouseDelta.x * DRAG_PER_PIXEL;
    camera.pitch = std::clamp(camera.pitch + (io.MouseDelta.y * DRAG_PER_PIXEL),
                              MIN_PITCH, MAX_PITCH);
  }
  if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Right))
  {
    camera.targetHeight =
        std::clamp(camera.targetHeight + (io.MouseDelta.y * DRAG_PER_PIXEL),
                   MIN_TARGET_HEIGHT, MAX_TARGET_HEIGHT);
  }
  if (ImGui::IsItemHovered() && io.MouseWheel != 0.0F)
  {
    camera.distance = std::clamp(
        camera.distance * std::pow(ZOOM_PER_WHEEL_TICK, io.MouseWheel),
        MIN_DISTANCE, MAX_DISTANCE);
  }
}

/** The playback clock within a cycle of that many seconds. */
float TrueViewScene::cycleOf(float total) const
{
  return std::fmod(std::max(clock, 0.0F), total);
}

/** A standing or running jump: launched to rise his full jump, landing half
 * his time in the air (as stepJumpBinturong). */
TrueViewMoment TrueViewScene::jumpMoment(TrueView::Body& body, float rise) const
{
  using A = MatchTuning::Aerial;
  using B = MatchTuning::Ball;
  const float launch = std::sqrt(2.0F * B::GRAVITY * rise);
  const float air = 2.0F * launch / B::GRAVITY;
  const float landing = launch / B::GRAVITY;
  const float t = cycleOf(REST_SECONDS + air + landing + REST_SECONDS);
  if (t < REST_SECONDS)
  {
    return {.caption = "About to jump"};
  }
  if (t < REST_SECONDS + air)
  {
    const float into = t - REST_SECONDS;
    body.feetHeight =
        std::max((launch * into) - (HALF * B::GRAVITY * into * into), 0.0F);
    body.airborne = true;
    return {.caption = std::format("In the air: feet {:.2f} m up (of {:.2f} m)",
                                   body.feetHeight, rise)};
  }
  if (t < REST_SECONDS + air + landing)
  {
    return {.caption = std::format(
                "Landing ({:.2f} s): {:.0f}% of his acceleration, braking and "
                "turning",
                landing, A::LANDING_MOVEMENT_SHARE * 100.0F)};
  }
  return {.caption = "Landed"};
}

/** How far he has rolled: a turn one way, a pause, a turn back (the engine's
 * rolls are a quarter to a full turn, mostly back the way the last came). */
float TrueViewScene::rolledMetres()
{
  using C = MatchTuning::Collision;
  const float roll = C::ROLL_METRES_PER_TURN / C::ROLL_SPEED;
  const float pause =
      0.5F * (C::ROLL_PAUSE_SHORTEST_SECONDS + C::ROLL_PAUSE_LONGEST_SECONDS);
  const float t = cycleOf(2.0F * (roll + pause));
  float rolled = 0.0F;
  if (t < roll)
  {
    rolled = t * C::ROLL_SPEED;
  }
  else if (t < roll + pause)
  {
    rolled = C::ROLL_METRES_PER_TURN;
  }
  else if (t < (ROLL_LEGS * roll) + pause)
  {
    rolled = C::ROLL_METRES_PER_TURN - (t - roll - pause) * C::ROLL_SPEED;
  }
  entries.push_back(
      {.what = "Down: a roll",
       .value = std::format("{:.2f} m a turn at {:.2f} m/s",
                            C::ROLL_METRES_PER_TURN, C::ROLL_SPEED),
       .source = "Collision::ROLL_METRES_PER_TURN, ROLL_SPEED",
       .wip = "roll speed to try"});
  return rolled;
}

/** Down: on his side, rolling about if he took a big hit. */
TrueViewMoment TrueViewScene::fallenMoment(TrueView::Body& body)
{
  using C = MatchTuning::Collision;
  body.lying = true;
  std::string caption = "Down: the cylinder on its side";
  const float rolled = rolling ? rolledMetres() : 0.0F;
  // Rolling across the way he lies.
  const float across = radians(stats.facing + lyingOffset + 90.0F);
  body.at = {.x = std::sin(across) * rolled,
             .y = std::cos(across) * rolled,
             .z = 0.0F};
  const float turns =
      (rolled / C::ROLL_METRES_PER_TURN) + (fellOnFront ? 0.0F : HALF);
  caption +=
      std::cos(turns * FULL_TURN) > 0.0F ? ", on his front" : ", on his back";
  return {.caption = caption};
}

/** Getting up: lying until fully up, the time by his Agility. */
TrueViewMoment TrueViewScene::gettingUpMoment(TrueView::Body& body)
{
  using C = MatchTuning::Collision;
  const float seconds =
      alongStatScale(rating(stats.agility), C::GET_UP_SECONDS_AT_AGILITY_1,
                     C::GET_UP_SECONDS_AT_AGILITY_99);
  const float t = cycleOf(REST_SECONDS + seconds + (2.0F * REST_SECONDS));
  TrueViewMoment moment{.caption = "Up"};
  if (t < REST_SECONDS + seconds)
  {
    body.lying = true;
    moment.getUpShare = std::clamp((t - REST_SECONDS) / seconds, 0.0F, 1.0F);
    moment.caption = std::format(
        "Getting up: {:.0f}% of {:.2f} s (still lying until fully up)",
        moment.getUpShare * 100.0F, seconds);
  }
  entries.push_back({.what = "Getting up",
                     .value = std::format("{:.2f} s", seconds),
                     .source = "Collision::GET_UP_SECONDS_AT_AGILITY_1/_99 "
                               "(gingerly: 4 to 3 s)",
                     .wip = ""});
  return moment;
}

/** The moment of the pose: what he is doing now, and his body for it. */
TrueViewMoment TrueViewScene::poseMoment(TrueView::Body& body,
                                         const JumpRises& rises)
{
  using B = MatchTuning::Ball;
  using C = MatchTuning::Collision;
  switch (pose)
  {
    case Pose::STANDING:
      return {.caption =
                  moving ? "Standing, moving faster than 1.0 m/s" : "Standing"};
    case Pose::STANDING_JUMP:
      return jumpMoment(body, rises.standing);
    case Pose::RUNNING_JUMP:
      return jumpMoment(body, rises.running);
    case Pose::FALLEN:
      return fallenMoment(body);
    case Pose::GETTING_UP:
      return gettingUpMoment(body);
    case Pose::STUMBLING:
      return {.caption = std::format(
                  "Stumbling ({:.1f}-{:.1f} s): no drive or steering, "
                  "recovery steps brake at {:.0f}%",
                  C::STUMBLE_SHORTEST_SECONDS, C::STUMBLE_LONGEST_SECONDS,
                  C::STUMBLE_BRAKING_SHARE * 100.0F)};
    case Pose::PLANTED:
      return {.caption = std::format(
                  "Planted after a thigh touch: no acceleration for {:.1f} s",
                  B::THIGH_PLANTED_SECONDS)};
  }
  return {};
}

/** His body as his skills and the pose make it (his measures are set by the
 * caller). */
TrueView::Body TrueViewScene::statsBody() const
{
  TrueView::Body body;
  body.facing = radians(stats.facing);
  body.rightFooted = stats.rightFooted;
  body.dribbling = rating(stats.dribbling);
  body.defending = rating(stats.defending);
  body.physicality = rating(stats.physicality);
  // His thigh only nearly stopped: off moving, and on a running jump's
  // run-up (at least a jog).
  body.thighOff =
      (moving && (pose == Pose::STANDING || pose == Pose::STUMBLING)) ||
      pose == Pose::RUNNING_JUMP;
  return body;
}

void TrueViewScene::renderCanvas(ImVec2 size)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  entries.clear();
  Page::beginCard("##true_view_canvas", "PLAYER MODEL", size.x, Page::DARK,
                  scale, boldFont);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 area(std::max(scale, ImGui::GetContentRegionAvail().x),
                    std::max(200 * scale, size.y - (56 * scale)));
  ImGui::InvisibleButton(
      "##orbit", area,
      ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
  orbitCamera();

  // ---- His measures, from the engine -------------------------------------
  const TechniqueTuning& sandbox = sandboxTuning();
  const TrueView::Tuning tuning =
      TrueView::Tuning::from(sandbox.touch, sandbox.strike, sandbox.stretch,
                             static_cast<StrikeKind>(strikeKind));
  const float height = static_cast<float>(stats.heightCentimetres) / 100.0F;
  const DerivedRecipe& jumpRecipe =
      DEFAULT_DERIVED_RECIPES[static_cast<std::size_t>(
          DerivedStat::JUMPING_REACH)];
  const float jumpRating =
      std::clamp(derivedRatingOnHeight(DerivedStat::JUMPING_REACH, jumpRecipe,
                                       rating(stats.jump), height),
                 0.0F, 1.0F);
  const float jumpingReach =
      derivedPhysicalValue(DerivedStat::JUMPING_REACH, jumpRating);
  const JumpRises rises{.running = jumpRiseOf(jumpingReach, height, true),
                        .standing = jumpRiseOf(jumpingReach, height, false)};
  TrueView::Body body = statsBody();
  body.height = height;
  body.jumpingReach = jumpingReach;
  body.standingRise = rises.standing;

  // ---- The moment ----------------------------------------------------------
  const TrueViewMoment moment = poseMoment(body, rises);
  body.lyingAngle = radians(stats.facing + lyingOffset);
  entries.push_back(
      {.what = "Weight (collisions)",
       .value = std::format("{:.0f} kg",
                            bodyMassKilograms(height, rating(stats.strength))),
       .source =
           "bodyMassKilograms: height squared x BMI 17 (Strength 1) to 31 (99)",
       .wip = "a proxy until the game carries weight"});
  entries.push_back(
      {.what = "Jumping reach stat",
       .value = std::format("{:.0f}", jumpRating * 100.0F),
       .source = "derivedRatingOnHeight: height + the jump's blend",
       .wip = ""});

  // ---- The painter ---------------------------------------------------------
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->PushClipRect(origin, ImVec2(origin.x + area.x, origin.y + area.y),
                     true);
  draw->AddRectFilled(origin, ImVec2(origin.x + area.x, origin.y + area.y),
                      Page::DARK);
  const Vec3 target{.x = 0.0F, .y = 0.0F, .z = camera.targetHeight};
  const Vec3 eye{.x = target.x + (camera.distance * std::cos(camera.pitch) *
                                  std::cos(camera.yaw)),
                 .y = target.y + (camera.distance * std::cos(camera.pitch) *
                                  std::sin(camera.yaw)),
                 .z = target.z + (camera.distance * std::sin(camera.pitch))};
  const RenderMath::Projection projection = RenderMath::Projection::make(
      eye, target, 0.75F, 0.05F, 200.0F,
      {.x = origin.x, .y = origin.y, .width = area.x, .height = area.y});
  TrueView::Painter paint;
  paint.draw = draw;
  paint.project = [&projection](Vec3 point, ImVec2& out)
  {
    RenderMath::ScreenPoint screen;
    if (!projection.project(point, screen))
    {
      return false;
    }
    out = {screen.x, screen.y};
    return true;
  };
  drawGrassGrid(paint);
  std::vector<TrueView::Label> labels;
  TrueView::drawBody(paint, body, tuning, layers, technique,
                     TrueView::Detail::FULL, &labels, &entries);
  TrueView::drawLabels(paint, labels);
  drawPoseCaption(draw, origin, area.x, moment, body.airborne);
  draw->PopClipRect();
  Page::endCard(scale);
}

void TrueViewScene::renderTable(float width)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  Page::beginCard("##true_view_values", "ENGINE VALUES", width, Page::MID,
                  scale, boldFont);
  UI::textWrapped(
      "Values come directly from engine functions and sandbox tuning this "
      "frame. WIP marks placeholders, old rules or work in progress.");
  // Keep the engine/source semantics and diagnostics of the existing readout.
  TrueView::renderTable(entries, boldFont);
  Page::endCard(scale);
}
