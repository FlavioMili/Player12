// Football Management Project — see LICENSE in the project root.
#include "tools/match_sandbox_setup_ui.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

#include "gui/widgets/text.h"
#include "model/match_tuning.h"
#include "tools/match_sandbox_setup.h"
#include "tools/match_sandbox_typography.h"

namespace
{
// Design pixels, from player-profile-2/SPEC.md §§2–4 and 6.
// Settings need the reference's compact 440px card, not its wider attribute
// grid.
constexpr float GAP = 6, HOME_W = 440, AWAY_W = 440, SIDE_W = 440;
constexpr float HEADER_H = 160, ENGINE_H = 268, SHARED_H = 404;
constexpr float SIMULATION_H = 400, NOTE_H = 112;
constexpr ImU32 PAGE = IM_COL32(22, 19, 16, 255);
constexpr ImU32 LIGHT = IM_COL32(42, 35, 28, 255);
constexpr ImU32 MID = IM_COL32(31, 26, 21, 255);
constexpr ImU32 DARK = IM_COL32(15, 13, 11, 255);
constexpr ImU32 CREAM = IM_COL32(240, 234, 226, 255);
constexpr ImU32 MUTED = IM_COL32(160, 149, 138, 255);
constexpr ImU32 LIME = IM_COL32(155, 208, 107, 255);
constexpr ImU32 INK = IM_COL32(18, 19, 21, 255);
constexpr ImU32 BORDER = IM_COL32(92, 81, 68, 255);
constexpr ImU32 RULE = IM_COL32(58, 50, 41, 255);

// Type (points) and the card grid (design pixels).
constexpr float BODY_SIZE = 16;
constexpr float SMALL_SIZE = 14;
constexpr float TITLE_TRACKING = 1.5F;
constexpr float SUBHEAD_TRACKING = 1;
/** Card contents sit this far in from its left and right edges. */
constexpr float INSET = 16;
constexpr float CARD_TITLE_TOP = 12;
/** Fade at each card edge into the page (§9.1). */
constexpr float EDGE_FADE = 6;

// Buttons: height, and the focus ring this far outside.
constexpr float BUTTON_H = 28;
constexpr float FOCUS_RING = 2;

// Engine version rows: the first starts this far down the card; label,
// helper and buttons within each row; the rule between rows above it.
constexpr float FIRST_ROW_TOP = 48;
constexpr float ROW_H = 68;
constexpr float ROW_LABEL_TOP = 1;
constexpr float ROW_HELPER_TOP = 36;
constexpr float ROW_BUTTON_TOP = 6;
constexpr float ROW_RULE_ABOVE = 10;
/** Two versions: 112-wide buttons; three: narrower, in the same strip
 * widened a little into the helper text's room. */
constexpr float TWO_BUTTON_W = 112;
constexpr float THREE_BUTTON_W = 96;

// Match options: a checkbox at the right, ticked with two strokes; the
// whole row is the hit area.
constexpr float CHECK_FROM_RIGHT = 38;
constexpr float CHECK_TOP = 1;
constexpr float CHECK_SIZE = 22;
constexpr float TICK_THICKNESS = 2;
constexpr std::array<ImVec2, 3> TICK{{{5, 11}, {10, 16}, {18, 7}}};
constexpr float OPTION_H = 54;
constexpr float OPTION_LABEL_TOP = 2;
constexpr float OPTION_HELPER_TOP = 28;
/** The helper wraps short of the checkbox. */
constexpr float OPTION_HELPER_MARGIN = 60;

// The header card's title and introduction.
constexpr float PAGE_TITLE_TOP = 18;
constexpr float PAGE_TITLE_SIZE = 42;
constexpr float PAGE_TITLE_TRACKING = 0.5F;
constexpr float INTRO_TOP = 82;
constexpr float INTRO_SIZE = 17;

/** The simulation card, down from its top (or in from its right): the "new
 * runs" block with the home advantage value and its slider (track, thumb
 * and scale labels), then "match options". */
struct SimulationLayout
{
  float newRuns;
  float homeLabel;
  float valueTop;
  float valueFromRight;
  float valueW;
  float sliderTop;
  float trackInset;
  float trackTop;
  float trackBottom;
  float thumbHalfW;
  float thumbTop;
  float thumbBottom;
  float scaleLabels;
  float standardHalfW;
  float maximumFromRight;
  float applies;
  float rule;
  float matchOptions;
  float familiarity;
  float debug;
};
constexpr SimulationLayout SIM{.newRuns = 48,
                               .homeLabel = 78,
                               .valueTop = 70,
                               .valueFromRight = 100,
                               .valueW = 84,
                               .sliderTop = 112,
                               .trackInset = 22,
                               .trackTop = 123,
                               .trackBottom = 127,
                               .thumbHalfW = 5,
                               .thumbTop = 118,
                               .thumbBottom = 132,
                               .scaleLabels = 146,
                               .standardHalfW = 36,
                               .maximumFromRight = 96,
                               .applies = 176,
                               .rule = 206,
                               .matchOptions = 222,
                               .familiarity = 248,
                               .debug = 322};
constexpr ImVec2 VALUE_PADDING{8, 6};
constexpr float NOTE_TITLE_TOP = 12;
constexpr float NOTE_TEXT_TOP = 38;

struct Card
{
  float x, y, w, h;
};

struct SetupPainter
{
  ImVec2 origin;
  float scale = 1.0F;
  ImFont* regular = nullptr;
  ImFont* medium = nullptr;
  ImFont* bold = nullptr;
  ImDrawList* draw = ImGui::GetWindowDrawList();

