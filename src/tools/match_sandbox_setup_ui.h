// Football Management Project — see LICENSE in the project root.
#pragma once
struct ImFont;
namespace SandboxSetupUI
{
void render(float scale, ImFont* regular, ImFont* medium, ImFont* bold,
            bool& fullFamiliarity, bool& debugAtKickOff);
}
