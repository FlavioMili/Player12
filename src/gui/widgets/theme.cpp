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

#include "database/datagenerator.h"
#include "gui/render/match_kit_colors.h"

namespace
{
constexpr float BASE_FONT_SIZE = 16.0F;
constexpr float MIN_UI_SCALE = 0.75F;
constexpr float MAX_UI_SCALE = 2.0F;

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
  float row_stripe_alpha = 0.0F;
  bool light = false;
};

constexpr std::array<PresetColors, static_cast<size_t>(std::to_underlying(
                                       Theme::Preset::COUNT))>
    PRESETS = {{
        // Slate (default): a soft graphite with clear elevation steps.
        {.background = ImVec4(0.118F, 0.133F, 0.153F, 1.0F),  // #1E2227
         .sidebar = ImVec4(0.137F, 0.153F, 0.176F, 1.0F),     // #23272D
         .surface = ImVec4(0.165F, 0.184F, 0.212F, 1.0F),     // #2A2F36
         .raised = ImVec4(0.208F, 0.231F, 0.267F, 1.0F),      // #353B44
         .border = ImVec4(0.275F, 0.302F, 0.345F, 1.0F),      // #464D58
         .text = ImVec4(0.940F, 0.950F, 0.965F, 1.0F),
         .muted = ImVec4(0.680F, 0.715F, 0.760F, 1.0F),
         .faint = ImVec4(0.505F, 0.545F, 0.595F, 1.0F),
         .row_stripe_alpha = 0.030F,
         .light = false},
        // Midnight blue.
        {.background = ImVec4(0.086F, 0.106F, 0.165F, 1.0F),
         .sidebar = ImVec4(0.102F, 0.125F, 0.192F, 1.0F),
         .surface = ImVec4(0.125F, 0.153F, 0.231F, 1.0F),
         .raised = ImVec4(0.165F, 0.200F, 0.290F, 1.0F),
         .border = ImVec4(0.235F, 0.275F, 0.380F, 1.0F),
         .text = ImVec4(0.930F, 0.945F, 0.980F, 1.0F),
         .muted = ImVec4(0.655F, 0.700F, 0.800F, 1.0F),
         .faint = ImVec4(0.475F, 0.520F, 0.625F, 1.0F),
         .row_stripe_alpha = 0.030F,
         .light = false},
        // Pitch green.
        {.background = ImVec4(0.078F, 0.122F, 0.102F, 1.0F),
         .sidebar = ImVec4(0.094F, 0.145F, 0.122F, 1.0F),
         .surface = ImVec4(0.114F, 0.173F, 0.145F, 1.0F),
         .raised = ImVec4(0.153F, 0.224F, 0.192F, 1.0F),
         .border = ImVec4(0.216F, 0.298F, 0.259F, 1.0F),
         .text = ImVec4(0.930F, 0.965F, 0.945F, 1.0F),
         .muted = ImVec4(0.655F, 0.745F, 0.700F, 1.0F),
         .faint = ImVec4(0.470F, 0.560F, 0.515F, 1.0F),
         .row_stripe_alpha = 0.030F,
         .light = false},
        // Light.
        {.background = ImVec4(0.925F, 0.933F, 0.945F, 1.0F),
         .sidebar = ImVec4(0.965F, 0.969F, 0.976F, 1.0F),
         .surface = ImVec4(1.000F, 1.000F, 1.000F, 1.0F),
         .raised = ImVec4(0.906F, 0.914F, 0.929F, 1.0F),
         .border = ImVec4(0.800F, 0.812F, 0.835F, 1.0F),
         .text = ImVec4(0.078F, 0.090F, 0.110F, 1.0F),
         .muted = ImVec4(0.330F, 0.360F, 0.405F, 1.0F),
         .faint = ImVec4(0.480F, 0.510F, 0.555F, 1.0F),
         .row_stripe_alpha = 0.030F,
         .light = true},
        // High contrast.
        {.background = ImVec4(0.000F, 0.000F, 0.000F, 1.0F),
         .sidebar = ImVec4(0.030F, 0.030F, 0.030F, 1.0F),
         .surface = ImVec4(0.050F, 0.050F, 0.050F, 1.0F),
         .raised = ImVec4(0.140F, 0.140F, 0.140F, 1.0F),
         .border = ImVec4(0.720F, 0.720F, 0.720F, 1.0F),
         .text = ImVec4(1.000F, 1.000F, 1.000F, 1.0F),
         .muted = ImVec4(0.850F, 0.850F, 0.850F, 1.0F),
         .faint = ImVec4(0.700F, 0.700F, 0.700F, 1.0F),
         .row_stripe_alpha = 0.060F,
         .light = false},
        // True dark: near-black charcoal for dim rooms and OLED panels.
        {.background = ImVec4(0.045F, 0.055F, 0.066F, 1.0F),
         .sidebar = ImVec4(0.058F, 0.069F, 0.082F, 1.0F),
         .surface = ImVec4(0.075F, 0.088F, 0.103F, 1.0F),
         .raised = ImVec4(0.112F, 0.128F, 0.147F, 1.0F),
         .border = ImVec4(0.160F, 0.182F, 0.205F, 1.0F),
         .text = ImVec4(0.905F, 0.925F, 0.940F, 1.0F),
         .muted = ImVec4(0.600F, 0.650F, 0.690F, 1.0F),
         .faint = ImVec4(0.420F, 0.460F, 0.500F, 1.0F),
         .row_stripe_alpha = 0.022F,
         .light = false},
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
        {.positive = ImVec4(0.235F, 0.770F, 0.486F, 1.0F),
         .warning = ImVec4(0.945F, 0.706F, 0.255F, 1.0F),
         .negative = ImVec4(0.937F, 0.416F, 0.396F, 1.0F),
         .info = ImVec4(0.400F, 0.660F, 0.965F, 1.0F)},
        {.positive = ImVec4(0.380F, 0.650F, 1.000F, 1.0F),
         .warning = ImVec4(0.950F, 0.830F, 0.300F, 1.0F),
         .negative = ImVec4(0.960F, 0.520F, 0.180F, 1.0F),
         .info = ImVec4(0.760F, 0.640F, 0.980F, 1.0F)},
        {.positive = ImVec4(0.200F, 0.780F, 0.740F, 1.0F),
         .warning = ImVec4(0.980F, 0.640F, 0.400F, 1.0F),
         .negative = ImVec4(0.960F, 0.420F, 0.520F, 1.0F),
         .info = ImVec4(0.640F, 0.660F, 0.980F, 1.0F)},
    }};
