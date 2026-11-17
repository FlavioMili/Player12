// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <imgui.h>

#include <cstdint>
#include <optional>

#include "global/types.h"

/**
 * @brief Design tokens shared by every management screen.
 *
 * A theme is a preset palette plus one accent colour (the managed club's or
 * a custom one). Typography uses the one bundled TTF at several sizes through
 * ImGui's dynamic font atlas, and a small spacing scale keeps rhythm
 * consistent between screens. Everything is re-applied live when the user
 * changes appearance settings or the window moves to another display.
 */
namespace Theme
{

/** @brief Spacing scale in unscaled pixels. */
namespace Space
{
constexpr float XS = 4.0F;
constexpr float S = 8.0F;
constexpr float M = 12.0F;
constexpr float L = 16.0F;
constexpr float XL = 24.0F;
}  // namespace Space

/** @brief Typography scale. */
enum class Text : uint8_t
{
  CAPTION, /**< Column labels, card captions, badges. */
  SMALL,   /**< Secondary table text and helper copy. */
  BODY,    /**< Default text. */
  TITLE,   /**< Card and section titles. */
  HEADING, /**< Page headings. */
  DISPLAY  /**< Large figures on stat tiles. */
};

/** @brief Built-in palettes. */
enum class Preset : uint8_t
{
  DARK_SLATE,
  MIDNIGHT_BLUE,
  PITCH_GREEN,
  LIGHT,
  HIGH_CONTRAST,
  TRUE_DARK, /**< Appended so saved preset indices stay valid. */
  COUNT
};

/**
 * @brief Hues of the status colours. Good and bad are never told apart by
 * red against green alone in the safe modes.
 */
enum class ColorVision : uint8_t
{
  STANDARD,    /**< Green good, red bad. */
  RED_GREEN,   /**< Blue good, orange bad (protanopia, deuteranopia). */
  BLUE_YELLOW, /**< Teal good, magenta bad (tritanopia). */
  COUNT
};

/** @brief User-selected appearance options. */
struct Appearance
{
  Preset preset = Preset::DARK_SLATE;
  bool club_accent = true; /**< Accent follows the managed club. */
  ImVec4 custom_accent = ImVec4(0.130F, 0.650F, 0.390F, 1.0F);
  float ui_scale = 0.0F; /**< 0 = follow the display content scale. */
  bool compact = false;  /**< Tighter paddings for denser tables. */
  bool reduced_motion = false;
  ColorVision color_vision = ColorVision::STANDARD;
  float text_scale = 1.0F; /**< Text size on top of the UI scale. */
};

/** @brief Semantic colours used by widgets. */
struct Palette
{
  ImVec4 background;
  ImVec4 sidebar;
  ImVec4 surface;
  ImVec4 raised;
  ImVec4 border;
  ImVec4 text;
  ImVec4 muted;
  ImVec4 faint;
  ImVec4 positive;
  ImVec4 warning;
  ImVec4 negative;
  ImVec4 info;
  ImVec4 accent;
  ImVec4 on_accent; /**< Text drawn on accent-filled areas. */
};

/**
 * @brief Palette of a preset without applying it (no ImGui state touched),
 * e.g. for previews and contrast checks.
 */
Palette presetPalette(Preset preset, ColorVision vision, const ImVec4& accent);

/** @brief Localisation key of a colour-vision mode's name. */
const char* colorVisionKey(ColorVision vision);

/** @brief WCAG contrast ratio of two opaque colours (1 to 21). */
float contrastRatio(const ImVec4& first, const ImVec4& second);

/** @brief Returns the active palette (accent included). */
const Palette& palette();

/** Temporarily supplies semantic colours to shared widgets without changing
 *
 * the user's appearance or ImGui style. Nested page scopes restore in order. */
class ScopedPalette
{
 public:
  explicit ScopedPalette(Palette value);
  ~ScopedPalette();
  ScopedPalette(const ScopedPalette&) = delete;
  ScopedPalette& operator=(const ScopedPalette&) = delete;

 private:
  Palette value;
  const Palette* previous;
};

/** @brief Returns the active appearance options. */
const Appearance& appearance();

/**
 * @brief Applies an appearance to the current ImGui style.
 * @param options Preset, accent, scale and density choices.
 * @param displayScale Display content scale used when ui_scale is automatic.
 */
void apply(const Appearance& options, float displayScale);

/** @brief Tells the theme which club is managed (for club accents). */
void setClub(std::optional<TeamID> teamId);

/** @brief Effective UI scale (fonts and sizes). */
float scale();

/** @brief True when animations should be avoided. */
bool reducedMotion();

/** @brief Representative colours of a preset, for previews. */
struct Swatch
{
  ImVec4 background;
  ImVec4 surface;
  ImVec4 text;
  ImVec4 muted;
};

/** @brief Preview colours of a preset without applying it. */
Swatch presetSwatch(Preset preset);

/** @brief Localisation key of a preset name. */
const char* presetKey(Preset preset);

/** @brief Deterministic accent colour for a club. */
ImVec4 clubAccent(TeamID teamId);

/** @brief Continuous colour scale for 0-100 ratings (red to green). */
ImVec4 ratingColor(double value);

/** @brief Unscaled pixel size for a typography level. */
float textSize(Text level);

/** @brief Converts a colour with an optional alpha multiplier. */
ImU32 toU32(const ImVec4& color, float alpha = 1.0F);

/** @brief Packs an opaque colour as 0xRRGGBB (for settings files). */
uint32_t packRgb(const ImVec4& color);

/** @brief Unpacks 0xRRGGBB into an opaque colour. */
ImVec4 unpackRgb(uint32_t rgb);

/** @brief Pushes a typography level for the lifetime of the object. */
class ScopedText
{
 public:
  explicit ScopedText(Text level);
  ~ScopedText();
  ScopedText(const ScopedText&) = delete;
  ScopedText& operator=(const ScopedText&) = delete;
};

}  // namespace Theme