  [[nodiscard]] ImVec2 point(float x, float y) const
  {
    return {origin.x + (x * scale), origin.y + (y * scale)};
  }

  // Argument order: position then styling, ImGui text order.
  // NOLINTBEGIN(bugprone-easily-swappable-parameters)
  void text(float x, float y, const char* value, float size = SMALL_SIZE,
            ImU32 ink = CREAM, ImFont* font = nullptr, float tracking = 0,
            float wrap = 0) const
  // NOLINTEND(bugprone-easily-swappable-parameters)
  {
    font = (font != nullptr) ? font : regular;
    ImGui::PushFont(font, size);
    ImVec2 pos = point(x, y);
    if (tracking == 0)
    {
      draw->AddText(font, ImGui::GetFontSize(), pos, ink, value, nullptr,
                    wrap * scale);
    }
    else
    {
      for (const char character : std::string_view(value))
      {
        const std::array glyph{character, '\0'};
        draw->AddText(pos, ink, glyph.data());
        pos.x += ImGui::CalcTextSize(glyph.data()).x + (tracking * scale);
      }
    }
    ImGui::PopFont();
  }

  void line(float x, float y, float w) const
  {
    draw->AddLine(point(x, y), point(x + w, y), RULE, scale);
  }

  void begin(const Card& c, ImU32 fill, const char* title = nullptr) const
  {
    draw->AddRectFilled(point(c.x, c.y), point(c.x + c.w, c.y + c.h), fill);
    draw->PushClipRect(point(c.x, c.y), point(c.x + c.w, c.y + c.h), true);
    if (title != nullptr)
    {
      text(c.x + INSET, c.y + CARD_TITLE_TOP, title, BODY_SIZE, CREAM, bold,
           TITLE_TRACKING);
    }
  }

  void end(const Card& c, bool band = false) const
  {
    // Fade all four edges into the page, after the card contents (§9.1).
    const ImU32 clear = IM_COL32(22, 19, 16, 0);
    const auto a = point(c.x, c.y);
    const auto b = point(c.x + c.w, c.y + c.h);
    const float f = EDGE_FADE * scale;
    draw->AddRectFilledMultiColor(a, ImVec2(b.x, a.y + f), PAGE, PAGE, clear,
                                  clear);
    draw->AddRectFilledMultiColor(ImVec2(a.x, b.y - f), b, clear, clear, PAGE,
                                  PAGE);
    draw->AddRectFilledMultiColor(a, ImVec2(a.x + f, b.y), PAGE, clear, clear,
                                  PAGE);
    draw->AddRectFilledMultiColor(ImVec2(b.x - f, a.y), b, clear, PAGE, PAGE,
                                  clear);
    if (band)
    {
      draw->AddRectFilled(a, ImVec2(b.x, a.y + f), LIME);
    }
    draw->PopClipRect();
  }

