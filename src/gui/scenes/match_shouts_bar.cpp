// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "match_shouts_bar.h"

#include <SDL3/SDL.h>
#include <fmt/printf.h>

#include <algorithm>
#include <string>

#include "global/language_manager.h"
#include "gui/input_actions.h"
#include "gui/view_models/match_changes.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/match_engine.h"

namespace MatchShoutsBar
{
namespace
{
constexpr float BUTTON_GAP = 4.0f;
/** Extra space below the last row so the panel pads the bottom like the top. */
constexpr float BOTTOM_PAD = 6.0f;

float gap() { return BUTTON_GAP * Theme::scale(); }

float buttonWidth(const char* label)
{
  // Must match the width the button widget actually renders at (its label
  // padding); otherwise the label is wider than the padded area and ImGui
  // misaligns it inside the button.
  return UI::buttonWidth(label, UI::ButtonSize::COMPACT);
}

/** Width of the leading label and of each button, laid out in rows. */
int rowsFor(float width)
{
  const float spacing = gap();
  float x = ImGui::CalcTextSize(LOC("MATCH_SHOUTS")).x + spacing;
  int rows = 1;
  for (const MatchChanges::ShoutInfo& info : MatchChanges::SHOUTS)
  {
    const float button = buttonWidth(LOC(info.labelKey));
    if (x > 0.0f && x + button > width)
    {
      ++rows;
      x = 0.0f;
    }
    x += button + spacing;
  }
  return rows;
}
}  // namespace

int indexForScancode(int scancode)
{
  static_assert(MatchChanges::SHOUTS.size() == 11);
  if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_0)
    return scancode - SDL_SCANCODE_1;
  if (scancode == SDL_SCANCODE_MINUS) return 10;
  return -1;
}

std::string keyName(std::size_t index)
{
  if (index >= MatchChanges::SHOUTS.size()) return {};
  // The binding in force (Settings > Controls can change it).
  const Input::ActionRegistry& registry = Input::registry();
  const auto action = registry.find(Input::Ids::shout(index));
  return action ? registry.label(*action) : std::string();
}

bool available(const MatchEngine& engine)
{
  const MatchState state = engine.getState();
  return state != MatchState::FULL_TIME &&
         state != MatchState::PENALTY_SHOOTOUT;
}

float height(float width)
{
  const auto rows = static_cast<float>(rowsFor(width));
  // Rows wrap like any ImGui line: the style's vertical item spacing. The
  // trailing pad gives the bottom the same breathing room as the top.
  return rows * UI::buttonHeight(UI::ButtonSize::COMPACT) +
         (rows - 1.0f) * ImGui::GetStyle().ItemSpacing.y +
         BOTTOM_PAD * Theme::scale();
}

bool shout(const TouchlineContext& context, std::size_t index)
{
  if (index >= MatchChanges::SHOUTS.size() || !available(context.engine))
    return false;
  const MatchChanges::ShoutInfo& info = MatchChanges::SHOUTS[index];
  context.engine.applyShout(context.home, info.shout);
  context.status = fmt::sprintf(LOC("MATCH_SHOUT_GIVEN"), LOC(info.labelKey));
  context.status_refused = false;
  return true;
}

void render(const TouchlineContext& context, float width)
{
  const Theme::Palette& palette = Theme::palette();
  const bool enabled = available(context.engine);
  const auto active = context.engine.getActiveShout(context.home);
  const float strength = context.engine.getShoutStrength(context.home);
  const float spacing = gap();

  ImGui::PushID("match_shouts");
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(palette.muted, "%s", LOC("MATCH_SHOUTS"));
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", LOC("MATCH_SHOUTS_HINT"));
  ImGui::BeginDisabled(!enabled);
  for (std::size_t index = 0; index < MatchChanges::SHOUTS.size(); ++index)
  {
    const MatchChanges::ShoutInfo& info = MatchChanges::SHOUTS[index];
    const char* label = LOC(info.labelKey);
    const float button = buttonWidth(label);
    ImGui::SameLine(0.0f, spacing);
    // Wrap onto a new line when the button does not fit the space left on the
    // current one. This measures the real remaining width, so the bar can
    // never be laid out past the panel (which clipped the last buttons).
    if (std::min(ImGui::GetContentRegionAvail().x, width) < button)
      ImGui::NewLine();
    ImGui::PushID(static_cast<int>(index));
    const bool inForce = active && *active == info.shout;
    if (UI::toggleButton(label, inForce, ImVec2(button, 0.0f),
                         UI::ButtonSize::COMPACT))
      shout(context, index);
    if (inForce)
    {
      // What is left of the shout's effect fills the button.
      const ImVec2 min = ImGui::GetItemRectMin();
      const ImVec2 max = ImGui::GetItemRectMax();
      ImGui::GetWindowDrawList()->AddRectFilled(
          min,
          ImVec2(min.x + (max.x - min.x) * std::clamp(strength, 0.0f, 1.0f),
                 max.y),
          Theme::toU32(palette.info, 0.22f), ImGui::GetStyle().FrameRounding);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
      ImGui::BeginTooltip();
      ImGui::TextUnformatted(LOC(info.hintKey));
      const std::string key =
          fmt::sprintf(LOC("MATCH_SHOUT_KEY"), keyName(index).c_str());
      ImGui::TextColored(palette.muted, "%s", key.c_str());
      if (inForce)
      {
        const std::string remaining =
            fmt::sprintf(LOC("MATCH_SHOUT_STRENGTH"),
                         static_cast<int>(strength * 100.0f + 0.5f));
        ImGui::TextColored(palette.info, "%s", remaining.c_str());
      }
      ImGui::EndTooltip();
    }
    ImGui::PopID();
  }
  ImGui::EndDisabled();
  ImGui::Dummy(ImVec2(0.0f, BOTTOM_PAD * Theme::scale()));
  ImGui::PopID();
}
}  // namespace MatchShoutsBar