constexpr std::array<StatusColors, static_cast<size_t>(std::to_underlying(
                                       Theme::ColorVision::COUNT))>
    STATUS_LIGHT = {{
        {.positive = ImVec4(0.090F, 0.470F, 0.250F, 1.0F),
         .warning = ImVec4(0.560F, 0.360F, 0.000F, 1.0F),
         .negative = ImVec4(0.700F, 0.150F, 0.140F, 1.0F),
         .info = ImVec4(0.120F, 0.360F, 0.720F, 1.0F)},
        {.positive = ImVec4(0.100F, 0.350F, 0.750F, 1.0F),
         .warning = ImVec4(0.500F, 0.400F, 0.000F, 1.0F),
         .negative = ImVec4(0.650F, 0.270F, 0.000F, 1.0F),
         .info = ImVec4(0.420F, 0.250F, 0.650F, 1.0F)},
        {.positive = ImVec4(0.000F, 0.430F, 0.400F, 1.0F),
         .warning = ImVec4(0.620F, 0.300F, 0.050F, 1.0F),
         .negative = ImVec4(0.700F, 0.120F, 0.300F, 1.0F),
         .info = ImVec4(0.330F, 0.300F, 0.700F, 1.0F)},
    }};

// Club accents are chosen for contrast on the dark surfaces, not to imitate
// any real club identity.
constexpr std::array<ImVec4, 10> CLUB_ACCENTS = {
    ImVec4(0.130F, 0.650F, 0.390F, 1.0F), ImVec4(0.235F, 0.560F, 0.925F, 1.0F),
    ImVec4(0.855F, 0.290F, 0.310F, 1.0F), ImVec4(0.925F, 0.620F, 0.180F, 1.0F),
    ImVec4(0.560F, 0.420F, 0.910F, 1.0F), ImVec4(0.110F, 0.660F, 0.690F, 1.0F),
    ImVec4(0.930F, 0.450F, 0.210F, 1.0F), ImVec4(0.330F, 0.420F, 0.900F, 1.0F),
    ImVec4(0.830F, 0.330F, 0.620F, 1.0F), ImVec4(0.520F, 0.700F, 0.200F, 1.0F),
};