  void subhead(float x, float y, const char* label) const
  {
    text(x, y, label, SMALL_SIZE, CREAM, bold, SUBHEAD_TRACKING);
  }

  bool button(float x, float y, float w, const char* label, bool selected) const
  {
    ImGui::SetCursorScreenPos(point(x, y));
    const bool pressed = ImGui::InvisibleButton(
        label, ImVec2(w * scale, BUTTON_H * scale), ImGuiButtonFlags_EnableNav);
    const bool hover = ImGui::IsItemHovered();
    const ImU32 hoverFill =
        selected ? IM_COL32(170, 215, 130, 255) : IM_COL32(46, 39, 32, 255);
    const ImU32 restFill = selected ? LIME : DARK;
    const ImU32 fill = hover ? hoverFill : restFill;
    draw->AddRectFilled(point(x, y), point(x + w, y + BUTTON_H), fill);
    draw->AddRect(point(x, y), point(x + w, y + BUTTON_H),
                  selected ? LIME : BORDER, 0, 0, scale);
    if (ImGui::IsItemFocused())
    {
      draw->AddRect(point(x - FOCUS_RING, y - FOCUS_RING),
                    point(x + w + FOCUS_RING, y + BUTTON_H + FOCUS_RING), CREAM,
                    0, 0, scale);
    }
    SandboxTypography::label(draw, bold, SMALL_SIZE, label, point(x, y),
                             point(x + w, y + BUTTON_H), selected ? INK : CREAM,
                             scale);
    return pressed;
  }

  template <typename Version>
  // Argument order: one value per brain, in brain order.
  // NOLINTBEGIN(bugprone-easily-swappable-parameters)
  void version(
      const Card& c, int row, const char* label, const char* helper,
      Version& value, Version aardvark, Version binturong,
      std::type_identity_t<std::optional<Version>> capybara = std::nullopt,
      float rowHeight = ROW_H) const
  // NOLINTEND(bugprone-easily-swappable-parameters)
  {
    const float y = c.y + FIRST_ROW_TOP + (static_cast<float>(row) * rowHeight);
    const float buttonW = capybara ? THREE_BUTTON_W : TWO_BUTTON_W;
    const float stride = buttonW + GAP;
    const float controlsX =
        c.x + c.w - INSET - (stride * (capybara ? 3.0F : 2.0F)) + GAP;
    ImGui::PushID(label);
    text(c.x + INSET, y + ROW_LABEL_TOP, label, BODY_SIZE, CREAM, medium);
    text(c.x + INSET, y + ROW_HELPER_TOP, helper, BODY_SIZE, MUTED, regular);
    const float buttonY = y + ROW_BUTTON_TOP;
    if (button(controlsX, buttonY, buttonW, "AARDVARK", value == aardvark))
    {
      value = aardvark;
    }
    if (button(controlsX + stride, buttonY, buttonW, "BINTURONG",
               value == binturong))
    {
      value = binturong;
    }
    if (capybara && button(controlsX + (2 * stride), buttonY, buttonW,
                           "CAPYBARA", value == *capybara))
    {
      value = *capybara;
    }
    if (row > 0)
    {
      line(c.x + INSET, y - ROW_RULE_ABOVE, c.w - (2 * INSET));
    }
    ImGui::PopID();
  }

