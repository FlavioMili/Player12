// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_contact_view.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "global/paths.h"
#include "gui/gui_view.h"
#include "gui/widgets/theme.h"
#include "tools/match_sandbox_bench_scene.h"
#include "tools/match_sandbox_page_ui.h"
#include "tools/match_sandbox_text.h"
#include "tools/match_sandbox_tuning.h"

namespace
{
constexpr float BUTTON_GAP = 6.0F;
constexpr float LEGEND_GAP = 12.0F;
constexpr float HALF_TURN_DEGREES = 180.0F;
constexpr float FULL_TURN_DEGREES = 360.0F;
constexpr float MIDPOINT_SHARE = 0.5F;
constexpr float PROBE_DIRECTION_EPSILON = 1e-4F;
constexpr float FACTOR_BAR_HEIGHT_SHARE = 0.6F;
constexpr float ARROW_HEAD_HALF_WIDTH = 0.6F;
constexpr float REGULAR_FONT_SIZE = 16.0F;
constexpr float HEADER_FONT_SIZE = 14.0F;
constexpr float PAGE_PADDING = 24.0F;
constexpr float MIN_CIRCLE_HEIGHT = 340.0F;
constexpr float NARROW_CIRCLE_SHARE = 0.75F;
constexpr float FIELD_PADDING_X = 8.0F;
constexpr float FIELD_PADDING_Y = 4.0F;
constexpr float INPUT_COLUMNS_WIDTH = 360.0F;
constexpr float MAX_OWN_SPEED = 10.0F;
constexpr float MIN_TOP_SPEED = 5.0F;
constexpr float MAX_TOP_SPEED = 10.5F;
constexpr float MAX_BALL_PACE = 35.0F;
constexpr float MIN_VERTICAL_SPEED = -15.0F;
constexpr float MAX_VERTICAL_SPEED = 10.0F;
constexpr float MAX_BALL_HEIGHT = 2.5F;
constexpr float MAX_OPPONENT_DISTANCE = 12.0F;
constexpr float MIN_LIFT = -10.0F;
constexpr float MAX_LIFT = 15.0F;
constexpr float DISC_HIT_RADIUS = 1.03F;
constexpr float RING_HIT_PADDING = 0.1F;
constexpr int DASH_SEGMENTS = 48;
constexpr int HEAT_BAR_STEPS = 48;
constexpr float THIN_STROKE = 1.5F;
constexpr float NORMAL_STROKE = 2.0F;
constexpr float CHEVRON_STROKE = 1.6F;
constexpr float CHEVRON_OUTLINE_STROKE = 3.5F;
constexpr float SHOULDER_STROKE = 5.0F;
constexpr float FACING_STROKE = 2.5F;
constexpr float CENTRE_RADIUS_METRES = 0.09F;
constexpr float FACING_START_METRES = 0.12F;
constexpr float FACING_RING_OVERHANG = 0.06F;
constexpr float FOOT_RADIUS_METRES = 0.05F;
constexpr float FOOT_LABEL_OFFSET_PIXELS = 4.0F;
constexpr float FOOT_LABEL_OFFSET_METRES = 0.06F;
constexpr float MIN_ARROW_SPEED = 0.05F;
constexpr float AIM_POINT_RADIUS = 4.0F;
constexpr float MIN_BALL_RADIUS_PIXELS = 4.0F;
constexpr float HANDLE_RADIUS = 6.0F;
constexpr float VISIBLE_FACTOR_THRESHOLD = 0.005F;
constexpr float VERTICAL_TURN_LABEL_THRESHOLD = 0.01F;
constexpr float JOB_COLUMN_WIDTH = 102.0F;
constexpr float DIFFICULTY_BAR_HEIGHT = 8.0F;
constexpr int FACTOR_COLUMN_COUNT = 5;
constexpr float VALUE_COLUMN_WIDTH = 42.0F;
constexpr float WEIGHT_COLUMN_WIDTH = 52.0F;
constexpr float VERTICAL_WEIGHT_COLUMN_WIDTH = 62.0F;
constexpr float DISTANCE_DRAG_SPEED = 0.005F;
constexpr float STRETCH_DRAG_SPEED = 0.005F;
constexpr float ANGLE_DRAG_SPEED = 0.5F;
constexpr int ANGLE_ROW_ID_BASE = 100;
constexpr float STRETCH_CELL_ALPHA = 0.55F;
constexpr float SIDE_BALL_RADIUS = 4.5F;
constexpr float SIDE_LABEL_PADDING_X = 4.0F;
constexpr float SIDE_LABEL_PADDING_Y = 2.0F;
constexpr float KNOB_DRAG_DIVISOR = 400.0F;
constexpr ImVec4 STRETCH_TINT{1.00F, 0.55F, 0.15F, 1.0F};
constexpr ImVec4 WEAK_FOOT_TINT{0.25F, 0.55F, 1.00F, 1.0F};
constexpr ImVec4 APPROACH_TINT{0.90F, 0.30F, 0.80F, 1.0F};
constexpr ImVec4 TURN_TINT{0.95F, 0.85F, 0.25F, 1.0F};
constexpr ImVec4 UNWEIGHTED_TINT{0.7F, 0.7F, 0.7F, 1.0F};
constexpr ImVec4 DEAD_ZONE_TINT{1.0F, 0.55F, 0.45F, 1.0F};
constexpr ImVec4 MODIFIED_TINT{1.0F, 0.75F, 0.3F, 1.0F};

struct PanelCard
{
  const char* id;
  const char* title;
};
constexpr std::array<PanelCard, 3> TUNING_CARDS{
    {{.id = "##contact_tuning", .title = "TUNING"},
     {.id = "##contact_stretch", .title = "STRETCH MAP"},
     {.id = "##contact_knobs", .title = "CONTACT KNOBS"}}};
constexpr std::array<PanelCard, 3> INPUT_CARDS{
    {{.id = "##contact_player", .title = "PLAYER"},
     {.id = "##contact_ball", .title = "BALL"},
     {.id = "##contact_aim", .title = "INTENT"}}};
template <std::size_t Size>
const PanelCard& panelCard(const std::array<PanelCard, Size>& cards, int tab)
{
  if (tab < 0 || tab >= static_cast<int>(Size))
  {
    return cards.back();
  }
  return cards.at(static_cast<std::size_t>(tab));
}

const char* verticalTurnLabel(float turn)
{
  if (std::abs(turn) < VERTICAL_TURN_LABEL_THRESHOLD)
  {
    return "";
  }
  return turn > 0.0F ? "up" : "down";
}

/** Cells across the reach circle, and segments round each ring. */
constexpr int DISC_CELLS = 64;
constexpr int RING_SEGMENTS = 96;
/** The rings round the disc, as multiples of its radius. */
constexpr float FROM_INNER = 1.07F;
constexpr float FROM_OUTER = 1.29F;
constexpr float AIM_INNER = 1.35F;
constexpr float AIM_OUTER = 1.57F;
constexpr float BALL_RADIUS_METRES = 0.11F;

// The zone conditions' indices, in ContactAssessment::factors order.
constexpr int TURN = 1;
constexpr int APPROACH = 3;
constexpr int STRETCH = 5;
constexpr int WEAK_FOOT = 6;

/** What each condition reads, for the breakdown. */
constexpr std::array<const char*, CONTACT_FACTOR_COUNT> DEPENDS_ON = {
    "intended pace against incoming",
    "aim against its line (outer ring)",
    "ball's speed relative to him",
    "where it comes from (inner ring)",
    "his speed against his top speed",
    "where he meets it (disc)",
    "which side of his facing line (scales the jobs)",
    "ball height",
    "the brain: a pass meant for someone else",
    "nearest opponent",
    "his lift against the ball's steepness (side view)",
    "the spin he asks for against what he can put on (strikes)",
    "the pace he asks for against his power (strikes)"};

/** Which conditions colour each part of the circle in the zones view. */
// Weaker foot is left off the disc so stretch reads clearly (it can still
// be shown alone with Colour by).
constexpr std::array<int, 1> DISC_ZONES{STRETCH};
constexpr std::array<int, 1> FROM_ZONES{APPROACH};
constexpr std::array<int, 1> AIM_ZONES{TURN};

/** Each zone condition's colour. */
ImVec4 zoneHue(int factor)
{
  switch (factor)
  {
    case STRETCH:
      return STRETCH_TINT;
    case WEAK_FOOT:
      return WEAK_FOOT_TINT;
    case APPROACH:
      return APPROACH_TINT;
    case TURN:
      return TURN_TINT;
    default:
      return UNWEIGHTED_TINT;
  }
}

constexpr ImVec4 BASE{0.13F, 0.15F, 0.18F, 1.0F};
/** Where a first touch cannot meet the ball. */
constexpr ImU32 DEAD_ZONE_COLOUR = IM_COL32(62, 62, 66, 255);

/** 0-1 on a dark-to-hot scale. */
ImU32 heat(float value)
{
  constexpr std::array<ImVec4, 5> STOPS{{{0.14F, 0.17F, 0.26F, 1.0F},
                                         {0.15F, 0.42F, 0.55F, 1.0F},
                                         {0.35F, 0.66F, 0.40F, 1.0F},
                                         {0.94F, 0.78F, 0.24F, 1.0F},
                                         {0.86F, 0.24F, 0.20F, 1.0F}}};
  const float scaled = std::clamp(value, 0.0F, 1.0F) * 4.0F;
  const int index = std::min(static_cast<int>(scaled), 3);
  const float t = scaled - static_cast<float>(index);
  const ImVec4& a = STOPS.at(static_cast<std::size_t>(index));
  const ImVec4& b = STOPS.at(static_cast<std::size_t>(index) + 1);
  return ImGui::GetColorU32(ImVec4(a.x + ((b.x - a.x) * t),
                                   a.y + ((b.y - a.y) * t),
                                   a.z + ((b.z - a.z) * t), 1.0F));
}

void field(const char* label)
{
  SandboxText::wrappedText(label);
  ImGui::SetNextItemWidth(-1);
}

void nextButton(const char* label, float scale, ImFont* font)
{
  if (ImGui::GetItemRectMax().x + BUTTON_GAP * scale +
          SandboxPageUI::buttonWidth(label, scale, font) <=
      ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x)
  {
    ImGui::SameLine(0, BUTTON_GAP * scale);
  }
}

float radians(float degrees)
{
  return degrees * std::numbers::pi_v<float> / HALF_TURN_DEGREES;
}

/** A compass direction (0 up, clockwise) as a metric unit vector (x right,
 * y up), the engine's handedness: his right is clockwise of his facing. */
Vector2F compass(float degrees)
{
  const float angle = radians(degrees);
  return {.x = std::sin(angle), .y = std::cos(angle)};
}

float compassOf(ImVec2 offset)
{
  float degrees = std::atan2(offset.x, offset.y) * HALF_TURN_DEGREES /
                  std::numbers::pi_v<float>;
  return degrees < 0.0F ? degrees + FULL_TURN_DEGREES : degrees;
}

/** Typical intended pace for each contact (m/s). */
constexpr std::array<float, 1 + STRIKE_KIND_COUNT> TYPICAL_PACE{
    3.0F,  15.0F, 20.0F, 20.0F, 25.0F, 10.0F, 22.0F,
    12.0F, 24.0F, 12.0F, 17.0F, 21.0F, 12.0F, 18.0F};

constexpr std::array<const char*, 4> EXPECTATIONS{
    "A pass to him", "A loose ball", "A pass for someone else",
    "His own ball (dribble touch)"};

/** An arrow from `from` to `to`. */
void arrow(ImDrawList& draw, ImVec2 from, ImVec2 to, ImU32 colour,
           float thickness)
{
  draw.AddLine(from, to, colour, thickness);
  const float dx = to.x - from.x;
  const float dy = to.y - from.y;
  const float length = std::hypot(dx, dy);
  if (length < 1.0F)
  {
    return;
  }
  const float ux = dx / length;
  const float uy = dy / length;
  const float head = thickness * 4.5F;
  draw.AddTriangleFilled(
      to,
      ImVec2(to.x - (ux * head) - (uy * head * ARROW_HEAD_HALF_WIDTH),
             to.y - (uy * head) + (ux * head * ARROW_HEAD_HALF_WIDTH)),
      ImVec2(to.x - (ux * head) + (uy * head * ARROW_HEAD_HALF_WIDTH),
             to.y - (uy * head) - (ux * head * ARROW_HEAD_HALF_WIDTH)),
      colour);
}
void renderFactorContribution(std::size_t index,
                              const ContactAssessment& contact, float weight,
                              ContactJob job)
{
  const ContactFactor& factor = contact.factors.at(index);

  if (weight == 0.0F)
  {
    SandboxText::disabledText("-");
  }
  else if (index == WEAK_FOOT_FACTOR)
  {
    ImGui::TextUnformatted(
        std::format("x{:.2f}",
                    static_cast<double>(1.0F + (factor.value * weight)))
            .c_str());
  }
  else
  {
    ImGui::TextUnformatted(
        std::format("{:.3f}",
                    static_cast<double>(contact.added(index, job) + 0.0F))
            .c_str());
  }
  if (index == WEAK_FOOT_FACTOR && weight != 0.0F && ImGui::IsItemHovered())
  {
    SandboxText::tooltipText(
        std::format("Adds {:.3f} here",
                    static_cast<double>(contact.added(index, job) + 0.0F))
            .c_str());
  }
}

}  // namespace

