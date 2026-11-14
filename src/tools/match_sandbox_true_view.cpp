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
constexpr float REST_SECONDS = 0.6f;

float radians(float degrees) { return degrees * PI / 180.0f; }

/** A stat (1-99) as the Binturong handshake's rating (the stat / 100). */
float rating(int stat) { return static_cast<float>(stat) / 100.0f; }

ImU32 colour(int r, int g, int b, float alpha)
{
  return IM_COL32(r, g, b,
                  static_cast<int>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f));
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
      if (std::string_view(existing->GetDebugName()).find(file) !=
              std::string_view::npos &&
          !existing->Sources.empty() &&
          existing->Sources[0]->ExtraSizeScale == config.ExtraSizeScale)
        return existing;
    return atlas->AddFontFromFileTTF(
        AssetPaths::file((std::string("assets/fonts/") + file).c_str()).c_str(),
        size, &config);
  };
  regularFont = font("Manrope-Regular.ttf", 16);
  boldFont = font("Manrope-Bold.ttf", 14);
}

void TrueViewScene::update(float deltaTime)
{
  if (playing) clock += deltaTime * playbackSpeed;
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
                      ImVec2(24 * scale, 24 * scale));
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
  const float gutter = 6 * scale + ImGui::GetStyle().ScrollbarSize;
  const float panel = columns
                          ? (available.x >= 1400 * scale ? 440 : 400) * scale
                          : std::max(scale, available.x - gutter);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  if (columns)
  {
    const ImVec2 origin = ImGui::GetCursorPos();
    ImGui::SetCursorPos(
        ImVec2(origin.x + panel + gutter + 6 * scale, origin.y));
    // Build this frame's measures with the model, before the values tab reads
    // them.
    renderCanvas(ImVec2(available.x - panel - gutter - 6 * scale, available.y));
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
    renderCanvas(ImVec2(panel, std::max(300 * scale, available.y * 0.65f)));
    renderPanel(panel);
    ImGui::EndChild();
  }
  ImGui::PopStyleVar();
  ImGui::End();
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
  Page::beginCard("##true_view_navigation", "VIEW", width, Page::DARK, scale,
                  boldFont);
  if (Page::button("Back to sandbox", false, scale, boldFont))
    guiView->popScene();
  if (Page::button("3/4", false, scale, boldFont)) camera = Camera{};
  ImGui::SameLine(0, 6 * scale);
  if (Page::button("Side", false, scale, boldFont))
  {
    camera.yaw = 0;
    camera.pitch = 0.05f;
  }
  ImGui::SameLine(0, 6 * scale);
  if (Page::button("Front", false, scale, boldFont))
  {
    camera.yaw = PI * 0.5f;
    camera.pitch = 0.05f;
  }
  ImGui::SameLine(0, 6 * scale);
  if (Page::button("Top", false, scale, boldFont)) camera.pitch = 1.5f;
  Page::endCard(scale);
  Page::beginCard("##true_view_tabs", "DETAILS", width, Page::MID, scale,
                  boldFont);
  if (Page::button("Controls", !showValues, scale, boldFont))
    showValues = false;
  ImGui::SameLine(0, 6 * scale);
  if (Page::button("Engine values", showValues, scale, boldFont))
    showValues = true;
  if (!showValues)
  {
    const std::array<const char*, 4> tabs{"Player", "Pose", "Technique",
                                          "Layers"};
    for (std::size_t index = 0; index < tabs.size(); ++index)
    {
      if (index > 0) ImGui::SameLine(0, 6 * scale);
      const auto tab = static_cast<ControlTab>(index);
      if (Page::button(tabs[index], controlTab == tab, scale, boldFont))
        controlTab = tab;
    }
  }
  Page::endCard(scale);
  if (showValues)
  {
    renderTable(width);
    return;
  }
  const auto field = [](const char* label)
  {
    ImGui::TextWrapped("%s", label);
    ImGui::SetNextItemWidth(-1);
  };
  if (controlTab == ControlTab::PLAYER)
  {
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
        {{"Height (cm)", &Stats::heightCentimetres, 150, 210, nullptr},
         {"Jump", &Stats::jump, 1, 99,
          "Jumping reach's blend: the jump on top of his height (Pace and "
          "Physicality stand in until the game carries it)."},
         {"Strength", &Stats::strength, 1, 99, nullptr},
         {"Agility", &Stats::agility, 1, 99, nullptr},
         {"Dribbling", &Stats::dribbling, 1, 99, nullptr},
         {"Defending", &Stats::defending, 1, 99, nullptr},
         {"Physicality", &Stats::physicality, 1, 99,
          "Only the old shot-block rule reads it here."}}};
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(8 * scale, 4 * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,
                        ImVec2(3 * scale, 3 * scale));
    if (ImGui::BeginTable(
            "##player_fields", width >= 360 * scale ? 2 : 1,
            ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_BordersInnerV))
    {
      for (const auto& stat : fields)
      {
        ImGui::TableNextColumn();
        ImGui::PushID(stat.label);
        ImGui::Dummy(ImVec2(0, 0));
        field(stat.label);
        if (stat.tip && ImGui::IsItemHovered())
          ImGui::SetTooltip("%s", stat.tip);
        ImGui::SliderInt("##value", &(stats.*stat.value), stat.min, stat.max);
        if (stat.tip && ImGui::IsItemHovered())
          ImGui::SetTooltip("%s", stat.tip);
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
    ImGui::PopStyleVar(2);
    if (Page::button("Right-footed", stats.rightFooted, scale, boldFont))
      stats.rightFooted = true;
    ImGui::SameLine(0, 6 * scale);
    if (Page::button("Left-footed", !stats.rightFooted, scale, boldFont))
      stats.rightFooted = false;
    field("Facing");
    ImGui::SliderFloat("##facing", &stats.facing, -180, 180, "%.0f deg");
    Page::endCard(scale);
    return;
  }
  if (controlTab == ControlTab::POSE)
  {
    Page::beginCard("##true_view_pose", "POSE & PLAYBACK", width, Page::MID,
                    scale, boldFont);
    field("Pose");
    int poseIndex = static_cast<int>(pose);
    if (ImGui::Combo("##pose", &poseIndex, POSE_NAMES.data(),
                     static_cast<int>(POSE_NAMES.size())))
    {
      pose = static_cast<Pose>(poseIndex);
      clock = 0;
    }
    if (pose == Pose::STANDING || pose == Pose::STUMBLING)
      ImGui::Checkbox("Moving faster than his thigh allows", &moving);
    if (pose == Pose::FALLEN || pose == Pose::GETTING_UP)
    {
      field("Lying");
      ImGui::SliderFloat("##lying", &lyingOffset, -90, 90,
                         "%.0f deg off facing");
      ImGui::Checkbox("Fell on his front", &fellOnFront);
      if (pose == Pose::FALLEN)
        ImGui::Checkbox("Rolling about (big hit)", &rolling);
    }
    if (pose != Pose::STANDING && pose != Pose::PLANTED &&
        pose != Pose::STUMBLING)
    {
      ImGui::Checkbox("Play", &playing);
      ImGui::SameLine(0, 6 * scale);
      if (Page::button("Restart", false, scale, boldFont)) clock = 0;
      field("Speed");
      ImGui::SliderFloat("##playback_speed", &playbackSpeed, 0.05f, 1, "%.2fx");
    }
    Page::endCard(scale);
    return;
  }
  if (controlTab == ControlTab::TECHNIQUE)
  {
    Page::beginCard("##true_view_technique", "TECHNIQUE", width, Page::MID,
                    scale, boldFont);
    field("Pick out");
    int techniqueIndex = static_cast<int>(technique);
    if (ImGui::Combo("##technique", &techniqueIndex,
                     TrueView::TECHNIQUE_NAMES.data(),
                     static_cast<int>(TrueView::TECHNIQUE_NAMES.size())))
      technique = static_cast<TrueView::Technique>(techniqueIndex);
    if (technique == TrueView::Technique::FOOT_STRIKE)
    {
      std::array<const char*, STRIKE_KIND_COUNT> kinds{};
      for (std::size_t kind = 0; kind < STRIKE_KIND_COUNT; ++kind)
        kinds[kind] = STRIKE_KIND_NAMES[kind].data();
      field("Strike");
      ImGui::Combo("##strike", &strikeKind, kinds.data(),
                   static_cast<int>(kinds.size()));
    }
    Page::endCard(scale);
    return;
  }
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

void TrueViewScene::renderCanvas(ImVec2 size)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  entries.clear();
  Page::beginCard("##true_view_canvas", "PLAYER MODEL", size.x, Page::DARK,
                  scale, boldFont);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 area(std::max(scale, ImGui::GetContentRegionAvail().x),
                    std::max(200 * scale, size.y - 56 * scale));
  ImGui::InvisibleButton(
      "##orbit", area,
      ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
  const ImGuiIO& io = ImGui::GetIO();
  if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
  {
    camera.yaw -= io.MouseDelta.x * 0.01f;
    camera.pitch =
        std::clamp(camera.pitch + io.MouseDelta.y * 0.01f, -0.2f, 1.55f);
  }
  if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Right))
    camera.targetHeight =
        std::clamp(camera.targetHeight + io.MouseDelta.y * 0.01f, -0.5f, 3.0f);
  if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f)
    camera.distance = std::clamp(
        camera.distance * std::pow(0.9f, io.MouseWheel), 1.0f, 25.0f);

  // ---- His measures, from the engine -------------------------------------
  using A = MatchTuning::Aerial;
  using B = MatchTuning::Ball;
  using C = MatchTuning::Collision;
  const TechniqueTuning& sandbox = sandboxTuning();
  const TrueView::Tuning tuning =
      TrueView::Tuning::from(sandbox.touch, sandbox.strike, sandbox.stretch,
                             static_cast<StrikeKind>(strikeKind));
  const float height = static_cast<float>(stats.heightCentimetres) / 100.0f;
  const DerivedRecipe& jumpRecipe =
      DEFAULT_DERIVED_RECIPES[static_cast<std::size_t>(
          DerivedStat::JUMPING_REACH)];
  const float jumpRating =
      std::clamp(derivedRatingOnHeight(DerivedStat::JUMPING_REACH, jumpRecipe,
                                       rating(stats.jump), height),
                 0.0f, 1.0f);
  const float jumpingReach =
      derivedPhysicalValue(DerivedStat::JUMPING_REACH, jumpRating);
  const float runningRise = jumpRiseOf(jumpingReach, height, true);
  const float standingRise = jumpRiseOf(jumpingReach, height, false);

  TrueView::Body body;
  body.facing = radians(stats.facing);
  body.height = height;
  body.rightFooted = stats.rightFooted;
  body.dribbling = rating(stats.dribbling);
  body.defending = rating(stats.defending);
  body.physicality = rating(stats.physicality);
  body.jumpingReach = jumpingReach;
  body.standingRise = standingRise;
  // His thigh only nearly stopped: off moving, and on a running jump's
  // run-up (at least a jog).
  body.thighOff =
      (moving && (pose == Pose::STANDING || pose == Pose::STUMBLING)) ||
      pose == Pose::RUNNING_JUMP;

  // ---- The moment ----------------------------------------------------------
  std::string caption;
  float getUpShare = -1.0f;
  const auto cycle = [&](float total)
  { return std::fmod(std::max(clock, 0.0f), total); };
  switch (pose)
  {
    case Pose::STANDING:
      caption = moving ? "Standing, moving faster than 1.0 m/s" : "Standing";
      break;
    case Pose::STANDING_JUMP:
    case Pose::RUNNING_JUMP:
    {
      const float rise =
          pose == Pose::RUNNING_JUMP ? runningRise : standingRise;
      // As stepJumpBinturong: launched to rise his full jump, landing half
      // his time in the air.
      const float launch = std::sqrt(2.0f * B::GRAVITY * rise);
      const float air = 2.0f * launch / B::GRAVITY;
      const float landing = launch / B::GRAVITY;
      const float t = cycle(REST_SECONDS + air + landing + REST_SECONDS);
      if (t < REST_SECONDS)
        caption = "About to jump";
      else if (t < REST_SECONDS + air)
      {
        const float into = t - REST_SECONDS;
        body.feetHeight =
            std::max(launch * into - 0.5f * B::GRAVITY * into * into, 0.0f);
        body.airborne = true;
        caption = std::format("In the air: feet {:.2f} m up (of {:.2f} m)",
                              body.feetHeight, rise);
      }
      else if (t < REST_SECONDS + air + landing)
        caption = std::format(
            "Landing ({:.2f} s): {:.0f}% of his acceleration, braking and "
            "turning",
            landing, A::LANDING_MOVEMENT_SHARE * 100.0f);
      else
        caption = "Landed";
      break;
    }
    case Pose::FALLEN:
    {
      body.lying = true;
      caption = "Down: the cylinder on its side";
      float rolled = 0.0f;
      if (rolling)
      {
        // A turn one way, a pause, a turn back (the engine's rolls are a
        // quarter to a full turn, mostly back the way the last came).
        const float roll = C::ROLL_METRES_PER_TURN / C::ROLL_SPEED;
        const float pause = 0.5f * (C::ROLL_PAUSE_SHORTEST_SECONDS +
                                    C::ROLL_PAUSE_LONGEST_SECONDS);
        const float t = cycle(2.0f * (roll + pause));
        if (t < roll)
          rolled = t * C::ROLL_SPEED;
        else if (t < roll + pause)
          rolled = C::ROLL_METRES_PER_TURN;
        else if (t < 2.0f * roll + pause)
          rolled = C::ROLL_METRES_PER_TURN - (t - roll - pause) * C::ROLL_SPEED;
        entries.push_back({"Down: a roll",
                           std::format("{:.2f} m a turn at {:.2f} m/s",
                                       C::ROLL_METRES_PER_TURN, C::ROLL_SPEED),
                           "Collision::ROLL_METRES_PER_TURN, ROLL_SPEED",
                           "roll speed to try"});
      }
      // Rolling across the way he lies.
      const float across = radians(stats.facing + lyingOffset + 90.0f);
      body.at = {std::sin(across) * rolled, std::cos(across) * rolled, 0.0f};
      const float turns =
          rolled / C::ROLL_METRES_PER_TURN + (fellOnFront ? 0.0f : 0.5f);
      caption += std::cos(turns * 2.0f * PI) > 0.0f ? ", on his front"
                                                    : ", on his back";
      break;
    }
    case Pose::GETTING_UP:
    {
      const float seconds =
          alongStatScale(rating(stats.agility), C::GET_UP_SECONDS_AT_AGILITY_1,
                         C::GET_UP_SECONDS_AT_AGILITY_99);
      const float t = cycle(REST_SECONDS + seconds + 2.0f * REST_SECONDS);
      if (t < REST_SECONDS + seconds)
      {
        body.lying = true;
        getUpShare = std::clamp((t - REST_SECONDS) / seconds, 0.0f, 1.0f);
        caption = std::format(
            "Getting up: {:.0f}% of {:.2f} s (still lying until fully up)",
            getUpShare * 100.0f, seconds);
      }
      else
        caption = "Up";
      entries.push_back(
          {"Getting up", std::format("{:.2f} s", seconds),
           "Collision::GET_UP_SECONDS_AT_AGILITY_1/_99 (gingerly: 4 to 3 s)",
           ""});
      break;
    }
    case Pose::STUMBLING:
      caption = std::format(
          "Stumbling ({:.1f}-{:.1f} s): no drive or steering, recovery steps "
          "brake at {:.0f}%",
          C::STUMBLE_SHORTEST_SECONDS, C::STUMBLE_LONGEST_SECONDS,
          C::STUMBLE_BRAKING_SHARE * 100.0f);
      break;
    case Pose::PLANTED:
      caption = std::format(
          "Planted after a thigh touch: no acceleration for {:.1f} s",
          B::THIGH_PLANTED_SECONDS);
      break;
  }
  body.lyingAngle = radians(stats.facing + lyingOffset);
  entries.push_back(
      {"Weight (collisions)",
       std::format("{:.0f} kg",
                   bodyMassKilograms(height, rating(stats.strength))),
       "bodyMassKilograms: height squared x BMI 17 (Strength 1) to 31 (99)",
       "a proxy until the game carries weight"});
  entries.push_back({"Jumping reach stat",
                     std::format("{:.0f}", jumpRating * 100.0f),
                     "derivedRatingOnHeight: height + the jump's blend", ""});

  // ---- The painter ---------------------------------------------------------
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->PushClipRect(origin, ImVec2(origin.x + area.x, origin.y + area.y),
                     true);
  draw->AddRectFilled(origin, ImVec2(origin.x + area.x, origin.y + area.y),
                      Page::DARK);
  const Vec3 target{0.0f, 0.0f, camera.targetHeight};
  const Vec3 eye{target.x + camera.distance * std::cos(camera.pitch) *
                                std::cos(camera.yaw),
                 target.y + camera.distance * std::cos(camera.pitch) *
                                std::sin(camera.yaw),
                 target.z + camera.distance * std::sin(camera.pitch)};
  const RenderMath::Projection projection = RenderMath::Projection::make(
      eye, target, 0.75f, 0.05f, 200.0f, {origin.x, origin.y, area.x, area.y});
  TrueView::Painter paint;
  paint.draw = draw;
  paint.project = [&projection](Vec3 point, ImVec2& out)
  {
    RenderMath::ScreenPoint screen;
    if (!projection.project(point, screen)) return false;
    out = {screen.x, screen.y};
    return true;
  };

  // The grass: a metre grid.
  for (int metre = -3; metre <= 3; ++metre)
  {
    const float m = static_cast<float>(metre);
    paint.line({m, -3.0f, 0.0f}, {m, 3.0f, 0.0f}, colour(125, 109, 91, 0.35f),
               1.0f);
    paint.line({-3.0f, m, 0.0f}, {3.0f, m, 0.0f}, colour(125, 109, 91, 0.35f),
               1.0f);
  }

  std::vector<TrueView::Label> labels;
  TrueView::drawBody(paint, body, tuning, layers, technique,
                     TrueView::Detail::FULL, &labels, &entries);
  TrueView::drawLabels(paint, labels);

  const float textWidth = std::max(scale, area.x - 20 * scale);
  const float line =
      ImGui::CalcTextSize(caption.c_str(), nullptr, false, textWidth).y +
      6 * scale;
  draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                ImVec2(origin.x + 10 * scale, origin.y + 8 * scale),
                Page::CREAM, caption.c_str(), nullptr, textWidth);
  if (getUpShare >= 0)
  {
    const ImVec2 bar(origin.x + 10 * scale, origin.y + 8 * scale + line);
    const float barWidth = std::min(200 * scale, textWidth);
    draw->AddRect(bar, ImVec2(bar.x + barWidth, bar.y + 8 * scale),
                  Page::CREAM);
    draw->AddRectFilled(
        bar, ImVec2(bar.x + barWidth * getUpShare, bar.y + 8 * scale),
        Page::LIME);
  }
  if (body.airborne)
    draw->AddText(
        ImGui::GetFont(), ImGui::GetFontSize(),
        ImVec2(origin.x + 10 * scale, origin.y + 8 * scale + line), Page::MUTED,
        "In the air only his head plays the ball (the touch sweep skips him)",
        nullptr, textWidth);
  draw->PopClipRect();
  Page::endCard(scale);
}

void TrueViewScene::renderTable(float width)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  Page::beginCard("##true_view_values", "ENGINE VALUES", width, Page::MID,
                  scale, boldFont);
  ImGui::TextWrapped(
      "Values come directly from engine functions and sandbox tuning this "
      "frame. WIP marks placeholders, old rules or work in progress.");
  // Keep the engine/source semantics and diagnostics of the existing readout.
  TrueView::renderTable(entries, boldFont);
  Page::endCard(scale);
}
