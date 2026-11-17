// Football Management Project — see LICENSE in the project root.
#include "tools/match_sandbox_setup_ui.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <string_view>

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
    return ImVec2(origin.x + (x * scale), origin.y + (y * scale));
  }

  void text(float x, float y, const char* value, float size = 14,
            ImU32 ink = CREAM, ImFont* font = nullptr, float tracking = 0,
            float wrap = 0) const
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
      text(c.x + 16, c.y + 12, title, 16, CREAM, bold, 1.5F);
    }
  }

  void end(const Card& c, bool band = false) const
  {
    // Fade all four edges into the page, after the card contents (§9.1).
    const ImU32 clear = IM_COL32(22, 19, 16, 0);
    const auto a = point(c.x, c.y), b = point(c.x + c.w, c.y + c.h);
    const float f = 6 * scale;
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
    text(x, y, label, 14, CREAM, bold, 1);
  }

  bool button(float x, float y, float w, const char* label, bool selected) const
  {
    ImGui::SetCursorScreenPos(point(x, y));
    const bool pressed = ImGui::InvisibleButton(
        label, ImVec2(w * scale, 28 * scale), ImGuiButtonFlags_EnableNav);
    const bool hover = ImGui::IsItemHovered();
    const ImU32 fill = selected ? (hover ? IM_COL32(170, 215, 130, 255) : LIME)
                                : (hover ? IM_COL32(46, 39, 32, 255) : DARK);
    draw->AddRectFilled(point(x, y), point(x + w, y + 28), fill);
    draw->AddRect(point(x, y), point(x + w, y + 28), selected ? LIME : BORDER,
                  0, 0, scale);
    if (ImGui::IsItemFocused())
    {
      draw->AddRect(point(x - 2, y - 2), point(x + w + 2, y + 30), CREAM, 0, 0,
                    scale);
    }
    SandboxTypography::label(draw, bold, 14, label, point(x, y),
                             point(x + w, y + 28), selected ? INK : CREAM,
                             scale);
    return pressed;
  }

  template <typename Version>
  void version(const Card& c, int row, const char* label, const char* helper,
               Version& value, Version aardvark, Version binturong,
               float rowHeight = 68) const
  {
    const float y = c.y + 48 + (static_cast<float>(row) * rowHeight);
    const float controlsX = c.x + c.w - 16 - 230;
    ImGui::PushID(label);
    text(c.x + 16, y + 1, label, 16, CREAM, medium);
    text(c.x + 16, y + 36, helper, 16, MUTED, regular);
    if (button(controlsX, y + 6, 112, "AARDVARK", value == aardvark))
    {
      value = aardvark;
    }
    if (button(controlsX + 118, y + 6, 112, "BINTURONG", value == binturong))
    {
      value = binturong;
    }
    if (row > 0)
    {
      line(c.x + 16, y - 10, c.w - 32);
    }
    ImGui::PopID();
  }

  void matchOption(const Card& c, float y, const char* label,
                   const char* helper, bool& value) const
  {
    const float x = c.x + c.w - 38;
    ImGui::SetCursorScreenPos(point(c.x + 16, y));
    ImGui::PushID(label);
    if (ImGui::InvisibleButton("##option",
                               ImVec2((c.w - 32) * scale, 54 * scale),
                               ImGuiButtonFlags_EnableNav))
    {
      value = !value;
    }
    const bool hover = ImGui::IsItemHovered();
    if (hover)
    {
      ImGui::SetTooltip("%s", helper);
    }
    draw->AddRectFilled(point(x, y + 1), point(x + 22, y + 23),
                        value ? LIME : DARK);
    draw->AddRect(point(x, y + 1), point(x + 22, y + 23),
                  (hover || ImGui::IsItemFocused()) ? CREAM : BORDER, 0, 0,
                  scale);
    if (value)
    {
      draw->AddLine(point(x + 5, y + 11), point(x + 10, y + 16), INK,
                    2 * scale);
      draw->AddLine(point(x + 10, y + 16), point(x + 18, y + 7), INK,
                    2 * scale);
    }
    text(c.x + 16, y + 2, label, 16, CREAM, medium);
    text(c.x + 16, y + 28, helper, 16, MUTED, regular, 0, c.w - 60);
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
  const float width =
      wide ? HOME_W + AWAY_W + SIDE_W + (2 * GAP)
           : (twoColumns ? HOME_W + SIDE_W + GAP : std::min(available, HOME_W));
  SetupPainter ui{.origin = ImGui::GetCursorScreenPos(),
                  .scale = scale,
                  .regular = setup_regular_font,
                  .medium = setup_medium_font,
                  .bold = sidebar_font};
  const Card header{.x = 0, .y = 0, .w = width, .h = HEADER_H};
  ui.begin(header, LIGHT);
  ui.text(16, 18, "Set-up", 42, CREAM, sidebar_font, 0.5F);
  ui.text(16, 82, "Choose the engines and conditions for your next run", 17,
          CREAM, setup_regular_font, 0, width - 32);
  ui.end(header, true);

  const float leftW =
      wide ? HOME_W + GAP + AWAY_W : (twoColumns ? HOME_W : width);
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
               BrainId::AARDVARK, BrainId::BINTURONG);
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
      .h = 400};
  ui.begin(simulation, DARK, "SIMULATION");
  const float sx = simulation.x, sy = simulation.y;
  ui.subhead(sx + 16, sy + 48, "NEW RUNS");
  ui.text(sx + 16, sy + 78, "Home advantage", 16, CREAM, setup_medium_font);
  // Native ImGui input preserves keyboard entry, dragging and focus semantics.
  ImGui::PushFont(setup_medium_font, 14);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, scale);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8 * scale, 6 * scale));
  ImGui::PushStyleColor(ImGuiCol_Text, CREAM);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, DARK);
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, MID);
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, MID);
  ImGui::PushStyleColor(ImGuiCol_Border, BORDER);
  ImGui::PushStyleColor(ImGuiCol_SliderGrab, LIME);
  ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, LIME);
  ImGui::SetCursorScreenPos(ui.point(sx + simulation.w - 100, sy + 70));
  ImGui::SetNextItemWidth(84 * scale);
  ImGui::InputFloat("##home_advantage_value", &sandboxHomeAdvantage(), 0, 0,
                    "%.2f");
  const float maximum = MatchTuning::Context::MAX_HOME_ADVANTAGE_SCALE;
  sandboxHomeAdvantage() = std::clamp(sandboxHomeAdvantage(), 0.0F, maximum);
  ImGui::SetCursorScreenPos(ui.point(sx + 16, sy + 112));
  ImGui::SetNextItemWidth((simulation.w - 32) * scale);
  for (const auto color :
       {ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive,
        ImGuiCol_SliderGrab, ImGuiCol_SliderGrabActive, ImGuiCol_Border})
  {
    ImGui::PushStyleColor(color, IM_COL32(0, 0, 0, 0));
  }
  ImGui::SliderFloat("##home_advantage_slider", &sandboxHomeAdvantage(), 0,
                     maximum, "", ImGuiSliderFlags_AlwaysClamp);
  if (ImGui::IsItemHovered())
  {
    ImGui::SetTooltip("0: neutral. 1: standard home edge. 2: maximum.");
  }
  ImGui::PopStyleColor(6);
  const float trackWidth = simulation.w - 44;
  const float thumb = trackWidth * sandboxHomeAdvantage() / maximum;
  ui.draw->AddRectFilled(ui.point(sx + 22, sy + 123),
                         ui.point(sx + 22 + trackWidth, sy + 127), RULE);
  ui.draw->AddRectFilled(ui.point(sx + 22, sy + 123),
                         ui.point(sx + 22 + thumb, sy + 127), LIME);
  ui.draw->AddRectFilled(ui.point(sx + 17 + thumb, sy + 118),
                         ui.point(sx + 27 + thumb, sy + 132), LIME);
  if (ImGui::IsItemFocused())
  {
    ui.draw->AddRect(ui.point(sx + 15 + thumb, sy + 116),
                     ui.point(sx + 29 + thumb, sy + 134), CREAM, 0, 0, scale);
  }
  ImGui::PopStyleColor(7);
  ImGui::PopStyleVar(3);
  ImGui::PopFont();
  ui.text(sx + 16, sy + 146, "0 Neutral", 14, MUTED);
  ui.text(sx + (simulation.w / 2) - 36, sy + 146, "1 Standard", 14, MUTED);
  ui.text(sx + simulation.w - 96, sy + 146, "2 Maximum", 14, MUTED);
  ui.text(sx + 16, sy + 176, "Applies to new matches, drills and training", 16,
          MUTED);
  ui.line(sx + 16, sy + 206, simulation.w - 32);
  ui.subhead(sx + 16, sy + 222, "MATCH OPTIONS");
  ui.matchOption(simulation, sy + 248, "Full tactical familiarity",
                 "Start both sides fully familiar with their tactics",
                 full_familiarity);
  ui.matchOption(simulation, sy + 322, "Debug at kick-off",
                 "Open review and debugger when play begins",
                 debug_at_kick_off);
  ui.end(simulation);
  const Card note{.x = simulation.x,
                  .y = simulation.y + simulation.h + GAP,
                  .w = simulation.w,
                  .h = 112};
  ui.begin(note, MID);
  ui.text(note.x + 16, note.y + 12, "NEXT RUN", 14, LIME, sidebar_font, 1);
  ui.text(note.x + 16, note.y + 38,
          "Changes apply to new runs. Existing runs keep their settings.", 16,
          CREAM, setup_regular_font, 0, note.w - 32);
  ui.end(note);
  // One page scrollbar in compact mode; no card owns a scrolling child.
  ImGui::SetCursorScreenPos(ui.origin);
  ImGui::Dummy(ImVec2(width * scale,
                      std::max(shared.y + shared.h, note.y + note.h) * scale));
}