ContactViewScene::ContactViewScene(GUIView* guiView_ptr) : GUIScene(guiView_ptr)
{
  profile = chosenProfile();
}

SceneID ContactViewScene::getID() const { return SceneID::MATCH_SANDBOX; }

void ContactViewScene::onEnter()
{
  // Reuse the launch page's fonts. Direct tool entry loads them once into
  // the current atlas, so reopening Contact Difficulty does not add font
  // sources.
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
  regularFont = font("Manrope-Regular.ttf", REGULAR_FONT_SIZE);
  boldFont = font("Manrope-Bold.ttf", HEADER_FONT_SIZE);
}

void ContactViewScene::update(float /*deltaTime*/) {}

ContactProfile ContactViewScene::chosenProfile() const
{
  const FirstTouchKnobs& touchKnobs = sandboxTuning().touch;
  const StrikeKnobs& strikeKnobs = sandboxTuning().strike;
  ContactProfile chosen =
      isTouch() ? touchContactProfile(touchKnobs)
                : strikeContactProfile(
                      strikeKnobs, static_cast<StrikeKind>(inputs.contact - 1),
                      touchKnobs);
  chosen.stretchMap = sandboxTuning().stretch;
  return chosen;
}

float ContactViewScene::reach()
{
  // As the engine measures it: his feet reach (the knob), for a touch and
  // a strike alike.
  return sandboxTuning().touch[TouchKnob::FEET_REACH];
}

bool ContactViewScene::inDeadZone(ImVec2 point) const
{
  // Every contact: he cannot touch or strike a ball in his dead zone.
  const float gap = std::hypot(point.x, point.y);
  const Vector2F toBall = gap > PROBE_DIRECTION_EPSILON
                              ? Vector2F{.x = point.x / gap, .y = point.y / gap}
                              : compass(inputs.facing);
  return inTouchDeadZone(compass(inputs.facing), toBall, gap,
                         profile.comfortGap,
                         sandboxTuning().touch[TouchKnob::DEAD_ZONE_ANGLE]);
}

ContactAssessment ContactViewScene::assess(ImVec2 point, float from,
                                           float aim) const
{
  ContactSituation situation;
  situation.facing = compass(inputs.facing);
  situation.aim = compass(aim);
  situation.intendedSpeed = inputs.intendedSpeed;
  situation.intendedLift = inputs.lift;
  situation.ballVerticalSpeed = inputs.verticalSpeed;
  const Vector2F incoming = compass(from);
  situation.ballVelocity = {.x = -incoming.x * inputs.incomingPace,
                            .y = -incoming.y * inputs.incomingPace};
  const Vector2F run = compass(inputs.ownDirection);
  situation.playerVelocity = {.x = run.x * inputs.ownSpeed,
                              .y = run.y * inputs.ownSpeed};
  situation.playerMaxSpeed = inputs.topSpeed;
  const float gap = std::hypot(point.x, point.y);
  situation.toBall = gap > PROBE_DIRECTION_EPSILON
                         ? Vector2F{.x = point.x / gap, .y = point.y / gap}
                         : situation.facing;
  situation.gap = gap;
  situation.reach = reach();
  situation.height = inputs.height;
  situation.rightFooted = inputs.rightFooted;
  // A strike is always his own ball (he chose to strike it).
  // The brain's read: only a pass meant for someone else surprises him.
  situation.surprise = isTouch() && inputs.expectation == 2 ? 1.0F : 0.0F;
  situation.pressure =
      contactPressure(inputs.opponentMetres, profile.pressureRadius);
  return assessContact(profile, situation);
}

ContactAssessment ContactViewScene::assessProbe() const
{
  return assess(inputs.probe, inputs.incomingFrom, inputs.aim);
}

ImU32 ContactViewScene::colourOf(const ContactAssessment& contact,
                                 std::span<const int> zoneFactors) const
{
  if (view == 1)
  {
    return heat(contact.angleDifficulty);
  }
  if (view == 2)
  {
    return heat(contact.weightDifficulty);
  }
  if (view == 3)
  {
    return heat(contact.verticalDifficulty);
  }
  if (view >= 4)
  {
    return heat(contact.factors.at(static_cast<std::size_t>(view - 4)).value);
  }
  // Zones: each condition in its own colour, as strong as its value; one
  // this contact gives no weight is faint.
  ImVec4 colour = BASE;
  for (const int factor : zoneFactors)
  {
    const ContactFactor& term =
        contact.factors.at(static_cast<std::size_t>(factor));
    const bool weighted = term.angleWeight != 0.0F || term.weightWeight != 0.0F;
    const float strength = term.value * (weighted ? 0.9F : 0.2F);
    const ImVec4 hue = zoneHue(factor);
    colour.x += (hue.x - BASE.x) * strength;
    colour.y += (hue.y - BASE.y) * strength;
    colour.z += (hue.z - BASE.z) * strength;
  }
  colour.x = std::min(colour.x, 1.0F);
  colour.y = std::min(colour.y, 1.0F);
  colour.z = std::min(colour.z, 1.0F);
  return ImGui::GetColorU32(colour);
}