struct RatingStop
{
  float value = 0.0F;
  ImVec4 color;
};

using RatingScale = std::array<RatingStop, 5>;

// Weak to strong: red to green, or diverging scales that avoid the
// red-green (orange to blue) and blue-yellow (magenta to teal) confusions.
constexpr std::array<RatingScale, static_cast<size_t>(std::to_underlying(
                                      Theme::ColorVision::COUNT))>
    RATING_SCALES = {{
        {{{.value = 35.0F,
           .color =
               ImVec4(0.898F, 0.337F, std::numbers::inv_pi_v<float>, 1.0F)},
          {.value = 50.0F, .color = ImVec4(0.945F, 0.560F, 0.255F, 1.0F)},
          {.value = 60.0F, .color = ImVec4(0.905F, 0.740F, 0.250F, 1.0F)},
          {.value = 70.0F, .color = ImVec4(0.520F, 0.780F, 0.330F, 1.0F)},
          {.value = 80.0F, .color = ImVec4(0.180F, 0.760F, 0.500F, 1.0F)}}},
        {{{.value = 35.0F, .color = ImVec4(0.900F, 0.420F, 0.100F, 1.0F)},
          {.value = 50.0F, .color = ImVec4(0.940F, 0.620F, 0.250F, 1.0F)},
          {.value = 60.0F, .color = ImVec4(0.800F, 0.780F, 0.600F, 1.0F)},
          {.value = 70.0F, .color = ImVec4(0.500F, 0.680F, 0.950F, 1.0F)},
          {.value = 80.0F, .color = ImVec4(0.300F, 0.550F, 1.000F, 1.0F)}}},
        {{{.value = 35.0F, .color = ImVec4(0.900F, 0.300F, 0.420F, 1.0F)},
          {.value = 50.0F, .color = ImVec4(0.900F, 0.520F, 0.620F, 1.0F)},
          {.value = 60.0F, .color = ImVec4(0.780F, 0.720F, 0.780F, 1.0F)},
          {.value = 70.0F, .color = ImVec4(0.420F, 0.780F, 0.760F, 1.0F)},
          {.value = 80.0F, .color = ImVec4(0.150F, 0.740F, 0.700F, 1.0F)}}},
    }};

Theme::Appearance activeAppearance;
std::optional<TeamID> activeClub;
float activeScale = 1.0F;
Theme::Palette activePalette{};
thread_local const Theme::Palette* scopedPalette = nullptr;

ImVec4 mix(const ImVec4& a, const ImVec4& b, float t)
{
  return ImVec4(a.x + ((b.x - a.x) * t), a.y + ((b.y - a.y) * t),
                a.z + ((b.z - a.z) * t), a.w + ((b.w - a.w) * t));
}

const PresetColors& presetColors()
{
  const auto index =
      std::min(static_cast<size_t>(std::to_underlying(activeAppearance.preset)),
               PRESETS.size() - 1);
  return PRESETS.at(index);
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
  return channel <= 0.04045F ? channel / 12.92F
                             : std::pow((channel + 0.055F) / 1.055F, 2.4F);
}

float relativeLuminance(const ImVec4& color)
{
  return (0.2126F * channelLuminance(color.x)) +
         (0.7152F * channelLuminance(color.y)) +
         (0.0722F * channelLuminance(color.z));
}

