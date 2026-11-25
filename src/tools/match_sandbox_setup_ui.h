// Football Management Project — see LICENSE in the project root.
#pragma once
struct ImFont;
namespace SandboxSetupUI
{
void render(float scale, ImFont* setup_regular_font, ImFont* setup_medium_font,
            ImFont* sidebar_font, bool& full_familiarity,
            bool& debug_at_kick_off);
}