void ContactViewScene::render()
{
  namespace Page = SandboxPageUI;
  profile = chosenProfile();
  const auto* viewport = ImGui::GetMainViewport();
  const float scale = Theme::scale();
  const Page::Style style(regularFont, scale);
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, Page::PAGE);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(PAGE_PADDING * scale, PAGE_PADDING * scale));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
  ImGui::Begin("##contact_view", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor();
  const ImVec2 available = ImGui::GetContentRegionAvail();
  const bool columns = available.x >= 1000 * scale;
  const float gutter = (BUTTON_GAP * scale) + ImGui::GetStyle().ScrollbarSize;
  const float panel =
      columns ? (available.x >= 1400 * scale ? 440.0F : 400.0F) * scale
              : available.x - gutter;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  if (columns)
  {
    ImGui::BeginChild("##contact_controls", ImVec2(panel + gutter, available.y),
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    renderPanel(panel);
    ImGui::EndChild();
    ImGui::SameLine(0, BUTTON_GAP * scale);
    renderCircle(ImVec2(ImGui::GetContentRegionAvail().x, available.y));
  }
  else
  {
    ImGui::BeginChild("##contact_controls", available,
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    renderCircle(ImVec2(panel, std::max(MIN_CIRCLE_HEIGHT * scale,
                                        available.y * NARROW_CIRCLE_SHARE)));
    renderPanel(panel);
    ImGui::EndChild();
  }
  ImGui::PopStyleVar();
  ImGui::End();
}

void ContactViewScene::renderPanel(float width)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  Page::header("Contact difficulty",
               "See how a touch or strike changes with position, ball movement "
               "and intent.",
               width, scale, boldFont, regularFont,
               "Drag the disc for contact, the inner ring for arrival, the "
               "outer ring for aim. Right-drag turns him.",
               true);
  Page::beginCard("##contact_mode", "CONTACT", width, Page::DARK, scale,
                  boldFont);
  if (Page::button("Back to sandbox", false, scale, boldFont))
  {
    guiView->popScene();
  }
  nextButton("Technique bench", scale, boldFont);
  if (Page::button("Technique bench", false, scale, boldFont))
  {
    openBench();
  }
  if (ImGui::IsItemHovered())
  {
    SandboxText::tooltipText(
        "Pass and shot tests over skill, using the same tuning.");
  }
  std::array<const char*, 1 + STRIKE_KIND_COUNT> contacts{};
  contacts.at(0) = "First touch";
  for (std::size_t kind = 0; kind < STRIKE_KIND_COUNT; ++kind)
  {
    contacts.at(kind + 1) = STRIKE_KIND_NAMES.at(kind).data();
  }
  field("Contact");
  if (SandboxPageUI::combo("##contact", &inputs.contact, contacts.data(),
                           static_cast<int>(contacts.size())))
  {
    inputs.intendedSpeed =
        TYPICAL_PACE.at(static_cast<std::size_t>(inputs.contact));
    profile = chosenProfile();
  }
  std::array<std::string, 4 + CONTACT_FACTOR_COUNT> viewNames{
      "Zones", "Angle job difficulty", "Weight job difficulty",
      "Vertical job difficulty"};
  for (std::size_t index = 0; index < CONTACT_FACTOR_COUNT; ++index)
  {
    viewNames.at(index + 4) =
        "Condition: " + std::string(CONTACT_FACTOR_LABELS.at(index));
  }
  field("Colour by");
  if (SandboxPageUI::beginCombo(
          "##colour_by", viewNames.at(static_cast<std::size_t>(view)).c_str()))
  {
    for (std::size_t index = 0; index < viewNames.size(); ++index)
    {
      if (ImGui::Selectable(viewNames.at(index).c_str(),
                            static_cast<int>(index) == view))
      {
        view = static_cast<int>(index);
      }
    }
    ImGui::EndCombo();
  }

  Page::endCard(scale);
  Page::beginCard("##contact_tabs", "DETAILS", width, Page::MID, scale,
                  boldFont);
  const std::array<const char*, 3> tabs{"Inputs", "Analysis", "Tuning"};
  for (int tab = 0; tab < 3; ++tab)
  {
    if (tab > 0)
    {
      ImGui::SameLine(0, BUTTON_GAP * scale);
    }
    if (Page::button(tabs.at(tab), panelTab == tab, scale, boldFont))
    {
      panelTab = tab;
    }
  }
  if (panelTab != 1)
  {
    const std::array<const char*, 3> sections =
        panelTab == 0
            ? std::array<const char*, 3>{"Player", "Ball", "Aim"}
            : std::array<const char*, 3>{"Summary", "Stretch", "Knobs"};
    int& selected = panelTab == 0 ? inputTab : tuningTab;
    for (int tab = 0; tab < 3; ++tab)
    {
      if (tab > 0)
      {
        ImGui::SameLine(0, BUTTON_GAP * scale);
      }
      if (Page::button(sections.at(tab), selected == tab, scale, boldFont))
      {
        selected = tab;
      }
    }
  }
  Page::endCard(scale);
  if (panelTab == 1)
  {
    renderBreakdown(width);
    return;
  }
  if (panelTab == 2)
  {
    const PanelCard& card = panelCard(TUNING_CARDS, tuningTab);
    Page::beginCard(card.id, card.title, width, Page::LIGHT, scale, boldFont);
    ImGui::PushTextWrapPos(0);
    if (tuningTab == 0)
    {
      renderTuning();
    }
    else if (tuningTab == 1)
    {
      renderStretchMap();
    }
    else
    {
      renderKnobs();
    }
    ImGui::PopTextWrapPos();
    Page::endCard(scale);
    return;
  }
  const PanelCard& card = panelCard(INPUT_CARDS, inputTab);
  Page::beginCard(card.id, card.title, width, Page::LIGHT, scale, boldFont);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                      ImVec2(FIELD_PADDING_X * scale, FIELD_PADDING_Y * scale));
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(3 * scale, 3 * scale));
  if (ImGui::BeginTable(
          "##input_fields", width >= INPUT_COLUMNS_WIDTH * scale ? 2 : 1,
          ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_RowBg |
              ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_BordersInnerV))
  {
    const auto nextField = [&](const char* label)
    {
      ImGui::TableNextColumn();
      ImGui::Dummy(ImVec2(0, 0));
      field(label);
    };
    const auto slider = [&](const char* label, float* value, float low,
                            float high, const char* format)
    {
      nextField(label);
      const std::string id = std::string("##") + label;
      return ImGui::SliderFloat(id.c_str(), value, low, high, format);
    };
    if (inputTab == 0)
    {
      slider("Facing (°)", &inputs.facing, 0.0F, FULL_TURN_DEGREES, "%.0f");
      int foot = inputs.rightFooted ? 0 : 1;
      constexpr std::array<const char*, 2> FEET{"Right", "Left"};
      nextField("Stronger foot");
      if (SandboxPageUI::combo("##foot", &foot, FEET.data(), 2))
      {
        inputs.rightFooted = foot == 0;
      }
      slider("His speed (m/s)", &inputs.ownSpeed, 0.0F, MAX_OWN_SPEED, "%.1f");
      slider("Running to (°)", &inputs.ownDirection, 0.0F, FULL_TURN_DEGREES,
             "%.0f");
      slider("Top speed (m/s)", &inputs.topSpeed, MIN_TOP_SPEED, MAX_TOP_SPEED,
             "%.1f");
    }
    else if (inputTab == 1)
    {
      slider("Comes from (°)", &inputs.incomingFrom, 0.0F, FULL_TURN_DEGREES,
             "%.0f");
      slider("Incoming pace", &inputs.incomingPace, 0.0F, MAX_BALL_PACE,
             "%.1f m/s");
      slider("Vertical speed", &inputs.verticalSpeed, MIN_VERTICAL_SPEED,
             MAX_VERTICAL_SPEED, "%+.1f m/s");
      if (ImGui::IsItemHovered())
      {
        SandboxText::tooltipText(
            "The ball's speed up (+) or down (-) as it reaches "
            "him.");
      }
      slider("Height", &inputs.height, 0.0F, MAX_BALL_HEIGHT, "%.2f m");
      const float reachMetres = reach();
      // Where he meets it, as distance and direction from his centre.
      float gap = std::hypot(inputs.probe.x, inputs.probe.y);
      float bearing =
          gap > PROBE_DIRECTION_EPSILON ? compassOf(inputs.probe) : 0.0F;
      const bool gapChanged =
          slider("Met at", &gap, 0.0F, reachMetres, "%.2f m");
      const bool bearingChanged =
          slider("Met at (°)", &bearing, 0.0F, FULL_TURN_DEGREES, "%.0f");
      if (gapChanged || bearingChanged)
      {
        const Vector2F unit = compass(bearing);
        inputs.probe = ImVec2(unit.x * gap, unit.y * gap);
      }
      nextField("Expected?");
      if (SandboxPageUI::beginCombo(
              "##expected",
              isTouch() ? EXPECTATIONS.at(
                              static_cast<std::size_t>(inputs.expectation))
                        : "He chose to strike it"))
      {
        if (isTouch())
        {
          for (std::size_t index = 0; index < EXPECTATIONS.size(); ++index)
          {
            if (ImGui::Selectable(
                    EXPECTATIONS.at(index),
                    static_cast<int>(index) == inputs.expectation))
            {
              inputs.expectation = static_cast<int>(index);
            }
          }
        }
        ImGui::EndCombo();
      }
      if (ImGui::IsItemHovered())
      {
        SandboxText::tooltipText(
            "The brain's read: only a pass meant for someone else "
            "surprises him, for now.");
      }
      slider("Nearest opponent", &inputs.opponentMetres, 0.0F,
             MAX_OPPONENT_DISTANCE, "%.1f m");
    }
    else
    {
      slider("Sends it to (°)", &inputs.aim, 0.0F, FULL_TURN_DEGREES, "%.0f");
      slider("Intended pace", &inputs.intendedSpeed, 0.0F, MAX_BALL_PACE,
             "%.1f m/s");
      if (ImGui::IsItemHovered())
      {
        SandboxText::tooltipText("Over the ground.");
      }
      slider("Lift", &inputs.lift, MIN_LIFT, MAX_LIFT, "%+.1f m/s");
      if (ImGui::IsItemHovered())
      {
        SandboxText::tooltipText(
            "The speed up (+) or down (-) he means it to leave "
            "with; 0 sends it off level, cushioning any drop.");
      }
    }
    ImGui::EndTable();
  }
  ImGui::PopStyleVar(2);
  Page::endCard(scale);
}

