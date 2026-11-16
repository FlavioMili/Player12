// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

/**
 * @brief Style configuration for buttons.
 */
struct ButtonStyle
{
  SDL_Color backgroundColor = {70, 70, 150, 255};
  SDL_Color borderColor = {100, 100, 200, 255};
  SDL_Color textColor = {255, 255, 255, 255};
  SDL_Color hoverBackgroundColor = {90, 90, 170, 255};
  SDL_Color hoverBorderColor = {120, 120, 220, 255};
  SDL_Color selectedBackgroundColor = {120, 120, 220, 255};
  SDL_Color selectedBorderColor = {150, 150, 250, 255};
  bool hasBorder = true;
  int borderWidth = 1;
};

/**
 * @brief Represents a single UI button.
 */
struct Button
{
  SDL_FRect rect{};
  SDL_Texture* textTexture = nullptr;
  SDL_FRect textRect{};
  std::string label;
  std::function<void()> onClick;
  bool isHovered = false;
  bool isVisible = true;
  bool isSelected = false;
  ButtonStyle style;
  int id = -1;
};

/**
 * @brief Manages a collection of UI buttons.
 */
class ButtonManager
{
 public:
  /**
   * @brief Constructs a new ButtonManager.
   * @param renderer_ptr The SDL renderer used for drawing buttons.
   * @param font_ptr The font used for button text.
   */
  ButtonManager(SDL_Renderer* renderer_ptr, TTF_Font* font_ptr);

  /**
   * @brief Destroys the ButtonManager.
   */
  ~ButtonManager();
  ButtonManager(const ButtonManager&) = delete;
  ButtonManager& operator=(const ButtonManager&) = delete;

  /**
   * @brief Gets the total number of buttons managed.
   * @return The number of buttons.
   */
  int getButtonCount() const { return static_cast<int>(buttons.size()); }
  // Button creation and management

  /**
   * @brief Adds a new button.
   * @param rect The rectangle defining the button's position and size.
   * @param label The text displayed on the button.
   * @param callback The function to call when the button is clicked.
   * @return The ID of the newly created button.
   */
  int addButton(const SDL_FRect& rect, std::string_view label,
                std::function<void()> callback);

  /**
   * @brief Adds a new button with a specific style.
   * @param rect The rectangle defining the button's position and size.
   * @param label The text displayed on the button.
   * @param style The style to apply to the button.
   * @param callback The function to call when the button is clicked.
   * @return The ID of the newly created button.
   */
  int addButton(const SDL_FRect& rect, std::string_view label,
                const ButtonStyle& style, std::function<void()> callback);

  /**
   * @brief Adds a new button using individual coordinates.
   * @param x The x-coordinate.
   * @param y The y-coordinate.
   * @param w The width.
   * @param h The height.
   * @param label The text displayed on the button.
   * @param callback The function to call when the button is clicked.
   * @return The ID of the newly created button.
   */
  int addButton(float x, float y, float w, float h, std::string_view label,
                std::function<void()> callback);

  /**
   * @brief Adds a new button with a specific style using individual
   * coordinates.
   * @param x The x-coordinate.
   * @param y The y-coordinate.
   * @param w The width.
   * @param h The height.
   * @param label The text displayed on the button.
   * @param style The style to apply to the button.
   * @param callback The function to call when the button is clicked.
   * @return The ID of the newly created button.
   */
  int addButton(float x, float y, float w, float h, std::string_view label,
                const ButtonStyle& style, std::function<void()> callback);

  /**
   * @brief Removes a button by ID.
   * @param buttonId The ID of the button to remove.
   */
  void removeButton(int buttonId);

  /**
   * @brief Clears all managed buttons.
   */
  void clearButtons();

  // Button modification
  /**
   * @brief Sets the style of a specific button.
   * @param buttonId The ID of the button.
   * @param style The new style to apply.
   */
  void setButtonStyle(int buttonId, const ButtonStyle& style);

  /**
   * @brief Sets the visibility of a specific button.
   * @param buttonId The ID of the button.
   * @param visible True to make it visible, false to hide it.
   */
  void setButtonVisible(int buttonId, bool visible);

  /**
   * @brief Sets the enabled state of a specific button.
   * @param buttonId The ID of the button.
   * @param enabled True to enable it, false to disable it.
   */
  void setButtonEnabled(int buttonId, bool enabled);

  /**
   * @brief Sets the text label of a specific button.
   * @param buttonId The ID of the button.
   * @param text The new text for the button.
   */
  void setButtonText(int buttonId, std::string_view text);

  /**
   * @brief Sets whether a specific button is selected.
   * @param buttonId The ID of the button.
   * @param selected True if selected, false otherwise.
   */
  void setButtonSelected(int buttonId, bool selected);

  // Event handling
  /**
   * @brief Handles mouse movement events for buttons.
   * @param x The mouse x-coordinate.
   * @param y The mouse y-coordinate.
   */
  void handleMouseMove(float x, float y);

  /**
   * @brief Handles mouse click events for buttons.
   * @param x The mouse x-coordinate.
   * @param y The mouse y-coordinate.
   * @return True if a button was clicked, false otherwise.
   */
  bool handleMouseClick(float x, float y) const;

  // Rendering
  /**
   * @brief Renders all visible buttons.
   */
  void render();

  // Utility
  /**
   * @brief Sets the font used for all buttons.
   * @param newFont The new font to use.
   */
  void setFont(TTF_Font* newFont) { font = newFont; }

  /**
   * @brief Gets the font currently used for buttons.
   * @return The font.
   */
  TTF_Font* getFont() const { return font; }

  /**
   * @brief Sets the default style applied to new buttons.
   * @param style The default style.
   */
  void setDefaultStyle(const ButtonStyle& style) { defaultStyle = style; }

  /**
   * @brief Gets a copy of the list of buttons.
   * @return A vector of buttons.
   */
  const std::vector<Button>& getButtons() const { return buttons; }

  /**
   * @brief Recreates textures for all buttons (e.g. on text change).
   */
  void recreateTextures();

  /**
   * @brief Updates a button's position by its index.
   * @param buttonIndex The index of the button in the internal list.
   * @param newRect The new rectangle.
   */
  void updateButtonPosition(size_t buttonIndex, SDL_FRect newRect);

  /**
   * @brief Updates a button's position by its ID.
   * @param buttonId The ID of the button.
   * @param newRect The new rectangle.
   */
  void updateButtonPositionById(int buttonId, SDL_FRect newRect);

 private:
  void renderButton(const Button& button);
  bool isPointInButton(float x, float y, const Button& button) const;
  void createButtonTexture(Button& btn);

  SDL_Renderer* renderer;
  TTF_Font* font;
  std::vector<Button> buttons;
  ButtonStyle defaultStyle;
  int nextButtonId = 0;
};
