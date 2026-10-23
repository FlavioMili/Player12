// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "button_manager.h"

#include <algorithm>
#include <cstddef>
#include <utility>

ButtonManager::ButtonManager(SDL_Renderer* renderer_ptr, TTF_Font* font_ptr)
    : renderer(renderer_ptr), font(font_ptr)
{
}

ButtonManager::~ButtonManager()
{
  for (auto& btn : buttons)
  {
    if (btn.textTexture)
    {
      SDL_DestroyTexture(btn.textTexture);
      btn.textTexture = nullptr;
    }
  }
}

int ButtonManager::addButton(const SDL_FRect& rect, std::string_view label,
                             std::function<void()> callback)
{
  Button button;
  button.rect = rect;
  button.label.assign(label);
  button.onClick = std::move(callback);
  button.style = defaultStyle;
  button.id = nextButtonId++;

  createButtonTexture(button);

  buttons.push_back(button);
  return button.id;
}

int ButtonManager::addButton(const SDL_FRect& rect, std::string_view label,
                             const ButtonStyle& style,
                             std::function<void()> callback)
{
  Button button;
  button.rect = rect;
  button.label.assign(label);
  button.onClick = std::move(callback);
  button.style = defaultStyle;
  button.id = nextButtonId++;
  button.style = style;

  createButtonTexture(button);

  buttons.push_back(button);
  return button.id;
}

int ButtonManager::addButton(float x, float y, float w, float h,
                             std::string_view label,
                             std::function<void()> callback)
{
  return addButton({x, y, w, h}, label, std::move(callback));
}

int ButtonManager::addButton(float x, float y, float w, float h,
                             std::string_view label, const ButtonStyle& style,
                             std::function<void()> callback)
{
  return addButton({x, y, w, h}, label, style, std::move(callback));
}

void ButtonManager::removeButton(int buttonId)
{
  const auto button =
      std::ranges::find_if(buttons, [buttonId](const Button& candidate)
                           { return candidate.id == buttonId; });
  if (button == buttons.end()) return;
  if (button->textTexture) SDL_DestroyTexture(button->textTexture);
  buttons.erase(button);
}

void ButtonManager::clearButtons()
{
  for (auto& button : buttons)
  {
    if (button.textTexture)
    {
      SDL_DestroyTexture(button.textTexture);
      button.textTexture = nullptr;
    }
  }
  buttons.clear();
}

void ButtonManager::setButtonStyle(int buttonId, const ButtonStyle& style)
{
  auto it = std::ranges::find_if(
      buttons, [buttonId](const Button& btn) { return btn.id == buttonId; });
  if (it != buttons.end())
  {
    it->style = style;
  }
}

void ButtonManager::setButtonVisible(int buttonId, bool visible)
{
  auto it = std::ranges::find_if(
      buttons, [buttonId](const Button& btn) { return btn.id == buttonId; });
  if (it != buttons.end())
  {
    it->isVisible = visible;
  }
}

void ButtonManager::setButtonText(int buttonId, std::string_view text)
{
  auto it = std::ranges::find_if(
      buttons, [buttonId](const Button& btn) { return btn.id == buttonId; });
  if (it != buttons.end())
  {
    it->label.assign(text);
    createButtonTexture(*it);
  }
}

void ButtonManager::handleMouseMove(float x, float y)
{
  for (auto& button : buttons)
  {
    if (!button.isVisible) continue;
    button.isHovered = isPointInButton(x, y, button);
  }
}

bool ButtonManager::handleMouseClick(float x, float y) const
{
  return std::ranges::any_of(
      buttons,
      [this, x, y](const Button& button)
      {
        if (!button.isVisible || !isPointInButton(x, y, button)) return false;
        if (button.onClick) button.onClick();
        return true;
      });
}

void ButtonManager::render()
{
  for (const auto& button : buttons)
  {
    if (button.isVisible)
    {
      renderButton(button);
    }
  }
}

void ButtonManager::setButtonSelected(int buttonId, bool selected)
{
  auto it = std::ranges::find_if(
      buttons, [buttonId](const Button& btn) { return btn.id == buttonId; });
  if (it != buttons.end())
  {
    it->isSelected = selected;
  }
}

void ButtonManager::renderButton(const Button& btn)
{
  if (!btn.isVisible) return;

  SDL_Color bgColor = btn.style.backgroundColor;
  if (btn.isSelected)
  {
    bgColor = btn.style.selectedBackgroundColor;
  }
  else if (btn.isHovered)
  {
    bgColor = btn.style.hoverBackgroundColor;
  }

  SDL_SetRenderDrawColor(renderer, bgColor.r, bgColor.g, bgColor.b, bgColor.a);
  SDL_RenderFillRect(renderer, &btn.rect);

  if (btn.style.hasBorder)
  {
    SDL_Color borderColor = btn.style.borderColor;
    if (btn.isSelected)
    {
      borderColor = btn.style.selectedBorderColor;
    }
    else if (btn.isHovered)
    {
      borderColor = btn.style.hoverBorderColor;
    }
    SDL_SetRenderDrawColor(renderer, borderColor.r, borderColor.g,
                           borderColor.b, borderColor.a);
    SDL_RenderRect(renderer, &btn.rect);
  }

  if (btn.textTexture)
  {
    float textW, textH;
    SDL_GetTextureSize(btn.textTexture, &textW, &textH);

    SDL_FRect dynamicTextRect = {btn.rect.x + (btn.rect.w - textW) / 2.0f,
                                 btn.rect.y + (btn.rect.h - textH) / 2.0f,
                                 textW, textH};

    SDL_RenderTexture(renderer, btn.textTexture, nullptr, &dynamicTextRect);
  }
}

bool ButtonManager::isPointInButton(float x, float y,
                                    const Button& button) const
{
  return (x >= button.rect.x && x <= button.rect.x + button.rect.w &&
          y >= button.rect.y && y <= button.rect.y + button.rect.h);
}

void ButtonManager::createButtonTexture(Button& btn)
{
  if (!font || !renderer) return;
  if (btn.textTexture)
  {
    SDL_DestroyTexture(btn.textTexture);
    btn.textTexture = nullptr;
  }

  SDL_Color color = btn.style.textColor;
  SDL_Surface* surf = TTF_RenderText_Blended(font, btn.label.c_str(), 0, color);
  if (!surf) return;

  btn.textTexture = SDL_CreateTextureFromSurface(renderer, surf);
  btn.textRect = {
      btn.rect.x + (btn.rect.w - static_cast<float>(surf->w)) / 2.0f,
      btn.rect.y + (btn.rect.h - static_cast<float>(surf->h)) / 2.0f,
      static_cast<float>(surf->w), static_cast<float>(surf->h)};
  SDL_DestroySurface(surf);
}

void ButtonManager::recreateTextures()
{
  for (auto& b : buttons) createButtonTexture(b);
}

void ButtonManager::updateButtonPosition(size_t buttonIndex, SDL_FRect newRect)
{
  if (buttonIndex >= buttons.size())
  {
    return;
  }

  Button& btn = buttons[buttonIndex];
  btn.rect = newRect;
}

void ButtonManager::updateButtonPositionById(int buttonId, SDL_FRect newRect)
{
  auto it = std::ranges::find_if(
      buttons, [buttonId](const Button& btn) { return btn.id == buttonId; });
  if (it != buttons.end())
  {
    it->rect = newRect;
  }
}