void ContactViewScene::renderCircle(ImVec2 size)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  Page::beginCard("##contact_diagram", "CONTACT MAP", size.x, Page::DARK, scale,
                  boldFont);
  const ImVec2 area(std::max(scale, ImGui::GetContentRegionAvail().x),
                    std::max(240 * scale, size.y - (56 * scale)));
  const ContactAssessment legendContact = assessProbe();
  const std::array<int, 3> factors{STRETCH, APPROACH, TURN};
  const std::array<const char*, 3> names{"Disc: stretch", "Arrival: approach",
                                         "Aim: turn"};
  std::array<std::string, 3> legendLabels;
  int legendRows = 1;
  float used = 0;
  for (std::size_t index = 0; index < factors.size(); ++index)
  {
    const auto& factor = legendContact.factors.at(factors.at(index));
    const bool weighted = factor.angleWeight != 0 || factor.weightWeight != 0 ||
                          factor.verticalWeight != 0;
    legendLabels.at(index) =
        std::string(names.at(index)) + (weighted ? "" : " (unweighted)");
    const float width = ImGui::CalcTextSize(legendLabels.at(index).c_str()).x;
    if (used > 0 && used + LEGEND_GAP * scale + width > area.x)
    {
      ++legendRows;
      used = 0;
    }
    used += width + LEGEND_GAP * scale;
  }
  const std::string reachLabel = std::format(
      "Reach {:.2f} m / comfort gap {:.2f} m (dashed). Directions: 0 deg up, "
      "clockwise.",
      reach(), profile.comfortGap);
  const char* guide =
      "Outer ring: > inward, >> outward; bars mean no ground line. Hover for "
      "guide.";
  const float spacing = ImGui::GetStyle().ItemSpacing.y;
  const float legendHeight =
      spacing +
      (static_cast<float>(view == 0 ? legendRows : 1) *
       ImGui::GetTextLineHeightWithSpacing()) +
      ImGui::CalcTextSize(reachLabel.c_str(), nullptr, false, area.x).y +
      spacing + ImGui::CalcTextSize(guide, nullptr, false, area.x).y + spacing;
  const float side =
      std::max(std::min(area.x, area.y - legendHeight), 100.0F * scale);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 centre(origin.x + (area.x * MIDPOINT_SHARE),
                      origin.y + (side * MIDPOINT_SHARE));
  const float radius = side * MIDPOINT_SHARE / (AIM_OUTER + 0.08F);
  const float reachMetres = reach();
  const float ppm = radius / reachMetres;
  const auto at = [&](float x, float y)
  { return ImVec2(centre.x + (x * ppm), centre.y - (y * ppm)); };
  const auto onRing = [&](float degrees, float multiple)
  {
    const Vector2F unit = compass(degrees);
    return ImVec2(centre.x + (unit.x * radius * multiple),
                  centre.y - (unit.y * radius * multiple));
  };

  ImGui::InvisibleButton(
      "##canvas", ImVec2(area.x, side),
      ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
  const bool hovered = ImGui::IsItemHovered();
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  const ImVec2 offset((mouse.x - centre.x) / ppm, (centre.y - mouse.y) / ppm);
  const float mouseRadius =
      std::hypot(mouse.x - centre.x, mouse.y - centre.y) / radius;

  // Dragging: what was grabbed keeps moving until release.
  if (ImGui::IsItemActivated())
  {
    if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
    {
      dragging = 4;
    }
    else if (mouseRadius <= DISC_HIT_RADIUS)
    {
      dragging = 1;
    }
    else if (mouseRadius <= (FROM_OUTER + AIM_INNER) * MIDPOINT_SHARE)
    {
      dragging = 2;
    }
    else if (mouseRadius <= AIM_OUTER + RING_HIT_PADDING)
    {
      dragging = 3;
    }
  }
  if (!ImGui::IsItemActive())
  {
    dragging = 0;
  }
  switch (dragging)
  {
    case 1:
    {
      const float gap = std::hypot(offset.x, offset.y);
      const float limit = reachMetres;
      inputs.probe =
          gap > limit ? ImVec2(offset.x * limit / gap, offset.y * limit / gap)
                      : offset;
      break;
    }
    case 2:
      inputs.incomingFrom = compassOf(offset);
      break;
    case 3:
      inputs.aim = compassOf(offset);
      break;
    case 4:
      inputs.facing = compassOf(offset);
      break;
    default:
      break;
  }
  // A shorter reach (the Feet reach knob turned down) pulls the ball in.
  if (const float gap = std::hypot(inputs.probe.x, inputs.probe.y);
      gap > reachMetres)
  {
    inputs.probe = ImVec2(inputs.probe.x * reachMetres / gap,
                          inputs.probe.y * reachMetres / gap);
  }

  ImDrawList& draw = *ImGui::GetWindowDrawList();

  // The rings: where it comes from (with the ball where it is met), and
  // where he sends it.
  const float step = FULL_TURN_DEGREES / RING_SEGMENTS;
  for (int segment = 0; segment < RING_SEGMENTS; ++segment)
  {
    const float from = static_cast<float>(segment) * step;
    const float to = from + step;
    const float middle = from + (step * MIDPOINT_SHARE);
    const ContactAssessment fromContact =
        assess(inputs.probe, middle, inputs.aim);
    const std::array<ImVec2, 4> fromQuad{
        onRing(from, FROM_INNER), onRing(from, FROM_OUTER),
        onRing(to, FROM_OUTER), onRing(to, FROM_INNER)};
    draw.AddConvexPolyFilled(fromQuad.data(), 4,
                             colourOf(fromContact, FROM_ZONES));
    const ContactAssessment aimContact =
        assess(inputs.probe, inputs.incomingFrom, middle);
    const std::array<ImVec2, 4> aimQuad{
        onRing(from, AIM_INNER), onRing(from, AIM_OUTER), onRing(to, AIM_OUTER),
        onRing(to, AIM_INNER)};
    draw.AddConvexPolyFilled(aimQuad.data(), 4,
                             colourOf(aimContact, AIM_ZONES));
  }

  // The disc: each cell coloured by meeting the ball there.
  const float cell = reachMetres * 2.0F / DISC_CELLS;
  draw.AddCircleFilled(centre, radius, ImGui::GetColorU32(BASE), RING_SEGMENTS);
  for (int row = 0; row < DISC_CELLS; ++row)
  {
    for (int column = 0; column < DISC_CELLS; ++column)
    {
      const float x =
          -reachMetres + ((static_cast<float>(column) + MIDPOINT_SHARE) * cell);
      const float y =
          -reachMetres + ((static_cast<float>(row) + MIDPOINT_SHARE) * cell);
      if (std::hypot(x, y) > reachMetres)
      {
        continue;
      }
      if (inDeadZone(ImVec2(x, y)))
      {
        draw.AddRectFilled(
            at(x - (cell * MIDPOINT_SHARE), y + (cell * MIDPOINT_SHARE)),
            at(x + (cell * MIDPOINT_SHARE), y - (cell * MIDPOINT_SHARE)),
            DEAD_ZONE_COLOUR);
        continue;
      }
      const ContactAssessment contact =
          assess(ImVec2(x, y), inputs.incomingFrom, inputs.aim);
      draw.AddRectFilled(
          at(x - (cell * MIDPOINT_SHARE), y + (cell * MIDPOINT_SHARE)),
          at(x + (cell * MIDPOINT_SHARE), y - (cell * MIDPOINT_SHARE)),
          colourOf(contact, DISC_ZONES));
    }
  }

  // Outlines: his reach, the comfort gap (stretching starts beyond it),
  // the rings.
  const ImU32 line = IM_COL32(255, 255, 255, 190);
  const ImU32 faint = IM_COL32(255, 255, 255, 70);
  // The square cells overhang the circle: trim them with the background.
  const float overhang = cell * ppm * 1.5F;
  draw.AddCircle(centre, radius + (overhang * MIDPOINT_SHARE),
                 ImGui::GetColorU32(ImGuiCol_WindowBg), RING_SEGMENTS,
                 overhang);
  draw.AddCircle(centre, radius, line, RING_SEGMENTS, NORMAL_STROKE * scale);
  const float comfort = profile.comfortGap * ppm;
  for (int dash = 0; dash < DASH_SEGMENTS; dash += 2)
  {
    const float a = static_cast<float>(dash) /
                    static_cast<float>(HEAT_BAR_STEPS) * 2.0F *
                    std::numbers::pi_v<float>;
    const float b = static_cast<float>(dash + 1) /
                    static_cast<float>(HEAT_BAR_STEPS) * 2.0F *
                    std::numbers::pi_v<float>;
    draw.PathArcTo(centre, comfort, a, b, 4);
    draw.PathStroke(line, 0, THIN_STROKE * scale);
  }
  // The outer ring's turns: chevrons pointing the way sending it there
  // turns the ball off its line, clockwise or anticlockwise; one for an
  // inward turn, two for outward, none where it is no turn. A ball with
  // no line across the ground (still, or straight up or down) has no
  // turn to point along: bars, one for inward, two for outward.
  constexpr int CHEVRONS = 24;
  const float chevron = radius * (AIM_OUTER - AIM_INNER) * 0.26F;
  for (int mark = 0; mark < CHEVRONS; ++mark)
  {
    const float degrees = (static_cast<float>(mark) + MIDPOINT_SHARE) *
                          FULL_TURN_DEGREES / CHEVRONS;
    const ContactAssessment contact =
        assess(inputs.probe, inputs.incomingFrom, degrees);
    const bool noLine = !contact.moving || std::cos(contact.lineElevation) <=
                                               PROBE_DIRECTION_EPSILON;
    // None in a slot holding a direction that is no turn (straight back
    // or straight on), so the gap shows where the ball keeps its line.
    if (!noLine &&
        std::abs(contact.turn) < radians(HALF_TURN_DEGREES / CHEVRONS))
    {
      continue;
    }
    // Along the ring on screen: a positive turn is anticlockwise.
    const float angle = radians(degrees);
    const float way = contact.turn > 0.0F ? -1.0F : 1.0F;
    const ImVec2 along(std::cos(angle) * way, std::sin(angle) * way);
    const ImVec2 out(std::sin(angle), -std::cos(angle));
    const ImVec2 middle =
        onRing(degrees, (AIM_INNER + AIM_OUTER) * MIDPOINT_SHARE);
    const int count = contact.outward ? 2 : 1;
    const float depth = chevron * 0.8F;
    const float spacing = chevron * 0.75F;
    for (int index = 0; index < count; ++index)
    {
      const float shift = (static_cast<float>(index) -
                           static_cast<float>(count - 1) * MIDPOINT_SHARE) *
                          spacing;
      if (noLine)
      {
        const ImVec2 centre(middle.x + (along.x * shift),
                            middle.y + (along.y * shift));
        const ImVec2 a(centre.x + (out.x * chevron),
                       centre.y + (out.y * chevron));
        const ImVec2 b(centre.x - (out.x * chevron),
                       centre.y - (out.y * chevron));
        draw.AddLine(a, b, IM_COL32(0, 0, 0, 170),
                     CHEVRON_OUTLINE_STROKE * scale);
        draw.AddLine(a, b, IM_COL32(255, 255, 255, 235),
                     CHEVRON_STROKE * scale);
        continue;
      }
      const ImVec2 tip(middle.x + (along.x * (shift + depth * MIDPOINT_SHARE)),
                       middle.y + (along.y * (shift + depth * MIDPOINT_SHARE)));
      const ImVec2 back(
          middle.x + (along.x * (shift - depth * MIDPOINT_SHARE)),
          middle.y + (along.y * (shift - depth * MIDPOINT_SHARE)));
      const std::array<ImVec2, 3> points{
          ImVec2(back.x + (out.x * chevron), back.y + (out.y * chevron)), tip,
          ImVec2(back.x - (out.x * chevron), back.y - (out.y * chevron))};
      draw.AddPolyline(points.data(), 3, IM_COL32(0, 0, 0, 170), 0,
                       CHEVRON_OUTLINE_STROKE * scale);
      draw.AddPolyline(points.data(), 3, IM_COL32(255, 255, 255, 235), 0,
                       CHEVRON_STROKE * scale);
    }
  }
  // The dead zone's edges, from the comfort gap out to his reach.
  const float deadAngle = sandboxTuning().touch[TouchKnob::DEAD_ZONE_ANGLE];
  for (const float sideOf : {-1.0F, 1.0F})
  {
    const Vector2F edge = compass(inputs.facing + (sideOf * deadAngle));
    draw.AddLine(at(edge.x * profile.comfortGap, edge.y * profile.comfortGap),
                 at(edge.x * reachMetres, edge.y * reachMetres), line,
                 THIN_STROKE * scale);
  }
  for (const float multiple : {FROM_INNER, FROM_OUTER, AIM_INNER, AIM_OUTER})
  {
    draw.AddCircle(centre, radius * multiple, faint, RING_SEGMENTS,
                   1.0F * scale);
  }

  // Him: shoulders across his facing, the facing arrow, his feet (the
  // stronger one marked), and his run.
  const Vector2F facing = compass(inputs.facing);
  const Vector2F right{.x = facing.y, .y = -facing.x};
  const float shoulder = 0.22F;
  draw.AddLine(at(-right.x * shoulder, -right.y * shoulder),
               at(right.x * shoulder, right.y * shoulder),
               IM_COL32(235, 235, 235, 255), SHOULDER_STROKE * scale);
  draw.AddCircleFilled(at(0.0F, 0.0F), CENTRE_RADIUS_METRES * ppm,
                       IM_COL32(235, 235, 235, 255));
  arrow(draw,
        at(facing.x * FACING_START_METRES, facing.y * FACING_START_METRES),
        onRing(inputs.facing, AIM_OUTER + FACING_RING_OVERHANG),
        IM_COL32(255, 255, 255, 230), FACING_STROKE * scale);
  for (const float sideOf : {-1.0F, 1.0F})
  {
    const bool stronger = (sideOf > 0.0F) == inputs.rightFooted;
    const ImVec2 foot = at((right.x * 0.13F * sideOf) + (facing.x * 0.06F),
                           (right.y * 0.13F * sideOf) + (facing.y * 0.06F));
    draw.AddCircleFilled(
        foot, FOOT_RADIUS_METRES * ppm,
        stronger ? IM_COL32(120, 255, 140, 255) : IM_COL32(170, 170, 170, 255));
    const char* label = sideOf > 0.0F ? "R" : "L";
    draw.AddText(ImVec2(foot.x - (FOOT_LABEL_OFFSET_PIXELS * scale),
                        foot.y + (FOOT_LABEL_OFFSET_METRES * ppm)),
                 IM_COL32(255, 255, 255, 220), label);
  }
  if (inputs.ownSpeed > MIN_ARROW_SPEED)
  {
    const Vector2F run = compass(inputs.ownDirection);
    const float length =
        reachMetres * 0.8F * std::min(inputs.ownSpeed / inputs.topSpeed, 1.0F);
    arrow(draw, at(0.0F, 0.0F), at(run.x * length, run.y * length),
          IM_COL32(120, 220, 255, 230), NORMAL_STROKE * scale);
  }

  // The ball, the way it comes, and where he sends it.
  const ImVec2 ball = at(inputs.probe.x, inputs.probe.y);
  if (inputs.incomingPace > MIN_ARROW_SPEED)
  {
    arrow(
        draw,
        onRing(inputs.incomingFrom, (FROM_INNER + FROM_OUTER) * MIDPOINT_SHARE),
        ball, IM_COL32(255, 255, 255, 150), THIN_STROKE * scale);
  }
  // The ball-contact line (his centre through the ball to his reach), and
  // the ball's path along his aim to where it leaves his reach: in or out
  // is read from these and his facing line.
  const float probeGap = std::hypot(inputs.probe.x, inputs.probe.y);
  if (probeGap > PROBE_DIRECTION_EPSILON)
  {
    draw.AddLine(at(0.0F, 0.0F),
                 at(inputs.probe.x / probeGap * reachMetres,
                    inputs.probe.y / probeGap * reachMetres),
                 IM_COL32(255, 255, 255, 110), THIN_STROKE * scale);
  }
  const Vector2F aimUnit = compass(inputs.aim);
  const float along =
      (inputs.probe.x * aimUnit.x) + (inputs.probe.y * aimUnit.y);
  const float exitDistance =
      -along + std::sqrt(std::max((along * along) - (probeGap * probeGap) +
                                      (reachMetres * reachMetres),
                                  0.0F));
  const ImVec2 exit = at(inputs.probe.x + (aimUnit.x * exitDistance),
                         inputs.probe.y + (aimUnit.y * exitDistance));
  arrow(draw, ball, exit, IM_COL32(255, 255, 255, 230), NORMAL_STROKE * scale);
  draw.AddCircleFilled(exit, AIM_POINT_RADIUS * scale,
                       IM_COL32(255, 255, 255, 255));
  // On to the handle on the outer ring, faintly.
  draw.AddLine(exit,
               onRing(inputs.aim, (AIM_INNER + AIM_OUTER) * MIDPOINT_SHARE),
               IM_COL32(255, 255, 255, 70), 1.0F * scale);
  draw.AddCircleFilled(
      ball, std::max(BALL_RADIUS_METRES * ppm, MIN_BALL_RADIUS_PIXELS * scale),
      IM_COL32(255, 255, 255, 255));
  draw.AddCircle(
      ball, std::max(BALL_RADIUS_METRES * ppm, MIN_BALL_RADIUS_PIXELS * scale),
      IM_COL32(0, 0, 0, 255), 0, THIN_STROKE * scale);
  for (const auto& [degrees, multiple] :
       {std::pair{inputs.incomingFrom,
                  (FROM_INNER + FROM_OUTER) * MIDPOINT_SHARE},
        std::pair{inputs.aim, (AIM_INNER + AIM_OUTER) * MIDPOINT_SHARE}})
  {
    draw.AddCircleFilled(onRing(degrees, multiple), HANDLE_RADIUS * scale,
                         IM_COL32(255, 255, 255, 255));
    draw.AddCircle(onRing(degrees, multiple), HANDLE_RADIUS * scale,
                   IM_COL32(0, 0, 0, 255), 0, THIN_STROKE * scale);
  }

  // Hovering reads one point.
  if (hovered && dragging == 0)
  {
    std::optional<ContactAssessment> contact;
    std::string where;
    if (mouseRadius <= 1.0F)
    {
      contact = assess(offset, inputs.incomingFrom, inputs.aim);
      where = std::format("Met {:.2f} m from his centre, at {:.0f}°",
                          std::hypot(offset.x, offset.y), compassOf(offset));
      if (inDeadZone(offset))
      {
        where += "\nDead zone: he cannot play it here";
      }
    }
    else if (mouseRadius >= FROM_INNER && mouseRadius <= FROM_OUTER)
    {
      contact = assess(inputs.probe, compassOf(offset), inputs.aim);
      where = std::format("Coming from {:.0f}°", compassOf(offset));
    }
    else if (mouseRadius >= AIM_INNER && mouseRadius <= AIM_OUTER)
    {
      contact = assess(inputs.probe, inputs.incomingFrom, compassOf(offset));
      where = std::format("Sent to {:.0f}°", compassOf(offset));
    }
    if (contact)
    {
      std::string tip = std::format(
          "{}\nAngle job {:.2f} · weight job {:.2f} · vertical job "
          "{:.2f}\nTurn "
          "{:.0f}° {} · pace change {:+.1f} m/s",
          where, contact->angleDifficulty, contact->weightDifficulty,
          contact->verticalDifficulty,
          std::abs(contact->turn) * HALF_TURN_DEGREES /
              std::numbers::pi_v<float>,
          contact->outward ? "outward" : "inward", contact->paceChange);
      for (const ContactFactor& factor : contact->factors)
      {
        if (factor.value > VISIBLE_FACTOR_THRESHOLD)
        {
          tip += std::format("\n  {:<26} {:.2f}", factor.name, factor.value);
        }
      }
      SandboxText::tooltipText(tip.c_str());
    }
  }

  // Legend.
  ImGui::Spacing();
  if (view == 0)
  {
    float rowWidth = 0;
    for (std::size_t index = 0; index < factors.size(); ++index)
    {
      const float width = ImGui::CalcTextSize(legendLabels.at(index).c_str()).x;
      if (rowWidth > 0 && rowWidth + LEGEND_GAP * scale + width <= area.x)
      {
        ImGui::SameLine(0, LEGEND_GAP * scale);
      }
      else
      {
        rowWidth = 0;
      }
      const auto& factor = legendContact.factors.at(factors.at(index));
      const bool weighted = factor.angleWeight != 0 ||
                            factor.weightWeight != 0 ||
                            factor.verticalWeight != 0;
      SandboxText::coloredText(
          weighted ? zoneHue(factors.at(index))
                   : ImGui::ColorConvertU32ToFloat4(Page::MUTED),
          legendLabels.at(index).c_str());
      if (ImGui::IsItemHovered())
      {
        SandboxText::tooltipText(
            std::format(
                "{} / weaker foot can be selected separately with Colour by.",
                DEPENDS_ON.at(factors.at(index)))
                .c_str());
      }
      rowWidth += width + LEGEND_GAP * scale;
    }
  }
  else
  {
    SandboxText::disabledText("0");
    ImGui::SameLine();
    const ImVec2 bar = ImGui::GetCursorScreenPos();
    const float barWidth = std::min(240 * scale, area.x * 0.4F);
    const float barHeight = ImGui::GetTextLineHeight();
    for (int index = 0; index < HEAT_BAR_STEPS; ++index)
    {
      draw.AddRectFilled(
          ImVec2(bar.x + (barWidth * static_cast<float>(index) /
                          static_cast<float>(HEAT_BAR_STEPS)),
                 bar.y),
          ImVec2(bar.x + (barWidth * static_cast<float>(index + 1) /
                          static_cast<float>(HEAT_BAR_STEPS)),
                 bar.y + barHeight),
          heat(float(index) / (HEAT_BAR_STEPS - 1)));
    }
    ImGui::Dummy(ImVec2(barWidth, barHeight));
    ImGui::SameLine();
    SandboxText::disabledText("1 / same scale throughout");
  }
  SandboxText::wrappedText(reachLabel.c_str());
  ImGui::PushTextWrapPos(0);
  SandboxText::disabledText(guide);
  ImGui::PopTextWrapPos();
  if (ImGui::IsItemHovered())
  {
    SandboxText::tooltipText(
        "Outer-ring chevrons show how sending it there turns the ball off its "
        "line, clockwise or anticlockwise. > is inward; >> is outward; no mark "
        "means no turn. Bars (| inward, || outward) appear when the ball has "
        "no line across the ground.");
  }
  Page::endCard(scale);
}