// Lower saturation and brightness of the accent for control fills.
ImVec4 mutedAccent(const ImVec4& accent, bool light)
{
  float hue = 0.0F;
  float saturation = 0.0F;
  float value = 0.0F;
  ImGui::ColorConvertRGBtoHSV(accent.x, accent.y, accent.z, hue, saturation,
                              value);
  saturation *= 0.62F;
  value = light ? std::min(value, 0.62F) : std::clamp(value, 0.45F, 0.72F);
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
  auto& colors = ImGui::GetStyle().Colors;
  // Controls stay neutral; only handles, checks and fills carry a muted
  // version of the accent, so bright custom accents never flood the screen.
  const ImVec4 control = mutedAccent(p.accent, preset.light);
  const ImVec4 controlHover = mix(control, p.text, 0.18F);
  const ImVec4 neutralHover = mix(p.raised, p.border, 0.65F);
  const ImVec4 neutralActive = mix(p.raised, p.border, 0.95F);
  const ImVec4 selection = mix(p.raised, control, 0.22F);
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
  colors[ImGuiCol_FrameBgHovered] = mix(p.raised, p.border, 0.7F);
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
  colors[ImGuiCol_HeaderActive] = mix(p.raised, control, 0.32F);
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
  colors[ImGuiCol_TableBorderLight] = mix(p.surface, p.border, 0.6F);
  colors[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
  colors[ImGuiCol_TableRowBgAlt] = stripe;
  colors[ImGuiCol_TextLink] =
      mix(p.accent, p.text, preset.light ? 0.1F : 0.35F);
  colors[ImGuiCol_TextSelectedBg] = mix(p.surface, p.accent, 0.45F);
  colors[ImGuiCol_DragDropTarget] = controlHover;
  colors[ImGuiCol_NavCursor] = controlHover;
  colors[ImGuiCol_NavWindowingHighlight] = p.text;
  colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0, 0, 0, 0.45F);
  colors[ImGuiCol_ModalWindowDimBg] =
      ImVec4(0.0F, 0.0F, 0.0F, preset.light ? 0.35F : 0.62F);
}
}  // namespace

