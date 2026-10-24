// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/widgets/theme.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <utility>

namespace
{
constexpr float BASE_FONT_SIZE = 16.0f;
constexpr float MIN_UI_SCALE = 0.75f;
constexpr float MAX_UI_SCALE = 2.0f;

struct PresetColors
{
  ImVec4 background;
  ImVec4 sidebar;
  ImVec4 surface;
  ImVec4 raised;
  ImVec4 border;
  ImVec4 text;
  ImVec4 muted;
  ImVec4 faint;
  float row_stripe_alpha;
  bool light;
};

constexpr std::array<PresetColors, static_cast<size_t>(std::to_underlying(
                                       Theme::Preset::COUNT))>
    PRESETS = {{
        // Slate (default): a soft graphite with clear elevation steps.
        {ImVec4(0.118f, 0.133f, 0.153f, 1.0f),  // #1E2227
         ImVec4(0.137f, 0.153f, 0.176f, 1.0f),  // #23272D
         ImVec4(0.165f, 0.184f, 0.212f, 1.0f),  // #2A2F36
         ImVec4(0.208f, 0.231f, 0.267f, 1.0f),  // #353B44
         ImVec4(0.275f, 0.302f, 0.345f, 1.0f),  // #464D58
         ImVec4(0.940f, 0.950f, 0.965f, 1.0f),
         ImVec4(0.680f, 0.715f, 0.760f, 1.0f),
         ImVec4(0.505f, 0.545f, 0.595f, 1.0f), 0.030f, false},
        // Midnight blue.
        {ImVec4(0.086f, 0.106f, 0.165f, 1.0f),
         ImVec4(0.102f, 0.125f, 0.192f, 1.0f),
         ImVec4(0.125f, 0.153f, 0.231f, 1.0f),
         ImVec4(0.165f, 0.200f, 0.290f, 1.0f),
         ImVec4(0.235f, 0.275f, 0.380f, 1.0f),
         ImVec4(0.930f, 0.945f, 0.980f, 1.0f),
         ImVec4(0.655f, 0.700f, 0.800f, 1.0f),
         ImVec4(0.475f, 0.520f, 0.625f, 1.0f), 0.030f, false},
        // Pitch green.
        {ImVec4(0.078f, 0.122f, 0.102f, 1.0f),
         ImVec4(0.094f, 0.145f, 0.122f, 1.0f),
         ImVec4(0.114f, 0.173f, 0.145f, 1.0f),
         ImVec4(0.153f, 0.224f, 0.192f, 1.0f),
         ImVec4(0.216f, 0.298f, 0.259f, 1.0f),
         ImVec4(0.930f, 0.965f, 0.945f, 1.0f),
         ImVec4(0.655f, 0.745f, 0.700f, 1.0f),
         ImVec4(0.470f, 0.560f, 0.515f, 1.0f), 0.030f, false},
        // Light.
        {ImVec4(0.925f, 0.933f, 0.945f, 1.0f),
         ImVec4(0.965f, 0.969f, 0.976f, 1.0f),
         ImVec4(1.000f, 1.000f, 1.000f, 1.0f),
         ImVec4(0.906f, 0.914f, 0.929f, 1.0f),
         ImVec4(0.800f, 0.812f, 0.835f, 1.0f),
         ImVec4(0.078f, 0.090f, 0.110f, 1.0f),
         ImVec4(0.330f, 0.360f, 0.405f, 1.0f),
         ImVec4(0.480f, 0.510f, 0.555f, 1.0f), 0.030f, true},
        // High contrast.
        {ImVec4(0.000f, 0.000f, 0.000f, 1.0f),
         ImVec4(0.030f, 0.030f, 0.030f, 1.0f),
         ImVec4(0.050f, 0.050f, 0.050f, 1.0f),
         ImVec4(0.140f, 0.140f, 0.140f, 1.0f),
         ImVec4(0.720f, 0.720f, 0.720f, 1.0f),
         ImVec4(1.000f, 1.000f, 1.000f, 1.0f),
         ImVec4(0.850f, 0.850f, 0.850f, 1.0f),
         ImVec4(0.700f, 0.700f, 0.700f, 1.0f), 0.060f, false},
        // True dark: near-black charcoal for dim rooms and OLED panels.
        {ImVec4(0.045f, 0.055f, 0.066f, 1.0f),
         ImVec4(0.058f, 0.069f, 0.082f, 1.0f),
         ImVec4(0.075f, 0.088f, 0.103f, 1.0f),
         ImVec4(0.112f, 0.128f, 0.147f, 1.0f),
         ImVec4(0.160f, 0.182f, 0.205f, 1.0f),
         ImVec4(0.905f, 0.925f, 0.940f, 1.0f),
         ImVec4(0.600f, 0.650f, 0.690f, 1.0f),
         ImVec4(0.420f, 0.460f, 0.500f, 1.0f), 0.022f, false},
    }};

struct StatusColors
{
  ImVec4 positive;
  ImVec4 warning;
  ImVec4 negative;
  ImVec4 info;
};

// Status colours per colour-vision mode, for the dark presets and for the
// light one. Each keeps at least 4.5:1 against the page background and 3:1
// against cards and raised controls of every preset (see the contrast test).
constexpr std::array<StatusColors, static_cast<size_t>(std::to_underlying(
                                       Theme::ColorVision::COUNT))>
    STATUS_DARK = {{
        {ImVec4(0.235f, 0.770f, 0.486f, 1.0f),
         ImVec4(0.945f, 0.706f, 0.255f, 1.0f),
         ImVec4(0.937f, 0.416f, 0.396f, 1.0f),
         ImVec4(0.400f, 0.660f, 0.965f, 1.0f)},
        {ImVec4(0.380f, 0.650f, 1.000f, 1.0f),
         ImVec4(0.950f, 0.830f, 0.300f, 1.0f),
         ImVec4(0.960f, 0.520f, 0.180f, 1.0f),
         ImVec4(0.760f, 0.640f, 0.980f, 1.0f)},
        {ImVec4(0.200f, 0.780f, 0.740f, 1.0f),
         ImVec4(0.980f, 0.640f, 0.400f, 1.0f),
         ImVec4(0.960f, 0.420f, 0.520f, 1.0f),
         ImVec4(0.640f, 0.660f, 0.980f, 1.0f)},
    }};
constexpr std::array<StatusColors, static_cast<size_t>(std::to_underlying(
                                       Theme::ColorVision::COUNT))>
    STATUS_LIGHT = {{
        {ImVec4(0.090f, 0.470f, 0.250f, 1.0f),
         ImVec4(0.560f, 0.360f, 0.000f, 1.0f),
         ImVec4(0.700f, 0.150f, 0.140f, 1.0f),
         ImVec4(0.120f, 0.360f, 0.720f, 1.0f)},
        {ImVec4(0.100f, 0.350f, 0.750f, 1.0f),
         ImVec4(0.500f, 0.400f, 0.000f, 1.0f),
         ImVec4(0.650f, 0.270f, 0.000f, 1.0f),
         ImVec4(0.420f, 0.250f, 0.650f, 1.0f)},
        {ImVec4(0.000f, 0.430f, 0.400f, 1.0f),
         ImVec4(0.620f, 0.300f, 0.050f, 1.0f),
         ImVec4(0.700f, 0.120f, 0.300f, 1.0f),
         ImVec4(0.330f, 0.300f, 0.700f, 1.0f)},
    }};

// Club accents are chosen for contrast on the dark surfaces, not to imitate
// any real club identity.
constexpr std::array<ImVec4, 10> CLUB_ACCENTS = {
    ImVec4(0.130f, 0.650f, 0.390f, 1.0f), ImVec4(0.235f, 0.560f, 0.925f, 1.0f),
    ImVec4(0.855f, 0.290f, 0.310f, 1.0f), ImVec4(0.925f, 0.620f, 0.180f, 1.0f),
    ImVec4(0.560f, 0.420f, 0.910f, 1.0f), ImVec4(0.110f, 0.660f, 0.690f, 1.0f),
    ImVec4(0.930f, 0.450f, 0.210f, 1.0f), ImVec4(0.330f, 0.420f, 0.900f, 1.0f),
    ImVec4(0.830f, 0.330f, 0.620f, 1.0f), ImVec4(0.520f, 0.700f, 0.200f, 1.0f),
};

struct RatingStop
{
  float value;
  ImVec4 color;
};

using RatingScale = std::array<RatingStop, 5>;

// Weak to strong: red to green, or diverging scales that avoid the
// red-green (orange to blue) and blue-yellow (magenta to teal) confusions.
constexpr std::array<RatingScale, static_cast<size_t>(std::to_underlying(
                                      Theme::ColorVision::COUNT))>
    RATING_SCALES = {{
        {{{35.0f, ImVec4(0.898f, 0.337f, std::numbers::inv_pi_v<float>, 1.0f)},
          {50.0f, ImVec4(0.945f, 0.560f, 0.255f, 1.0f)},
          {60.0f, ImVec4(0.905f, 0.740f, 0.250f, 1.0f)},
          {70.0f, ImVec4(0.520f, 0.780f, 0.330f, 1.0f)},
          {80.0f, ImVec4(0.180f, 0.760f, 0.500f, 1.0f)}}},
        {{{35.0f, ImVec4(0.900f, 0.420f, 0.100f, 1.0f)},
          {50.0f, ImVec4(0.940f, 0.620f, 0.250f, 1.0f)},
          {60.0f, ImVec4(0.800f, 0.780f, 0.600f, 1.0f)},
          {70.0f, ImVec4(0.500f, 0.680f, 0.950f, 1.0f)},
          {80.0f, ImVec4(0.300f, 0.550f, 1.000f, 1.0f)}}},
        {{{35.0f, ImVec4(0.900f, 0.300f, 0.420f, 1.0f)},
          {50.0f, ImVec4(0.900f, 0.520f, 0.620f, 1.0f)},
          {60.0f, ImVec4(0.780f, 0.720f, 0.780f, 1.0f)},
          {70.0f, ImVec4(0.420f, 0.780f, 0.760f, 1.0f)},
          {80.0f, ImVec4(0.150f, 0.740f, 0.700f, 1.0f)}}},
    }};

Theme::Appearance activeAppearance;
std::optional<TeamID> activeClub;
float activeScale = 1.0f;
Theme::Palette activePalette{};

ImVec4 mix(const ImVec4& a, const ImVec4& b, float t)
{
  return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

const PresetColors& presetColors()
{
  const auto index =
      std::min(static_cast<size_t>(std::to_underlying(activeAppearance.preset)),
               PRESETS.size() - 1);
  return PRESETS[index];
}

size_t visionIndex(Theme::ColorVision vision)
{
  return std::min(static_cast<size_t>(std::to_underlying(vision)),
                  STATUS_DARK.size() - 1);
}

void buildPalette()
{
  activePalette = Theme::presetPalette(
      activeAppearance.preset, activeAppearance.color_vision,
      activeAppearance.club_accent && activeClub
          ? Theme::clubAccent(*activeClub)
          : activeAppearance.custom_accent);
}

float channelLuminance(float channel)
{
  return channel <= 0.04045f ? channel / 12.92f
                             : std::pow((channel + 0.055f) / 1.055f, 2.4f);
}

float relativeLuminance(const ImVec4& color)
{
  return 0.2126f * channelLuminance(color.x) +
         0.7152f * channelLuminance(color.y) +
         0.0722f * channelLuminance(color.z);
}

// Lower saturation and brightness of the accent for control fills.
ImVec4 mutedAccent(const ImVec4& accent, bool light)
{
  float hue = 0.0f;
  float saturation = 0.0f;
  float value = 0.0f;
  ImGui::ColorConvertRGBtoHSV(accent.x, accent.y, accent.z, hue, saturation,
                              value);
  saturation *= 0.62f;
  value = light ? std::min(value, 0.62f) : std::clamp(value, 0.45f, 0.72f);
  ImVec4 result(0, 0, 0, 1);
  ImGui::ColorConvertHSVtoRGB(hue, saturation, value, result.x, result.y,
                              result.z);
  return result;
}

void applyColors()
{
  buildPalette();
  const Theme::Palette& p = activePalette;
  const PresetColors& preset = presetColors();
  ImVec4* colors = ImGui::GetStyle().Colors;
  // Controls stay neutral; only handles, checks and fills carry a muted
  // version of the accent, so bright custom accents never flood the screen.
  const ImVec4 control = mutedAccent(p.accent, preset.light);
  const ImVec4 controlHover = mix(control, p.text, 0.18f);
  const ImVec4 neutralHover = mix(p.raised, p.border, 0.65f);
  const ImVec4 neutralActive = mix(p.raised, p.border, 0.95f);
  const ImVec4 selection = mix(p.raised, control, 0.22f);
  const ImVec4 stripe = preset.light ? ImVec4(0, 0, 0, preset.row_stripe_alpha)
                                     : ImVec4(1, 1, 1, preset.row_stripe_alpha);

  colors[ImGuiCol_Text] = p.text;
  colors[ImGuiCol_TextDisabled] = p.muted;
  colors[ImGuiCol_WindowBg] = p.background;
  colors[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
  colors[ImGuiCol_PopupBg] = p.surface;
  colors[ImGuiCol_Border] = p.border;
  colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
  colors[ImGuiCol_FrameBg] = p.raised;
  colors[ImGuiCol_FrameBgHovered] = mix(p.raised, p.border, 0.7f);
  colors[ImGuiCol_FrameBgActive] = neutralActive;
  colors[ImGuiCol_TitleBg] = p.surface;
  colors[ImGuiCol_TitleBgActive] = p.raised;
  colors[ImGuiCol_TitleBgCollapsed] = p.background;
  colors[ImGuiCol_MenuBarBg] = p.surface;
  colors[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
  colors[ImGuiCol_ScrollbarGrab] = p.border;
  colors[ImGuiCol_ScrollbarGrabHovered] = p.faint;
  colors[ImGuiCol_ScrollbarGrabActive] = p.muted;
  colors[ImGuiCol_CheckMark] = control;
  colors[ImGuiCol_SliderGrab] = control;
  colors[ImGuiCol_SliderGrabActive] = controlHover;
  colors[ImGuiCol_Button] = p.raised;
  colors[ImGuiCol_ButtonHovered] = neutralHover;
  colors[ImGuiCol_ButtonActive] = neutralActive;
  colors[ImGuiCol_Header] = selection;
  colors[ImGuiCol_HeaderHovered] = neutralHover;
  colors[ImGuiCol_HeaderActive] = mix(p.raised, control, 0.32f);
  colors[ImGuiCol_Separator] = p.border;
  colors[ImGuiCol_SeparatorHovered] = p.accent;
  colors[ImGuiCol_SeparatorActive] = controlHover;
  colors[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
  colors[ImGuiCol_ResizeGripHovered] = p.accent;
  colors[ImGuiCol_ResizeGripActive] = controlHover;
  colors[ImGuiCol_InputTextCursor] = p.text;
  colors[ImGuiCol_Tab] = p.surface;
  colors[ImGuiCol_TabHovered] = p.raised;
  colors[ImGuiCol_TabSelected] = p.raised;
  colors[ImGuiCol_TabSelectedOverline] = p.accent;
  colors[ImGuiCol_TabDimmed] = p.background;
  colors[ImGuiCol_TabDimmedSelected] = p.surface;
  colors[ImGuiCol_TabDimmedSelectedOverline] = p.border;
  colors[ImGuiCol_PlotLines] = p.accent;
  colors[ImGuiCol_PlotLinesHovered] = controlHover;
  colors[ImGuiCol_PlotHistogram] = p.accent;
  colors[ImGuiCol_PlotHistogramHovered] = controlHover;
  colors[ImGuiCol_TableHeaderBg] = p.surface;
  colors[ImGuiCol_TableBorderStrong] = p.border;
  colors[ImGuiCol_TableBorderLight] = mix(p.surface, p.border, 0.6f);
  colors[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
  colors[ImGuiCol_TableRowBgAlt] = stripe;
  colors[ImGuiCol_TextLink] =
      mix(p.accent, p.text, preset.light ? 0.1f : 0.35f);
  colors[ImGuiCol_TextSelectedBg] = mix(p.surface, p.accent, 0.45f);
  colors[ImGuiCol_DragDropTarget] = controlHover;
  colors[ImGuiCol_NavCursor] = controlHover;
  colors[ImGuiCol_NavWindowingHighlight] = p.text;
  colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0, 0, 0, 0.45f);
  colors[ImGuiCol_ModalWindowDimBg] =
      ImVec4(0.0f, 0.0f, 0.0f, preset.light ? 0.35f : 0.62f);
}
}  // namespace

namespace Theme
{

Palette presetPalette(Preset preset, ColorVision vision, const ImVec4& accent)
{
  const PresetColors& colors = PRESETS[std::min(
      static_cast<size_t>(std::to_underlying(preset)), PRESETS.size() - 1)];
  Palette p{};
  p.background = colors.background;
  p.sidebar = colors.sidebar;
  p.surface = colors.surface;
  p.raised = colors.raised;
  p.border = colors.border;
  p.text = colors.text;
  p.muted = colors.muted;
  p.faint = colors.faint;
  const StatusColors& status =
      (colors.light ? STATUS_LIGHT : STATUS_DARK)[visionIndex(vision)];
  p.positive = status.positive;
  p.warning = status.warning;
  p.negative = status.negative;
  p.info = status.info;
  p.accent = accent;
  const float luminance =
      0.2126f * p.accent.x + 0.7152f * p.accent.y + 0.0722f * p.accent.z;
  p.on_accent = luminance > 0.45f ? ImVec4(0.03f, 0.04f, 0.05f, 1.0f)
                                  : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
  return p;
}

const char* colorVisionKey(ColorVision vision)
{
  using enum ColorVision;
  switch (vision)
  {
    case RED_GREEN:
      return "SETTINGS_COLOR_VISION_RED_GREEN";
    case BLUE_YELLOW:
      return "SETTINGS_COLOR_VISION_BLUE_YELLOW";
    case STANDARD:
    case COUNT:
      break;
  }
  return "SETTINGS_COLOR_VISION_STANDARD";
}

float contrastRatio(const ImVec4& first, const ImVec4& second)
{
  const float a = relativeLuminance(first);
  const float b = relativeLuminance(second);
  return (std::max(a, b) + 0.05f) / (std::min(a, b) + 0.05f);
}

const Palette& palette() { return activePalette; }

const Appearance& appearance() { return activeAppearance; }

void apply(const Appearance& options, float displayScale)
{
  activeAppearance = options;
  activeScale = std::clamp(
      options.ui_scale > 0.0f ? options.ui_scale : std::max(1.0f, displayScale),
      MIN_UI_SCALE, MAX_UI_SCALE);

  ImGuiStyle& style = ImGui::GetStyle();
  style = ImGuiStyle();
  applyColors();

  const bool compact = options.compact;
  style.FontSizeBase = BASE_FONT_SIZE;
  style.WindowPadding = ImVec2(Space::L, Space::M);
  style.FramePadding = compact ? ImVec2(8.0f, 5.0f) : ImVec2(10.0f, 8.0f);
  style.CellPadding = compact ? ImVec2(6.0f, 2.0f) : ImVec2(Space::S, 5.0f);
  style.ItemSpacing = compact ? ImVec2(6.0f, 5.0f) : ImVec2(Space::S, Space::S);
  style.ItemInnerSpacing = ImVec2(6.0f, Space::XS);
  style.IndentSpacing = Space::L;
  style.ScrollbarSize = 12.0f;
  style.GrabMinSize = 10.0f;
  style.WindowRounding = 6.0f;
  style.ChildRounding = 6.0f;
  style.FrameRounding = 4.0f;
  style.PopupRounding = 6.0f;
  style.ScrollbarRounding = 6.0f;
  style.GrabRounding = 4.0f;
  style.TabRounding = 4.0f;
  style.WindowBorderSize = 1.0f;
  style.ChildBorderSize = 1.0f;
  style.PopupBorderSize = 1.0f;
  style.FrameBorderSize = options.preset == Preset::HIGH_CONTRAST ? 1.0f : 0.0f;
  style.TabBarOverlineSize = 2.0f;
  style.SelectableTextAlign = ImVec2(0.0f, 0.5f);
  style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
  style.SeparatorTextBorderSize = 1.0f;
  style.SeparatorTextPadding = ImVec2(0.0f, Space::XS);

  // Thin lines are tessellated instead of sampled from the font atlas: the
  // SDL_Renderer backend samples the baked line texels inconsistently, which
  // left broken separators and table borders.
  style.AntiAliasedLinesUseTex = false;

  style.ScaleAllSizes(activeScale);
  style.FontScaleDpi = activeScale;
  // Text size on top of the UI scale: every font size (PushFont included)
  // is multiplied by it, while paddings and fixed sizes follow the UI scale.
  style.FontScaleMain = std::clamp(options.text_scale, 0.85f, 1.5f);
}

void setClub(std::optional<TeamID> teamId)
{
  if (activeClub == teamId) return;
  activeClub = teamId;
  applyColors();
}

float scale() { return activeScale; }

bool reducedMotion() { return activeAppearance.reduced_motion; }

Swatch presetSwatch(Preset preset)
{
  const PresetColors& colors = PRESETS[std::min(
      static_cast<size_t>(std::to_underlying(preset)), PRESETS.size() - 1)];
  return {colors.background, colors.surface, colors.text, colors.muted};
}

const char* presetKey(Preset preset)
{
  using enum Preset;
  switch (preset)
  {
    case DARK_SLATE:
      return "THEME_PRESET_DARK_SLATE";
    case MIDNIGHT_BLUE:
      return "THEME_PRESET_MIDNIGHT";
    case PITCH_GREEN:
      return "THEME_PRESET_PITCH";
    case LIGHT:
      return "THEME_PRESET_LIGHT";
    case HIGH_CONTRAST:
      return "THEME_PRESET_HIGH_CONTRAST";
    case TRUE_DARK:
      return "THEME_PRESET_TRUE_DARK";
    case COUNT:
      break;
  }
  return "THEME_PRESET_DARK_SLATE";
}

ImVec4 clubAccent(TeamID teamId)
{
  return CLUB_ACCENTS[static_cast<size_t>(teamId) % CLUB_ACCENTS.size()];
}

ImVec4 ratingColor(double value)
{
  const auto rating = static_cast<float>(value);
  const RatingScale& stops =
      RATING_SCALES[visionIndex(activeAppearance.color_vision)];
  ImVec4 color = stops.back().color;
  if (rating <= stops.front().value)
  {
    color = stops.front().color;
  }
  else
  {
    for (size_t index = 1; index < stops.size(); ++index)
    {
      const RatingStop& upper = stops[index];
      if (rating <= upper.value)
      {
        const RatingStop& lower = stops[index - 1];
        color = mix(lower.color, upper.color,
                    (rating - lower.value) / (upper.value - lower.value));
        break;
      }
    }
  }
  // Saturated mid tones (yellow, light green) wash out on white surfaces.
  if (presetColors().light) color = mix(color, ImVec4(0, 0, 0, 1), 0.28f);
  return color;
}

float textSize(Text level)
{
  using enum Text;
  switch (level)
  {
    case CAPTION:
      return 12.5f;
    case SMALL:
      return 14.0f;
    case BODY:
      return BASE_FONT_SIZE;
    case TITLE:
      return 18.0f;
    case HEADING:
      return 25.0f;
    case DISPLAY:
      return 28.0f;
  }
  return BASE_FONT_SIZE;
}

ImU32 toU32(const ImVec4& color, float alpha)
{
  return ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, color.w * alpha));
}

uint32_t packRgb(const ImVec4& color)
{
  const auto channel = [](float value)
  {
    return static_cast<uint32_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
  };
  return (channel(color.x) << 16U) | (channel(color.y) << 8U) |
         channel(color.z);
}

ImVec4 unpackRgb(uint32_t rgb)
{
  return ImVec4(static_cast<float>((rgb >> 16U) & 0xFFU) / 255.0f,
                static_cast<float>((rgb >> 8U) & 0xFFU) / 255.0f,
                static_cast<float>(rgb & 0xFFU) / 255.0f, 1.0f);
}

ScopedText::ScopedText(Text level)
{
  ImGui::PushFont(nullptr, textSize(level));
}

ScopedText::~ScopedText() { ImGui::PopFont(); }

}  // namespace Theme