void ContactViewScene::renderBreakdown(float width)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  Page::beginCard("##contact_analysis", "AT THE BALL", width, Page::MID, scale,
                  boldFont);
  ImGui::PushTextWrapPos(0);
  const ContactAssessment contact = assessProbe();

  if (inDeadZone(inputs.probe))
  {
    SandboxText::coloredText(
        DEAD_ZONE_TINT,
        "In his dead zone: he cannot play it here (touch or strike).");
  }
  ImGui::TextUnformatted(
      std::format(
          "Turn {:.0f}° {}{} · pace change {:+.1f} m/s",
          static_cast<double>(std::abs(contact.turn) * HALF_TURN_DEGREES /
                              std::numbers::pi_v<float>),
          contact.outward ? "outward" : "inward",
          contact.rebound ? " (back the way it came)" : "",
          static_cast<double>(contact.paceChange))
          .c_str());
  ImGui::TextUnformatted(
      std::format(
          "Steepness: line {:+.0f}°, he means {:+.0f}°: turn {:.0f}° {}",
          static_cast<double>((contact.lineElevation * HALF_TURN_DEGREES /
                               std::numbers::pi_v<float>)+0.0F),
          static_cast<double>((contact.intendedElevation * HALF_TURN_DEGREES /
                               std::numbers::pi_v<float>)+0.0F),
          static_cast<double>(std::abs(contact.verticalTurn) *
                              HALF_TURN_DEGREES / std::numbers::pi_v<float>),
          verticalTurnLabel(contact.verticalTurn))
          .c_str());
  renderSideView(contact);
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 3 * scale));
  if (ImGui::BeginTable("##job_difficulty", 2, ImGuiTableFlags_SizingFixedFit))
  {
    ImGui::TableSetupColumn("Job", ImGuiTableColumnFlags_WidthFixed,
                            JOB_COLUMN_WIDTH * scale);
    ImGui::TableSetupColumn("Difficulty", ImGuiTableColumnFlags_WidthStretch);
    for (const auto& [label, value] :
         {std::pair{"Angle job", contact.angleDifficulty},
          std::pair{"Weight job", contact.weightDifficulty},
          std::pair{"Vertical job", contact.verticalDifficulty}})
    {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(label);
      ImGui::TableNextColumn();
      const ImVec2 start = ImGui::GetCursorScreenPos();
      const float width =
          std::max(scale, ImGui::GetContentRegionAvail().x - (50 * scale));
      const float top =
          start.y + ((ImGui::GetTextLineHeight() - 8 * scale) * MIDPOINT_SHARE);
      auto* draw = ImGui::GetWindowDrawList();
      draw->AddRectFilled(
          ImVec2(start.x, top),
          ImVec2(start.x + width, top + (DIFFICULTY_BAR_HEIGHT * scale)),
          Page::RULE);
      draw->AddRectFilled(ImVec2(start.x, top),
                          ImVec2(start.x + (width * value),
                                 top + (DIFFICULTY_BAR_HEIGHT * scale)),
                          heat(value));
      ImGui::Dummy(ImVec2(width, ImGui::GetTextLineHeight()));
      ImGui::SameLine(0, BUTTON_GAP * scale);
      ImGui::TextUnformatted(
          std::format("{:.2f}", static_cast<double>(value)).c_str());
    }
    ImGui::EndTable();
  }
  ImGui::PopStyleVar();

  renderFactors(contact);
  ImGui::PushTextWrapPos(0.0F);
  SandboxText::disabledText(
      "Each condition is 0-1; its weights say what it adds to each job. "
      "Grey: no weight for this contact. Hover a condition for what it "
      "reads. Difficulty then sets how often a job fails and how badly "
      "(the technique knobs, cut by his skill).");
  ImGui::PopTextWrapPos();
  ImGui::PopTextWrapPos();
  Page::endCard(scale);
}

