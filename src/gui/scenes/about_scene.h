// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <span>
#include <string>
#include <vector>

#include "gui/gui_scene.h"

/** @brief Third-party software credited on the About screen. */
namespace Credits
{
struct Component
{
  const char* name;
  const char* licence;  ///< Licence name, e.g. "zlib License".
  const char* file;     ///< Licence text under assets/licenses/.
  bool tests_only;      ///< Used to test the game, not shipped with it.
};

/** Every component, in display order. */
std::span<const Component> components();
}  // namespace Credits

/**
 * @brief About screen: version and build of this copy, the game's licence
 * status and the third-party software with its licence texts. Opened from
 * the main menu or the command palette.
 */
class AboutScene : public GUIScene
{
 public:
  /** @param inCareer Leaving pops back to the career (else the main menu). */
  AboutScene(GUIView* guiView_ptr, bool inCareer);

  void onEnter() override;
  void update(float /*deltaTime*/) override
  {
    // Rendering reads current state; there is no per-frame update work.
  }
  void render() override;
  [[nodiscard]] SceneID getID() const override { return SceneID::ABOUT; }

 private:
  void renderBuild();
  void renderThirdParty();
  void leave();

  bool in_career = false;
  std::string version_line;
  std::string build_line;
  /** Licence texts, one per component (empty when the file is missing). */
  std::vector<std::string> licence_texts;
};