  void matchOption(const Card& c, float y, const char* label,
                   const char* helper, bool& value) const
  {
    const float x = c.x + c.w - CHECK_FROM_RIGHT;
    ImGui::SetCursorScreenPos(point(c.x + INSET, y));
    ImGui::PushID(label);
    if (ImGui::InvisibleButton(
            "##option", ImVec2((c.w - (2 * INSET)) * scale, OPTION_H * scale),
            ImGuiButtonFlags_EnableNav))
    {
      value = !value;
    }
    const bool hover = ImGui::IsItemHovered();
    if (hover)
    {
      UI::tooltip("{}", helper);
    }
    const ImVec2 boxMin = point(x, y + CHECK_TOP);
    const ImVec2 boxMax = point(x + CHECK_SIZE, y + CHECK_TOP + CHECK_SIZE);
    draw->AddRectFilled(boxMin, boxMax, value ? LIME : DARK);
    draw->AddRect(boxMin, boxMax,
                  (hover || ImGui::IsItemFocused()) ? CREAM : BORDER, 0, 0,
                  scale);
    if (value)
    {
      for (std::size_t stroke = 0; stroke + 1 < TICK.size(); ++stroke)
      {
        const ImVec2 from = TICK.at(stroke);
        const ImVec2 to = TICK.at(stroke + 1);
        draw->AddLine(point(x + from.x, y + from.y), point(x + to.x, y + to.y),
                      INK, TICK_THICKNESS * scale);
      }
    }
    text(c.x + INSET, y + OPTION_LABEL_TOP, label, BODY_SIZE, CREAM, medium);
    text(c.x + INSET, y + OPTION_HELPER_TOP, helper, BODY_SIZE, MUTED, regular,
         0, c.w - OPTION_HELPER_MARGIN);
    ImGui::PopID();
  }
};
}  // namespace