void ContactViewScene::renderStretchMap()
{
  const float scale = Theme::scale();
  namespace Page = SandboxPageUI;
  ImGui::PushTextWrapPos(0.0F);
  SandboxText::disabledText(
      "Stretch (0-1) by angle off his facing (rows, either side) and "
      "distance from his centre as a share of his reach (columns; metres "
      "for a first touch at feet height). Blended between points. Drag, or "
      "Ctrl+click to type. Used for every contact here and in the technique "
      "bench's runs; the game keeps its own until the values are copied in.");
  ImGui::PopTextWrapPos();
  const ImGuiTableFlags flags =
      ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame;
  ImGui::PushFont(regularFont, HEADER_FONT_SIZE);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4 * scale, 3 * scale));
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(3 * scale, 3 * scale));
  if (ImGui::BeginTable("##stretch_map",
                        static_cast<int>(StretchMap::DISTANCES) + 1, flags))
  {
    // Header: the distances, editable, kept in order.
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    ImGui::TableNextColumn();
    SandboxText::disabledText("Angle");
    for (std::size_t column = 0; column < StretchMap::DISTANCES; ++column)
    {
      ImGui::TableNextColumn();
      ImGui::PushID(static_cast<int>(column));
      ImGui::SetNextItemWidth(-1.0F);
      const float low =
          column == 0 ? 0.0F : sandboxTuning().stretch.distances.at(column - 1);
      const float high = column + 1 == StretchMap::DISTANCES
                             ? 1.0F
                             : sandboxTuning().stretch.distances.at(column + 1);
      if (ImGui::DragFloat("##distance",
                           &sandboxTuning().stretch.distances.at(column),
                           DISTANCE_DRAG_SPEED, low, high, "%.2f",
                           ImGuiSliderFlags_AlwaysClamp))
      {
        profile = chosenProfile();
      }
      ImGui::PopID();
    }
    // Rows: the angle, then the stretch at each distance.
    for (std::size_t row = 0; row < StretchMap::ANGLES; ++row)
    {
      ImGui::TableNextRow();
      ImGui::PushID(static_cast<int>(ANGLE_ROW_ID_BASE + row));
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1.0F);
      const float low =
          row == 0 ? 0.0F : sandboxTuning().stretch.angles.at(row - 1);
      const float high = row + 1 == StretchMap::ANGLES
                             ? HALF_TURN_DEGREES
                             : sandboxTuning().stretch.angles.at(row + 1);
      if (ImGui::DragFloat("##angle", &sandboxTuning().stretch.angles.at(row),
                           ANGLE_DRAG_SPEED, low, high, "%.0f°",
                           ImGuiSliderFlags_AlwaysClamp))
      {
        profile = chosenProfile();
      }
      for (std::size_t column = 0; column < StretchMap::DISTANCES; ++column)
      {
        ImGui::TableNextColumn();
        float& value = sandboxTuning().stretch.values.at(row).at(column);
        ImVec4 shade = ImGui::ColorConvertU32ToFloat4(heat(value));
        shade.w = STRETCH_CELL_ALPHA;
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
                               ImGui::GetColorU32(shade));
        ImGui::PushID(static_cast<int>(column));
        ImGui::SetNextItemWidth(-1.0F);
        if (ImGui::DragFloat("##value", &value, STRETCH_DRAG_SPEED, 0.0F, 1.0F,
                             "%.2f", ImGuiSliderFlags_AlwaysClamp))
        {
          profile = chosenProfile();
        }
        ImGui::PopID();
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  ImGui::PopStyleVar(2);
  ImGui::PopFont();
  if (Page::button("Reset to game", false, scale, boldFont))
  {
    sandboxTuning().stretch = StretchMap{};
    profile = chosenProfile();
  }
  nextButton("Copy values", scale, boldFont);
  if (Page::button("Copy values", false, scale, boldFont))
  {
    // As the MatchTuning::FirstTouch constants, ready to paste in.
    std::string text =
        "    static constexpr std::array<float, 6> STRETCH_ANGLES{";
    for (std::size_t row = 0; row < StretchMap::ANGLES; ++row)
    {
      text += std::format("{}{:.1f}f", row == 0 ? "" : ", ",
                          sandboxTuning().stretch.angles.at(row));
    }
    text += "};\n    static constexpr std::array<float, 6> STRETCH_DISTANCES{";
    for (std::size_t column = 0; column < StretchMap::DISTANCES; ++column)
    {
      text += std::format("{}{:.3f}f", column == 0 ? "" : ", ",
                          sandboxTuning().stretch.distances.at(column));
    }
    text +=
        "};\n    static constexpr std::array<std::array<float, 6>, 6> "
        "STRETCH_VALUES{{\n";
    for (std::size_t row = 0; row < StretchMap::ANGLES; ++row)
    {
      text += "        {";
      for (std::size_t column = 0; column < StretchMap::DISTANCES; ++column)
      {
        text += std::format("{}{:.3f}f", column == 0 ? "" : ", ",
                            sandboxTuning().stretch.values.at(row).at(column));
      }
      text += "},\n";
    }
    text += "    }};\n";
    ImGui::SetClipboardText(text.c_str());
  }
  if (ImGui::IsItemHovered())
  {
    SandboxText::tooltipText(
        "Copies the map as the MatchTuning constants, to paste "
        "in (or to me) to make it the game's.");
  }
}

