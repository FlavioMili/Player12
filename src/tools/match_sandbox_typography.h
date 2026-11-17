// Football Management Project — see LICENSE in the project root.
#pragma once

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace SandboxTypography
{
// The reference uses CSS em sizes. stb_truetype normally maps the requested
// size to ascent minus descent instead. Bundled Manrope has 2000 units/em,
// ascent 2132 and descent -600: compensate so 16px here matches CSS 16px.
inline ImFontConfig manropeConfig()
{
  ImFontConfig config;
  config.ExtraSizeScale = (2132.0F + 600.0F) / 2000.0F;
  return config;
}

struct LabelBounds
{
  ImVec2 min{FLT_MAX, FLT_MAX}, max{-FLT_MAX, -FLT_MAX};
  bool visible = false;
};

// Button/navigation labels are ASCII. Use visible glyph bounds, including side
// bearings, rather than the font's line box or its trailing advance. Account
// for the renderer snapping each separately tracked character to a pixel.
inline LabelBounds labelBounds(ImFont* font, float pixelSize, const char* label,
                               float tracking, bool snap = true)
{
  LabelBounds bounds;
  auto* baked = font->GetFontBaked(pixelSize);
  const float factor = pixelSize / baked->Size;
  float x = 0;
  for (const unsigned char* c = reinterpret_cast<const unsigned char*>(label);
       *c != 0u; ++c)
  {
    const auto* glyph = baked->FindGlyph(*c);
    const float gx = snap ? std::floor(x) : x;
    if (glyph->Visible)
    {
      bounds.min.x = std::min(bounds.min.x, gx + (glyph->X0 * factor));
      bounds.min.y = std::min(bounds.min.y, glyph->Y0 * factor);
      bounds.max.x = std::max(bounds.max.x, gx + (glyph->X1 * factor));
      bounds.max.y = std::max(bounds.max.y, glyph->Y1 * factor);
      bounds.visible = true;
    }
    x += glyph->AdvanceX * factor + tracking;
  }
  if (!bounds.visible)
  {
    bounds.min = bounds.max = ImVec2(0, 0);
  }
  return bounds;
}

inline void label(ImDrawList* draw, ImFont* font, float size, const char* text,
                  ImVec2 a, ImVec2 b, ImU32 color, float tracking = 0,
                  bool centerHorizontal = true, float leftInset = 0)
{
  ImGui::PushFont(font, size);
  const float pixels = ImGui::GetFontSize();
  const bool snap = (draw->Flags & ImDrawListFlags_TextNoPixelSnap) == 0;
  const auto bounds = labelBounds(font, pixels, text, tracking, snap);
  ImVec2 origin(centerHorizontal ? (a.x + b.x - bounds.min.x - bounds.max.x) / 2
                                 : a.x + leftInset - bounds.min.x,
                (a.y + b.y - bounds.min.y - bounds.max.y) / 2);
  if (snap)
  {
    origin.x = std::round(origin.x);
    origin.y = std::round(origin.y);
  }
  auto* baked = font->GetFontBaked(pixels);
  const float factor = pixels / baked->Size;
  float x = 0;
  for (const char* c = text; *c != 0; ++c)
  {
    const auto* glyph = baked->FindGlyph(static_cast<unsigned char>(*c));
    const float advance = glyph->AdvanceX * factor;
    draw->AddText(font, pixels, ImVec2(origin.x + x, origin.y), color, c,
                  c + 1);
    x += advance + tracking;
  }
  ImGui::PopFont();
}
}  // namespace SandboxTypography