void SandboxSetupUI::render(float scale, ImFont* setup_regular_font,
                            ImFont* setup_medium_font, ImFont* sidebar_font,
                            bool& full_familiarity, bool& debug_at_kick_off)
{
  const float available = ImGui::GetContentRegionAvail().x / scale;
  const bool wide = available >= HOME_W + AWAY_W + SIDE_W + 2 * GAP;
  const bool twoColumns = available >= HOME_W + SIDE_W + GAP;
  float width =
      twoColumns ? HOME_W + SIDE_W + GAP : std::min(available, HOME_W);
  float leftW = twoColumns ? HOME_W : width;
  if (wide)
  {
    width = HOME_W + AWAY_W + SIDE_W + (2 * GAP);
    leftW = HOME_W + GAP + AWAY_W;
  }
  SetupPainter ui{.origin = ImGui::GetCursorScreenPos(),
                  .scale = scale,
                  .regular = setup_regular_font,
                  .medium = setup_medium_font,
                  .bold = sidebar_font};
  const Card header{.x = 0, .y = 0, .w = width, .h = HEADER_H};
  ui.begin(header, LIGHT);
  ui.text(INSET, PAGE_TITLE_TOP, "Set-up", PAGE_TITLE_SIZE, CREAM, sidebar_font,
          PAGE_TITLE_TRACKING);
  ui.text(INSET, INTRO_TOP,
          "Choose the engines and conditions for your next run", INTRO_SIZE,
          CREAM, setup_regular_font, 0, width - (2 * INSET));
  ui.end(header, true);

  const Card home{
      .x = 0, .y = HEADER_H + GAP, .w = wide ? HOME_W : leftW, .h = ENGINE_H};
  const Card away{.x = wide ? HOME_W + GAP : 0,
                  .y = wide ? home.y : home.y + ENGINE_H + GAP,
                  .w = wide ? AWAY_W : leftW,
                  .h = ENGINE_H};
  for (int side = 0; side < 2; ++side)
  {
    const Card& c = side == 0 ? home : away;
    ImGui::PushID(side);
    ui.begin(c, side == 0 ? LIGHT : MID,
             side == 0 ? "HOME ENGINES" : "AWAY ENGINES");
    ui.version(c, 0, "Brain", "Player decisions", sandboxBrains().at(side),
               BrainId::AARDVARK, BrainId::BINTURONG, BrainId::CAPYBARA);
    ui.version(c, 1, "Body", "Movement and ball contact",
               sandboxBodies().at(side), BodyId::AARDVARK, BodyId::BINTURONG);
    ui.version(c, 2, "Manager", "Touchline decisions",
               sandboxManagers().at(side), ManagerId::AARDVARK,
               ManagerId::BINTURONG);
    ui.end(c);
    ImGui::PopID();
  }
  // Use the same row rhythm and body-text size for all engine settings.
  const Card shared{
      .x = 0, .y = away.y + ENGINE_H + GAP, .w = leftW, .h = SHARED_H};
  ui.begin(shared, LIGHT, "SHARED SYSTEMS");
  const auto sharedRow = [&]<typename V>(int row, const char* label,
                                         const char* helper, V& current, V a,
                                         V b)
  { ui.version(shared, row, label, helper, current, a, b); };
  sharedRow(0, "Handshake", "Attributes into match inputs", sandboxHandshake(),
            HandshakeId::AARDVARK, HandshakeId::BINTURONG);
  sharedRow(1, "Referee", "Fouls, cards and advantage", sandboxReferee(),
            RefereeId::AARDVARK, RefereeId::BINTURONG);
  sharedRow(2, "Ball physics", "Flight, bounce and roll", sandboxBallPhysics(),
            BallPhysicsId::AARDVARK, BallPhysicsId::BINTURONG);
  sharedRow(3, "Out of play", "Goals, boundaries and restarts",
            sandboxOutOfPlay(), OutOfPlayId::AARDVARK, OutOfPlayId::BINTURONG);
  sharedRow(4, "Possession", "Control and ownership of the ball",
            sandboxPossession(), PossessionId::AARDVARK,
            PossessionId::BINTURONG);
  ui.end(shared);

  const Card simulation{
      .x = (wide || twoColumns) ? leftW + GAP : 0,
      .y = (wide || twoColumns) ? home.y : shared.y + shared.h + GAP,
      .w = (wide || twoColumns) ? width - leftW - GAP : width,
      .h = SIMULATION_H};
  ui.begin(simulation, DARK, "SIMULATION");
  const float sx = simulation.x;
  const float sy = simulation.y;
  ui.subhead(sx + INSET, sy + SIM.newRuns, "NEW RUNS");
  ui.text(sx + INSET, sy + SIM.homeLabel, "Home advantage", BODY_SIZE, CREAM,
          setup_medium_font);
  // Native ImGui input preserves keyboard entry, dragging and focus semantics.
  ImGui::PushFont(setup_medium_font, SMALL_SIZE);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, scale);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                      ImVec2(VALUE_PADDING.x * scale, VALUE_PADDING.y * scale));
  const std::array<std::pair<ImGuiCol, ImU32>, 7> valueColours{
      {{ImGuiCol_Text, CREAM},
       {ImGuiCol_FrameBg, DARK},
       {ImGuiCol_FrameBgHovered, MID},
       {ImGuiCol_FrameBgActive, MID},
       {ImGuiCol_Border, BORDER},
       {ImGuiCol_SliderGrab, LIME},
       {ImGuiCol_SliderGrabActive, LIME}}};
  for (const auto& [slot, colour] : valueColours)
  {
    ImGui::PushStyleColor(slot, colour);
  }
  ImGui::SetCursorScreenPos(
      ui.point(sx + simulation.w - SIM.valueFromRight, sy + SIM.valueTop));
  ImGui::SetNextItemWidth(SIM.valueW * scale);
  ImGui::InputFloat("##home_advantage_value", &sandboxHomeAdvantage(), 0, 0,
                    "%.2f");
  const float maximum = MatchTuning::Context::MAX_HOME_ADVANTAGE_SCALE;
  sandboxHomeAdvantage() = std::clamp(sandboxHomeAdvantage(), 0.0F, maximum);
  ImGui::SetCursorScreenPos(ui.point(sx + INSET, sy + SIM.sliderTop));
  ImGui::SetNextItemWidth((simulation.w - (2 * INSET)) * scale);
  // ImGui's own slider is invisible; its track and thumb are drawn below.
  constexpr std::array<ImGuiCol, 6> HIDDEN{
      ImGuiCol_FrameBg,    ImGuiCol_FrameBgHovered,   ImGuiCol_FrameBgActive,
      ImGuiCol_SliderGrab, ImGuiCol_SliderGrabActive, ImGuiCol_Border};
  for (const auto slot : HIDDEN)
  {
    ImGui::PushStyleColor(slot, IM_COL32(0, 0, 0, 0));
  }
  ImGui::SliderFloat("##home_advantage_slider", &sandboxHomeAdvantage(), 0,
                     maximum, "", ImGuiSliderFlags_AlwaysClamp);
  if (ImGui::IsItemHovered())
  {
    UI::tooltip("0: neutral. 1: standard home edge. 2: maximum.");
  }
  ImGui::PopStyleColor(static_cast<int>(HIDDEN.size()));
  const float trackLeft = sx + SIM.trackInset;
  const float trackWidth = simulation.w - (2 * SIM.trackInset);
  const float thumb = trackWidth * sandboxHomeAdvantage() / maximum;
  ui.draw->AddRectFilled(ui.point(trackLeft, sy + SIM.trackTop),
                         ui.point(trackLeft + trackWidth, sy + SIM.trackBottom),
                         RULE);
  ui.draw->AddRectFilled(ui.point(trackLeft, sy + SIM.trackTop),
                         ui.point(trackLeft + thumb, sy + SIM.trackBottom),
                         LIME);
  const float thumbX = trackLeft + thumb;
  ui.draw->AddRectFilled(
      ui.point(thumbX - SIM.thumbHalfW, sy + SIM.thumbTop),
      ui.point(thumbX + SIM.thumbHalfW, sy + SIM.thumbBottom), LIME);
  if (ImGui::IsItemFocused())
  {
    ui.draw->AddRect(ui.point(thumbX - SIM.thumbHalfW - FOCUS_RING,
                              sy + SIM.thumbTop - FOCUS_RING),
                     ui.point(thumbX + SIM.thumbHalfW + FOCUS_RING,
                              sy + SIM.thumbBottom + FOCUS_RING),
                     CREAM, 0, 0, scale);
  }
  ImGui::PopStyleColor(static_cast<int>(valueColours.size()));
  ImGui::PopStyleVar(3);
  ImGui::PopFont();
  ui.text(sx + INSET, sy + SIM.scaleLabels, "0 Neutral", SMALL_SIZE, MUTED);
  ui.text(sx + (simulation.w / 2) - SIM.standardHalfW, sy + SIM.scaleLabels,
          "1 Standard", SMALL_SIZE, MUTED);
  ui.text(sx + simulation.w - SIM.maximumFromRight, sy + SIM.scaleLabels,
          "2 Maximum", SMALL_SIZE, MUTED);
  ui.text(sx + INSET, sy + SIM.applies,
          "Applies to new matches, drills and training", BODY_SIZE, MUTED);
  ui.line(sx + INSET, sy + SIM.rule, simulation.w - (2 * INSET));
  ui.subhead(sx + INSET, sy + SIM.matchOptions, "MATCH OPTIONS");
  ui.matchOption(simulation, sy + SIM.familiarity, "Full tactical familiarity",
                 "Start both sides fully familiar with their tactics",
                 full_familiarity);
  ui.matchOption(simulation, sy + SIM.debug, "Debug at kick-off",
                 "Open review and debugger when play begins",
                 debug_at_kick_off);
  ui.end(simulation);
  const Card note{.x = simulation.x,
                  .y = simulation.y + simulation.h + GAP,
                  .w = simulation.w,
                  .h = NOTE_H};
  ui.begin(note, MID);
  ui.text(note.x + INSET, note.y + NOTE_TITLE_TOP, "NEXT RUN", SMALL_SIZE, LIME,
          sidebar_font, SUBHEAD_TRACKING);
  ui.text(note.x + INSET, note.y + NOTE_TEXT_TOP,
          "Changes apply to new runs. Existing runs keep their settings.",
          BODY_SIZE, CREAM, setup_regular_font, 0, note.w - (2 * INSET));
  ui.end(note);
  // One page scrollbar in compact mode; no card owns a scrolling child.
  ImGui::SetCursorScreenPos(ui.origin);
  ImGui::Dummy(ImVec2(width * scale,
                      std::max(shared.y + shared.h, note.y + note.h) * scale));
}