void ContactViewScene::renderSideView(const ContactAssessment& contact) const
{
  // Seen from the side, along the ball's line: it arrives from the left at
  // its steepness and leaves at the steepness he means (to the left when he
  // sends it back the way it came). The ground and the height he meets it
  // at are to scale; the arrows show direction only.
  const float scale = Theme::scale();
  const float width = ImGui::GetContentRegionAvail().x;
  const float height = 96.0F * scale;
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(width, height));
  ImDrawList& draw = *ImGui::GetWindowDrawList();
  draw.AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height),
                     IM_COL32(255, 255, 255, 10));
  const float ground = origin.y + height - (12.0F * scale);
  const float metre = 26.0F * scale;
  draw.AddLine(ImVec2(origin.x, ground), ImVec2(origin.x + width, ground),
               IM_COL32(120, 200, 120, 200), NORMAL_STROKE * scale);
  const ImVec2 ball(origin.x + (width * MIDPOINT_SHARE),
                    ground - (std::min(inputs.height, 2.5F) * metre));
  const float arm = 44.0F * scale;
  const auto towards = [&](float elevation, float side)
  {
    return ImVec2(ball.x + (std::cos(elevation) * arm * side),
                  ball.y - (std::sin(elevation) * arm));
  };
  // Arriving: its travel's steepness.
  const float level = inputs.incomingPace;
  if (contact.moving)
  {
    const float arrival = std::atan2(inputs.verticalSpeed, level);
    const ImVec2 from = towards(arrival + std::numbers::pi_v<float>, 1.0F);
    arrow(draw, ImVec2(from.x, from.y), ball, IM_COL32(255, 255, 255, 150),
          THIN_STROKE * scale);
  }
  // Leaving: the steepness he means, back the way it came or on.
  const ImVec2 to =
      towards(contact.intendedElevation, contact.rebound ? -1.0F : 1.0F);
  arrow(draw, ball, to, IM_COL32(255, 255, 255, 235), NORMAL_STROKE * scale);
  draw.AddCircleFilled(ball, SIDE_BALL_RADIUS * scale,
                       IM_COL32(255, 255, 255, 255));
  draw.AddText(ImVec2(origin.x + (SIDE_LABEL_PADDING_X * scale),
                      origin.y + (SIDE_LABEL_PADDING_Y * scale)),
               IM_COL32(170, 170, 170, 255), "Side view");
}

