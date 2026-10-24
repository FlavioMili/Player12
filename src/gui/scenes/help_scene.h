// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "gui/gui_scene.h"

/** @brief Football-management terms explained on the Help screen. */
namespace Glossary
{
struct Term
{
  const char* term_key;
  const char* definition_key;
};

/** Every term, in display order. */
std::span<const Term> terms();
}  // namespace Glossary

/**
 * @brief Help screen: getting started, the keyboard shortcuts in force and
 * a searchable glossary. Opened from the command palette, the Help shortcut
 * or the main menu; in a career it can replay the welcome tour.
 */
class HelpScene : public GUIScene
{
 public:
  /**
   * @param inCareer Leaving pops back to the career (else the main menu).
   * @param focusTerm Glossary term to scroll to and highlight.
   */
  HelpScene(GUIView* guiView_ptr, bool inCareer,
            std::optional<std::size_t> focusTerm = std::nullopt);

  void onEnter() override;
  void update(float /*deltaTime*/) override
  {
    // Rendering reads current state; there is no per-frame update work.
  }
  void render() override;
  [[nodiscard]] SceneID getID() const override { return SceneID::HELP; }

 private:
  friend class GameFlowTest_GUIFlowLifecycle_Test;

  void renderStart();
  void renderShortcuts();
  void renderGlossary();
  void rebuildShortcuts();
  void leave();
  void filterTerms();

  bool in_career = false;
  std::optional<std::size_t> focus_term;
  bool focus_pending = false;
  std::optional<float> focus_screen_y; /*!< Where the focused term is drawn. */
  std::array<char, 64> query{};
  std::string filtered_query;
  std::vector<std::size_t> matches;
  struct ShortcutRow
  {
    std::string label_key; /*!< Category heading or action label. */
    std::string keys;      /*!< Chord labels ("Space  /  Enter"). */
    bool heading = false;
    bool is_default = true;
  };
  std::vector<ShortcutRow> shortcut_rows;
  std::uint32_t shortcuts_revision = 0;
  /** Lower-case "term definition" text per glossary entry, for search. */
  std::vector<std::string> search_text;
};
