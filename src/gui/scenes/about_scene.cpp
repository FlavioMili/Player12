// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/about_scene.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

#include "global/build_info.h"
#include "global/language_manager.h"
#include "global/paths.h"
#include "gui/gui_view.h"
#include "gui/scenes/main_menu_scene.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"

namespace
{
constexpr float CONTENT_MAX_WIDTH = 860.0f;

// clang-format off
constexpr std::array<Credits::Component, 13> COMPONENTS = {{
    {"SDL3", "zlib License", "SDL3.txt", false},
    {"SDL_ttf", "zlib License", "SDL3_ttf.txt", false},
    {"FreeType", "FreeType License (FTL)", "FreeType.txt", false},
    {"HarfBuzz", "MIT License (Old MIT)", "HarfBuzz.txt", false},
    {"PlutoSVG", "MIT License", "PlutoSVG.txt", false},
    {"PlutoVG", "MIT License", "PlutoVG.txt", false},
    {"Dear ImGui", "MIT License", "DearImGui.txt", false},
    {"SQLite", "Public domain", "SQLite.txt", false},
    {"{fmt}", "MIT License", "fmt.txt", false},
    {"spdlog", "MIT License", "spdlog.txt", false},
    {"JSON for Modern C++", "MIT License", "nlohmann_json.txt", false},
    {"Roboto", "Apache License 2.0", "Roboto.txt", false},
    {"GoogleTest", "BSD 3-Clause License", "GoogleTest.txt", true},
}};
// clang-format on

std::string readText(const std::string& path)
{
  std::ifstream file(path, std::ios::binary);
  std::string text{std::istreambuf_iterator<char>(file), {}};
  std::erase(text, '\r');
  return text;
}
}  // namespace

std::span<const Credits::Component> Credits::components() { return COMPONENTS; }

AboutScene::AboutScene(GUIView* guiView_ptr, bool inCareer)
    : GUIScene(guiView_ptr), in_career(inCareer)
{
}

void AboutScene::onEnter()
{
  version_line =
      formatLocalized("ABOUT_VERSION", {std::string(BuildInfo::version())});
  build_line =
      formatLocalized("ABOUT_BUILD", {std::string(BuildInfo::commit()),
                                      std::string(BuildInfo::configuration()),
                                      std::string(BuildInfo::compiler()),
                                      std::string(BuildInfo::platform())});
  licence_texts.clear();
  licence_texts.reserve(COMPONENTS.size());
  for (const Credits::Component& component : COMPONENTS)
    licence_texts.push_back(readText(AssetPaths::file(
        (std::string("assets/licenses/") + component.file).c_str())));
}

void AboutScene::leave()
{
  if (in_career)
    guiView->popScene();
  else
    changeScene(std::make_unique<MainMenuScene>(guiView));
}

void AboutScene::render()
{
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const float scale = Theme::scale();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(
      ImGuiStyleVar_WindowPadding,
      ImVec2(Theme::Space::XL * scale, Theme::Space::XL * scale));
  ImGui::Begin("##about", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleVar(3);
  const bool leaveWithEscape =
      !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape, false);

  const float width =
      std::min(ImGui::GetContentRegionAvail().x, CONTENT_MAX_WIDTH * scale);
  const float footerHeight =
      ImGui::GetFrameHeightWithSpacing() + Theme::Space::L * scale;
  ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),
                                (ImGui::GetWindowWidth() - width) * 0.5f));
  ImGui::BeginGroup();
  UI::pageHeader(LOC("ABOUT_TITLE"), LOC("ABOUT_SUBTITLE"));
  // The body is the page's only scroll surface.
  ImGui::BeginChild(
      "##about_body",
      ImVec2(width, ImGui::GetContentRegionAvail().y - footerHeight));
  renderBuild();
  renderThirdParty();
  ImGui::EndChild();
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * scale));
  const bool back =
      UI::secondaryButton(LOC("ABOUT_BACK"), ImVec2(160.0f * scale, 0.0f));
  ImGui::EndGroup();
  ImGui::End();
  if (back || leaveWithEscape) leave();
}

void AboutScene::renderBuild()
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("about_build", LOC("ABOUT_SECTION_BUILD"));
  ImGui::PushTextWrapPos(0.0f);
  {
    Theme::ScopedText title(Theme::Text::TITLE);
    ImGui::TextUnformatted(version_line.c_str());
  }
  ImGui::TextColored(palette.muted, "%s", build_line.c_str());
  ImGui::Spacing();
  ImGui::TextUnformatted(LOC("ABOUT_LICENSE"));
  ImGui::TextColored(palette.muted, "%s", LOC("ABOUT_PRIVACY"));
  ImGui::PopTextWrapPos();
  UI::endCard();
}

void AboutScene::renderThirdParty()
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("about_credits", LOC("ABOUT_SECTION_THIRD_PARTY"));
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(palette.muted, "%s", LOC("ABOUT_THIRD_PARTY_HINT"));
  for (std::size_t index = 0; index < COMPONENTS.size(); ++index)
  {
    const Credits::Component& component = COMPONENTS[index];
    ImGui::PushID(static_cast<int>(index));
    // Licence texts open inline: the page stays the only scroll surface.
    if (const bool open =
            ImGui::TreeNodeEx("##licence", ImGuiTreeNodeFlags_SpanAvailWidth,
                              "%s  ·  %s", component.name, component.licence);
        open)
    {
      if (component.tests_only)
        ImGui::TextColored(palette.muted, "%s", LOC("ABOUT_TESTS_ONLY"));
      if (const std::string& text = licence_texts[index]; text.empty())
        ImGui::TextColored(palette.muted, "%s", LOC("ABOUT_LICENSE_MISSING"));
      else
      {
        Theme::ScopedText small(Theme::Text::SMALL);
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
      }
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
  ImGui::PopTextWrapPos();
  UI::endCard();
}