void ContactViewScene::renderKnobs()
{
  // The chosen contact's knobs, by group; a changed one in the warning
  // colour, with a button back to the game's value.
  const float scale = Theme::scale();
  TechniqueTuning& tuning = sandboxTuning();
  const std::string title =
      isTouch() ? std::string("Knobs: touch (first and dribble touches)")
                : std::format("Knobs: {}",
                              STRIKE_KIND_NAMES.at(static_cast<std::size_t>(
                                  inputs.contact - 1)));
  namespace Page = SandboxPageUI;
  SandboxText::wrappedText(title.c_str());
  ImGui::PushTextWrapPos(0.0F);
  SandboxText::disabledText(
      "The conditions' weights, how they are measured, and the technique "
      "(how often a job fails and how badly, cut by skill). Drag, or "
      "Ctrl+click to type (beyond the range if need be). Shared with the "
      "technique bench.");
  ImGui::PopTextWrapPos();
  const auto row = [&](std::string_view name, std::string_view unit,
                       float& value, float fallback, float low, float high)
  {
    ImGui::PushID(name.data(), name.data() + name.size());
    const bool changed = std::abs(value - fallback) > 1e-6F;
    const std::string label =
        std::format("{}{}{}", name, unit.empty() ? "" : " ", unit);
    SandboxText::wrappedText(label.c_str());
    ImGui::SetNextItemWidth(changed
                                ? -Page::buttonWidth("Reset", scale, boldFont) -
                                      (BUTTON_GAP * scale)
                                : -1);
    if (changed)
    {
      ImGui::PushStyleColor(ImGuiCol_Text, MODIFIED_TINT);
    }
    ImGui::DragFloat("##value", &value, (high - low) / KNOB_DRAG_DIVISOR, 0.0F,
                     0.0F, "%.4g");
    if (changed)
    {
      ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered())
    {
      SandboxText::tooltipText(
          std::format("The game's value: {:.6g} (range {:.6g} to {:.6g})",
                      static_cast<double>(fallback), static_cast<double>(low),
                      static_cast<double>(high))
              .c_str());
    }
    if (changed)
    {
      ImGui::SameLine();
      if (Page::button("Reset", false, scale, boldFont))
      {
        value = fallback;
      }
    }
    ImGui::PopID();
  };
  // Each group once, in the order it first appears, with all its knobs
  // (the registries are not sorted by group: the strike knobs come back to
  // a group further down).
  const auto groups = [](const auto& registry)
  {
    std::vector<std::string_view> found;
    for (const auto& info : registry)
    {
      if (std::ranges::find(found, info.group) == found.end())
      {
        found.push_back(info.group);
      }
    }
    return found;
  };
  if (isTouch())
  {
    const FirstTouchKnobs defaults;
    for (const std::string_view group : groups(TOUCH_KNOBS))
    {
      if (!ImGui::TreeNodeEx(std::format("{}##touch", group).c_str(),
                             ImGuiTreeNodeFlags_NoTreePushOnOpen))
      {
        continue;
      }
      for (std::size_t index = 0; index < TOUCH_KNOB_COUNT; ++index)
      {
        const TouchKnobInfo& info = TOUCH_KNOBS.at(index);
        if (info.group == group)
        {
          row(info.name, info.unit, tuning.touch.values.at(index),
              defaults.values.at(index), info.min, info.max);
        }
      }
    }
  }
  else
  {
    const auto kind = static_cast<StrikeKind>(inputs.contact - 1);
    for (const std::string_view group : groups(STRIKE_KNOBS))
    {
      if (!ImGui::TreeNodeEx(std::format("{}##strike", group).c_str(),
                             ImGuiTreeNodeFlags_NoTreePushOnOpen))
      {
        continue;
      }
      for (std::size_t index = 0; index < STRIKE_KNOB_COUNT; ++index)
      {
        const StrikeKnobInfo& info = STRIKE_KNOBS.at(index);
        const auto knob = static_cast<StrikeKnob>(index);
        if (info.group == group)
        {
          row(info.name, info.unit, tuning.strike.at(kind, knob),
              strikeKnobDefault(kind, knob), info.min, info.max);
        }
      }
    }
  }
  profile = chosenProfile();
}

void ContactViewScene::renderTuning()
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  TechniqueTuning& tuning = sandboxTuning();
  const std::vector<std::string> changes = tuning.changes();
  if (changes.empty())
  {
    ImGui::TextUnformatted("The game's values.");
  }
  else
  {
    SandboxText::coloredText(
        MODIFIED_TINT,
        std::format("{} changes from the game's values.", changes.size())
            .c_str());
  }
  if (!changes.empty() && ImGui::IsItemHovered())
  {
    std::string tip;
    for (const std::string& line : changes)
    {
      tip += line + "\n";
    }
    SandboxText::tooltipText(tip.c_str());
  }
  ImGui::BeginDisabled(changes.empty());
  if (Page::button("Copy", false, scale, boldFont))
  {
    ImGui::SetClipboardText(tuning.changesText().c_str());
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
  {
    SandboxText::tooltipText(
        "Every change as text, to copy into match_tuning.h.");
  }
  nextButton("Reset all", scale, boldFont);
  if (Page::button("Reset all", false, scale, boldFont))
  {
    tuning = TechniqueTuning{};
    profile = chosenProfile();
  }
  ImGui::EndDisabled();
}

void ContactViewScene::openBench()
{
  // Back to it if this view was opened from the bench, so the two don't
  // stack up.
  if (dynamic_cast<TechniqueBenchScene*>(guiView->getSceneBelowTop()) !=
      nullptr)
  {
    guiView->popScene();
  }
  else
  {
    guiView->overlayScene(std::make_unique<TechniqueBenchScene>(guiView));
  }
}

void ContactViewScene::renderFactors(const ContactAssessment& contact) const
{
  const float scale = Theme::scale();
  ImGui::SeparatorText("Conditions");
  const ImGuiTableFlags flags =
      ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
      ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(3 * scale, 3 * scale));
  if (ImGui::BeginTable("##factors", FACTOR_COLUMN_COUNT, flags))
  {
    ImGui::TableSetupColumn("Condition", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed,
                            VALUE_COLUMN_WIDTH * scale);
    ImGui::TableSetupColumn("Angle", ImGuiTableColumnFlags_WidthFixed,
                            WEIGHT_COLUMN_WIDTH * scale);
    ImGui::TableSetupColumn("Weight", ImGuiTableColumnFlags_WidthFixed,
                            VERTICAL_WEIGHT_COLUMN_WIDTH * scale);
    ImGui::TableSetupColumn("Vertical", ImGuiTableColumnFlags_WidthFixed,
                            WEIGHT_COLUMN_WIDTH * scale);
    ImGui::PushFont(boldFont, HEADER_FONT_SIZE);
    ImGui::TableHeadersRow();
    ImGui::PopFont();
    for (std::size_t index = 0; index < CONTACT_FACTOR_COUNT; ++index)
    {
      const ContactFactor& factor = contact.factors.at(index);
      const bool weighted = factor.angleWeight != 0.0F ||
                            factor.weightWeight != 0.0F ||
                            factor.verticalWeight != 0.0F;
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      if (weighted)
      {
        ImGui::TextUnformatted(factor.name);
      }
      else
      {
        SandboxText::disabledText(factor.name);
      }
      if (ImGui::IsItemHovered())
      {
        SandboxText::tooltipText(
            std::format("Reads: {}\nWeights: angle {:.2f}, weight {:.2f}, "
                        "vertical {:.2f}",
                        DEPENDS_ON.at(index),
                        static_cast<double>(factor.angleWeight),
                        static_cast<double>(factor.weightWeight),
                        static_cast<double>(factor.verticalWeight))
                .c_str());
      }
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(
          std::format("{:.2f}", static_cast<double>(factor.value + 0.0F))
              .c_str());
      // The value as a small bar.
      const ImVec2 start = ImGui::GetCursorScreenPos();
      const float barWidth = std::min(VALUE_COLUMN_WIDTH * scale,
                                      ImGui::GetContentRegionAvail().x);
      const float barHeight =
          ImGui::GetTextLineHeight() * FACTOR_BAR_HEIGHT_SHARE;
      const float top = start.y + (ImGui::GetTextLineHeight() * 0.2F);
      ImDrawList& draw = *ImGui::GetWindowDrawList();
      draw.AddRectFilled(ImVec2(start.x, top),
                         ImVec2(start.x + barWidth, top + barHeight),
                         IM_COL32(255, 255, 255, 30));
      draw.AddRectFilled(
          ImVec2(start.x, top),
          ImVec2(start.x + (barWidth * factor.value), top + barHeight),
          weighted ? heat(factor.value) : IM_COL32(150, 150, 150, 120));
      ImGui::Dummy(ImVec2(barWidth, ImGui::GetTextLineHeight()));

      // What it actually added; the weaker foot scales the rest (x1.10).
      ImGui::TableNextColumn();
      renderFactorContribution(index, contact, factor.angleWeight,
                               ContactJob::ANGLE);
      ImGui::TableNextColumn();
      renderFactorContribution(index, contact, factor.weightWeight,
                               ContactJob::WEIGHT);
      ImGui::TableNextColumn();
      renderFactorContribution(index, contact, factor.verticalWeight,
                               ContactJob::VERTICAL);
    }
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Job difficulty (0-1)");
    ImGui::TableNextColumn();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(
        std::format("{:.3f}", static_cast<double>(contact.angleDifficulty))
            .c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(
        std::format("{:.3f}", static_cast<double>(contact.weightDifficulty))
            .c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(
        std::format("{:.3f}", static_cast<double>(contact.verticalDifficulty))
            .c_str());
    ImGui::EndTable();
  }
  ImGui::PopStyleVar();
}
