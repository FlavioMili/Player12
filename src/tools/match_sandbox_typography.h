// Football Management Project — see LICENSE in the project root.
#pragma once

#include <imgui.h>

namespace SandboxTypography
{
// The reference uses CSS em sizes. stb_truetype normally maps the requested
// size to ascent minus descent instead. Bundled Manrope has 2000 units/em,
// ascent 2132 and descent -600: compensate so 16px here matches CSS 16px.
inline ImFontConfig manropeConfig()
{
  ImFontConfig config;
  config.ExtraSizeScale = (2132.0f + 600.0f) / 2000.0f;
  return config;
}
}  // namespace SandboxTypography