namespace Theme
{

Palette presetPalette(Preset preset, ColorVision vision, const ImVec4& accent)
{
  const PresetColors& colors = PRESETS.at(std::min(
      static_cast<size_t>(std::to_underlying(preset)), PRESETS.size() - 1));
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
      (colors.light ? STATUS_LIGHT : STATUS_DARK).at(visionIndex(vision));
  p.positive = status.positive;
  p.warning = status.warning;
  p.negative = status.negative;
  p.info = status.info;
  p.accent = accent;
  const float luminance =
      (0.2126F * p.accent.x) + (0.7152F * p.accent.y) + (0.0722F * p.accent.z);
  p.on_accent = luminance > 0.45F ? ImVec4(0.03F, 0.04F, 0.05F, 1.0F)
                                  : ImVec4(1.0F, 1.0F, 1.0F, 1.0F);
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
  return (std::max(a, b) + 0.05F) / (std::min(a, b) + 0.05F);
}

const Palette& palette()
{
  return (scopedPalette != nullptr) ? *scopedPalette : activePalette;
}

ScopedPalette::ScopedPalette(Palette colours)
    : value(colours), previous(scopedPalette)
{
  scopedPalette = &value;
}
ScopedPalette::~ScopedPalette() { scopedPalette = previous; }

const Appearance& appearance() { return activeAppearance; }

void apply(const Appearance& options, float displayScale)
{
  activeAppearance = options;
  activeScale = std::clamp(
      options.ui_scale > 0.0F ? options.ui_scale : std::max(1.0F, displayScale),
      MIN_UI_SCALE, MAX_UI_SCALE);

  ImGuiStyle& style = ImGui::GetStyle();
  style = ImGuiStyle();
  applyColors();

  const bool compact = options.compact;
  style.FontSizeBase = BASE_FONT_SIZE;
  style.WindowPadding = ImVec2(Space::L, Space::M);
  style.FramePadding = compact ? ImVec2(8.0F, 5.0F) : ImVec2(10.0F, 8.0F);
  style.CellPadding = compact ? ImVec2(6.0F, 2.0F) : ImVec2(Space::S, 5.0F);
  style.ItemSpacing = compact ? ImVec2(6.0F, 5.0F) : ImVec2(Space::S, Space::S);
  style.ItemInnerSpacing = ImVec2(6.0F, Space::XS);
  style.IndentSpacing = Space::L;
  style.ScrollbarSize = 12.0F;
  style.GrabMinSize = 10.0F;
  style.WindowRounding = 6.0F;
  style.ChildRounding = 6.0F;
  style.FrameRounding = 4.0F;
  style.PopupRounding = 6.0F;
  style.ScrollbarRounding = 6.0F;
  style.GrabRounding = 4.0F;
  style.TabRounding = 4.0F;
  style.WindowBorderSize = 1.0F;
  style.ChildBorderSize = 1.0F;
  style.PopupBorderSize = 1.0F;
  style.FrameBorderSize = options.preset == Preset::HIGH_CONTRAST ? 1.0F : 0.0F;
  style.TabBarOverlineSize = 2.0F;
  style.SelectableTextAlign = ImVec2(0.0F, 0.5F);
  style.WindowTitleAlign = ImVec2(0.0F, 0.5F);
  style.SeparatorTextBorderSize = 1.0F;
  style.SeparatorTextPadding = ImVec2(0.0F, Space::XS);

  // Thin lines are tessellated instead of sampled from the font atlas: the
  // SDL_Renderer backend samples the baked line texels inconsistently, which
  // left broken separators and table borders.
  style.AntiAliasedLinesUseTex = false;

  style.ScaleAllSizes(activeScale);
  style.FontScaleDpi = activeScale;
  // Text size on top of the UI scale: every font size (PushFont included)
  // is multiplied by it, while paddings and fixed sizes follow the UI scale.
  style.FontScaleMain = std::clamp(options.text_scale, 0.85F, 1.5F);
}

void setClub(std::optional<TeamID> teamId)
{
  if (activeClub == teamId)
  {
    return;
  }
  activeClub = teamId;
  applyColors();
}

float scale() { return activeScale; }

bool reducedMotion() { return activeAppearance.reduced_motion; }

Swatch presetSwatch(Preset preset)
{
  const PresetColors& colors = PRESETS.at(std::min(
      static_cast<size_t>(std::to_underlying(preset)), PRESETS.size() - 1));
  return {.background = colors.background,
          .surface = colors.surface,
          .text = colors.text,
          .muted = colors.muted};
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
  // Follow the club's home shirt when the data pack describes it; fall back
  // to a light strip's contrast colour, then to the hashed original.
  if (const ClubIdentity* identity = findClubIdentity(teamId))
  {
    std::uint32_t rgb = identity->primary_colour;
    const auto channel = [](std::uint32_t value, int shift)
    { return static_cast<float>((value >> shift) & 0xFFU) / 255.0f; };
    const float luminance = 0.2126f * channel(rgb, 16) +
                            0.7152f * channel(rgb, 8) +
                            0.0722f * channel(rgb, 0);
    if (luminance > 0.75f) rgb = identity->secondary_colour;
    return ImVec4(channel(rgb, 16), channel(rgb, 8), channel(rgb, 0), 1.0f);
  }
  return CLUB_ACCENTS.at(static_cast<size_t>(teamId) % CLUB_ACCENTS.size());
}

ImVec4 ratingColor(double value)
{
  const auto rating = static_cast<float>(value);
  const RatingScale& stops =
      RATING_SCALES.at(visionIndex(activeAppearance.color_vision));
  ImVec4 color = stops.back().color;
  if (rating <= stops.front().value)
  {
    color = stops.front().color;
  }
  else
  {
    for (size_t index = 1; index < stops.size(); ++index)
    {
      const RatingStop& upper = stops.at(index);
      if (rating <= upper.value)
      {
        const RatingStop& lower = stops.at(index - 1);
        color = mix(lower.color, upper.color,
                    (rating - lower.value) / (upper.value - lower.value));
        break;
      }
    }
  }
  // Saturated mid tones (yellow, light green) wash out on white surfaces.
  if (presetColors().light)
  {
    color = mix(color, ImVec4(0, 0, 0, 1), 0.28F);
  }
  return color;
}

float textSize(Text level)
{
  using enum Text;
  switch (level)
  {
    case CAPTION:
      return 12.5F;
    case SMALL:
      return 14.0F;
    case BODY:
      return BASE_FONT_SIZE;
    case TITLE:
      return 18.0F;
    case HEADING:
      return 25.0F;
    case DISPLAY:
      return 28.0F;
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
    constexpr float CHANNEL_MAX = 255.0F;
    return static_cast<uint32_t>(
        std::lround(std::clamp(value, 0.0F, 1.0F) * CHANNEL_MAX));
  };
  return (channel(color.x) << 16U) | (channel(color.y) << 8U) |
         channel(color.z);
}

ImVec4 unpackRgb(uint32_t rgb)
{
  return ImVec4(static_cast<float>((rgb >> 16U) & 0xFFU) / 255.0F,
                static_cast<float>((rgb >> 8U) & 0xFFU) / 255.0F,
                static_cast<float>(rgb & 0xFFU) / 255.0F, 1.0F);
}

ScopedText::ScopedText(Text level)
{
  ImGui::PushFont(nullptr, textSize(level));
}

ScopedText::~ScopedText() { ImGui::PopFont(); }

}  // namespace Theme
