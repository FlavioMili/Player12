// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/input_actions.h"

#include <algorithm>
#include <format>
#include <memory>

#include "gui/view_models/match_changes.h"
#include "model/settings_manager.h"

namespace Input
{
namespace
{
/** A bindable key: stable file name, label and physical key. */
struct KeyInfo
{
  ImGuiKey key;
  const char* name;  /*!< Written to the settings file. */
  const char* label; /*!< Shown to the player (nullptr: same as name). */
  SDL_Scancode scancode;
};

// clang-format off
constexpr std::array<KeyInfo, 90> KEYS = {{
    {ImGuiKey_A, "A", nullptr, SDL_SCANCODE_A}, {ImGuiKey_B, "B", nullptr, SDL_SCANCODE_B},
    {ImGuiKey_C, "C", nullptr, SDL_SCANCODE_C}, {ImGuiKey_D, "D", nullptr, SDL_SCANCODE_D},
    {ImGuiKey_E, "E", nullptr, SDL_SCANCODE_E}, {ImGuiKey_F, "F", nullptr, SDL_SCANCODE_F},
    {ImGuiKey_G, "G", nullptr, SDL_SCANCODE_G}, {ImGuiKey_H, "H", nullptr, SDL_SCANCODE_H},
    {ImGuiKey_I, "I", nullptr, SDL_SCANCODE_I}, {ImGuiKey_J, "J", nullptr, SDL_SCANCODE_J},
    {ImGuiKey_K, "K", nullptr, SDL_SCANCODE_K}, {ImGuiKey_L, "L", nullptr, SDL_SCANCODE_L},
    {ImGuiKey_M, "M", nullptr, SDL_SCANCODE_M}, {ImGuiKey_N, "N", nullptr, SDL_SCANCODE_N},
    {ImGuiKey_O, "O", nullptr, SDL_SCANCODE_O}, {ImGuiKey_P, "P", nullptr, SDL_SCANCODE_P},
    {ImGuiKey_Q, "Q", nullptr, SDL_SCANCODE_Q}, {ImGuiKey_R, "R", nullptr, SDL_SCANCODE_R},
    {ImGuiKey_S, "S", nullptr, SDL_SCANCODE_S}, {ImGuiKey_T, "T", nullptr, SDL_SCANCODE_T},
    {ImGuiKey_U, "U", nullptr, SDL_SCANCODE_U}, {ImGuiKey_V, "V", nullptr, SDL_SCANCODE_V},
    {ImGuiKey_W, "W", nullptr, SDL_SCANCODE_W}, {ImGuiKey_X, "X", nullptr, SDL_SCANCODE_X},
    {ImGuiKey_Y, "Y", nullptr, SDL_SCANCODE_Y}, {ImGuiKey_Z, "Z", nullptr, SDL_SCANCODE_Z},
    {ImGuiKey_0, "0", nullptr, SDL_SCANCODE_0}, {ImGuiKey_1, "1", nullptr, SDL_SCANCODE_1},
    {ImGuiKey_2, "2", nullptr, SDL_SCANCODE_2}, {ImGuiKey_3, "3", nullptr, SDL_SCANCODE_3},
    {ImGuiKey_4, "4", nullptr, SDL_SCANCODE_4}, {ImGuiKey_5, "5", nullptr, SDL_SCANCODE_5},
    {ImGuiKey_6, "6", nullptr, SDL_SCANCODE_6}, {ImGuiKey_7, "7", nullptr, SDL_SCANCODE_7},
    {ImGuiKey_8, "8", nullptr, SDL_SCANCODE_8}, {ImGuiKey_9, "9", nullptr, SDL_SCANCODE_9},
    {ImGuiKey_F1, "F1", nullptr, SDL_SCANCODE_F1}, {ImGuiKey_F2, "F2", nullptr, SDL_SCANCODE_F2},
    {ImGuiKey_F3, "F3", nullptr, SDL_SCANCODE_F3}, {ImGuiKey_F4, "F4", nullptr, SDL_SCANCODE_F4},
    {ImGuiKey_F5, "F5", nullptr, SDL_SCANCODE_F5}, {ImGuiKey_F6, "F6", nullptr, SDL_SCANCODE_F6},
    {ImGuiKey_F7, "F7", nullptr, SDL_SCANCODE_F7}, {ImGuiKey_F8, "F8", nullptr, SDL_SCANCODE_F8},
    {ImGuiKey_F9, "F9", nullptr, SDL_SCANCODE_F9}, {ImGuiKey_F10, "F10", nullptr, SDL_SCANCODE_F10},
    {ImGuiKey_F11, "F11", nullptr, SDL_SCANCODE_F11}, {ImGuiKey_F12, "F12", nullptr, SDL_SCANCODE_F12},
    {ImGuiKey_Space, "Space", nullptr, SDL_SCANCODE_SPACE},
    {ImGuiKey_Enter, "Enter", nullptr, SDL_SCANCODE_RETURN},
    {ImGuiKey_Escape, "Escape", "Esc", SDL_SCANCODE_ESCAPE},
    {ImGuiKey_Tab, "Tab", nullptr, SDL_SCANCODE_TAB},
    {ImGuiKey_Backspace, "Backspace", nullptr, SDL_SCANCODE_BACKSPACE},
    {ImGuiKey_Delete, "Delete", "Del", SDL_SCANCODE_DELETE},
    {ImGuiKey_Insert, "Insert", "Ins", SDL_SCANCODE_INSERT},
    {ImGuiKey_Home, "Home", nullptr, SDL_SCANCODE_HOME},
    {ImGuiKey_End, "End", nullptr, SDL_SCANCODE_END},
    {ImGuiKey_PageUp, "PageUp", "PgUp", SDL_SCANCODE_PAGEUP},
    {ImGuiKey_PageDown, "PageDown", "PgDn", SDL_SCANCODE_PAGEDOWN},
    {ImGuiKey_LeftArrow, "Left", nullptr, SDL_SCANCODE_LEFT},
    {ImGuiKey_RightArrow, "Right", nullptr, SDL_SCANCODE_RIGHT},
    {ImGuiKey_UpArrow, "Up", nullptr, SDL_SCANCODE_UP},
    {ImGuiKey_DownArrow, "Down", nullptr, SDL_SCANCODE_DOWN},
    {ImGuiKey_Minus, "Minus", "-", SDL_SCANCODE_MINUS},
    {ImGuiKey_Equal, "Equal", "=", SDL_SCANCODE_EQUALS},
    {ImGuiKey_Comma, "Comma", ",", SDL_SCANCODE_COMMA},
    {ImGuiKey_Period, "Period", ".", SDL_SCANCODE_PERIOD},
    {ImGuiKey_Slash, "Slash", "/", SDL_SCANCODE_SLASH},
    {ImGuiKey_Semicolon, "Semicolon", ";", SDL_SCANCODE_SEMICOLON},
    {ImGuiKey_Apostrophe, "Apostrophe", "'", SDL_SCANCODE_APOSTROPHE},
    {ImGuiKey_LeftBracket, "LeftBracket", "[", SDL_SCANCODE_LEFTBRACKET},
    {ImGuiKey_RightBracket, "RightBracket", "]", SDL_SCANCODE_RIGHTBRACKET},
    {ImGuiKey_Backslash, "Backslash", "\\", SDL_SCANCODE_BACKSLASH},
    {ImGuiKey_GraveAccent, "Grave", "`", SDL_SCANCODE_GRAVE},
    {ImGuiKey_Keypad0, "Keypad0", "Num 0", SDL_SCANCODE_KP_0},
    {ImGuiKey_Keypad1, "Keypad1", "Num 1", SDL_SCANCODE_KP_1},
    {ImGuiKey_Keypad2, "Keypad2", "Num 2", SDL_SCANCODE_KP_2},
    {ImGuiKey_Keypad3, "Keypad3", "Num 3", SDL_SCANCODE_KP_3},
    {ImGuiKey_Keypad4, "Keypad4", "Num 4", SDL_SCANCODE_KP_4},
    {ImGuiKey_Keypad5, "Keypad5", "Num 5", SDL_SCANCODE_KP_5},
    {ImGuiKey_Keypad6, "Keypad6", "Num 6", SDL_SCANCODE_KP_6},
    {ImGuiKey_Keypad7, "Keypad7", "Num 7", SDL_SCANCODE_KP_7},
    {ImGuiKey_Keypad8, "Keypad8", "Num 8", SDL_SCANCODE_KP_8},
    {ImGuiKey_Keypad9, "Keypad9", "Num 9", SDL_SCANCODE_KP_9},
    {ImGuiKey_KeypadAdd, "KeypadAdd", "Num +", SDL_SCANCODE_KP_PLUS},
    {ImGuiKey_KeypadSubtract, "KeypadSubtract", "Num -", SDL_SCANCODE_KP_MINUS},
    {ImGuiKey_KeypadMultiply, "KeypadMultiply", "Num *", SDL_SCANCODE_KP_MULTIPLY},
    {ImGuiKey_KeypadDivide, "KeypadDivide", "Num /", SDL_SCANCODE_KP_DIVIDE},
    {ImGuiKey_KeypadEnter, "KeypadEnter", "Num Enter", SDL_SCANCODE_KP_ENTER},
    {ImGuiKey_KeypadDecimal, "KeypadDecimal", "Num .", SDL_SCANCODE_KP_PERIOD},
}};
// clang-format on

struct ModInfo
{
  ImGuiKeyChord mod;
  const char* name;
};

constexpr std::array<ModInfo, 4> MODS = {{
    {ImGuiMod_Ctrl, "Ctrl"},
    {ImGuiMod_Shift, "Shift"},
    {ImGuiMod_Alt, "Alt"},
    {ImGuiMod_Super, "Super"},
}};

constexpr ImGuiKeyChord MOD_MASK =
    ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiMod_Alt | ImGuiMod_Super;

const KeyInfo* keyInfo(ImGuiKey key)
{
  const auto found = std::ranges::find(KEYS, key, &KeyInfo::key);
  return found != KEYS.end() ? std::to_address(found) : nullptr;
}

ImGuiKey keyOf(ImGuiKeyChord chord)
{
  return static_cast<ImGuiKey>(chord & ~MOD_MASK);
}

ImGuiKeyChord modsOfEvent(SDL_Keymod mod)
{
  ImGuiKeyChord mods = 0;
  if ((mod & SDL_KMOD_CTRL) != 0) mods |= ImGuiMod_Ctrl;
  if ((mod & SDL_KMOD_SHIFT) != 0) mods |= ImGuiMod_Shift;
  if ((mod & SDL_KMOD_ALT) != 0) mods |= ImGuiMod_Alt;
  if ((mod & SDL_KMOD_GUI) != 0) mods |= ImGuiMod_Super;
  return mods;
}

ImGuiKey keyOfScancode(SDL_Scancode scancode)
{
  const auto found = std::ranges::find(KEYS, scancode, &KeyInfo::scancode);
  return found != KEYS.end() ? found->key : ImGuiKey_None;
}

/**
 * Keys by what they type (letters, digits, punctuation) and the named keys
 * (Esc, Enter, arrows, F-keys...) by their key code, so an event that only
 * carries the key code still matches.
 */
ImGuiKey keyOfKeycode(SDL_Keycode keycode)
{
  if (keycode >= SDLK_A && keycode <= SDLK_Z)
    return static_cast<ImGuiKey>(ImGuiKey_A +
                                 static_cast<int>(keycode - SDLK_A));
  if (keycode >= SDLK_0 && keycode <= SDLK_9)
    return static_cast<ImGuiKey>(ImGuiKey_0 +
                                 static_cast<int>(keycode - SDLK_0));
  // Keys without a character carry their scancode in the key code.
  if ((keycode & SDLK_SCANCODE_MASK) != 0)
    return keyOfScancode(
        static_cast<SDL_Scancode>(keycode & ~SDLK_SCANCODE_MASK));
  switch (keycode)
  {
    case SDLK_ESCAPE:
      return ImGuiKey_Escape;
    case SDLK_RETURN:
      return ImGuiKey_Enter;
    case SDLK_SPACE:
      return ImGuiKey_Space;
    case SDLK_TAB:
      return ImGuiKey_Tab;
    case SDLK_BACKSPACE:
      return ImGuiKey_Backspace;
    case SDLK_DELETE:
      return ImGuiKey_Delete;
    case SDLK_MINUS:
      return ImGuiKey_Minus;
    case SDLK_EQUALS:
      return ImGuiKey_Equal;
    case SDLK_COMMA:
      return ImGuiKey_Comma;
    case SDLK_PERIOD:
      return ImGuiKey_Period;
    case SDLK_SLASH:
      return ImGuiKey_Slash;
    case SDLK_SEMICOLON:
      return ImGuiKey_Semicolon;
    case SDLK_APOSTROPHE:
      return ImGuiKey_Apostrophe;
    case SDLK_LEFTBRACKET:
      return ImGuiKey_LeftBracket;
    case SDLK_RIGHTBRACKET:
      return ImGuiKey_RightBracket;
    case SDLK_BACKSLASH:
      return ImGuiKey_Backslash;
    case SDLK_GRAVE:
      return ImGuiKey_GraveAccent;
    default:
      return ImGuiKey_None;
  }
}

ImGuiKeyChord chordOf(ImGuiKey key, ImGuiKeyChord mods)
{
  return key == ImGuiKey_None ? ImGuiKeyChord{ImGuiKey_None} : (key | mods);
}

void registerBuiltins(ActionRegistry& registry)
{
  using C = Category;
  using X = Context;
  const auto add = [&registry](std::string_view id, const char* label,
                               Category category, Context context,
                               ImGuiKeyChord primary,
                               ImGuiKeyChord alternate = ImGuiKey_None,
                               bool rebindable = true)
  {
    registry.registerAction({std::string(id), label, category, context, primary,
                             alternate, rebindable});
  };
  add(Ids::NAV_HOME, "NAV_HOME", C::NAVIGATION, X::MANAGEMENT, ImGuiKey_F1);
  add(Ids::NAV_INBOX, "NAV_INBOX", C::NAVIGATION, X::MANAGEMENT, ImGuiKey_F2);
  add(Ids::NAV_SQUAD, "NAV_SQUAD", C::NAVIGATION, X::MANAGEMENT, ImGuiKey_F3);
  add(Ids::NAV_TRAINING, "NAV_TRAINING", C::NAVIGATION, X::MANAGEMENT,
      ImGuiKey_F4);
  add(Ids::NAV_MATCHES, "NAV_HUB_MATCHES", C::NAVIGATION, X::MANAGEMENT,
      ImGuiKey_F5);
  add(Ids::NAV_RECRUITMENT, "NAV_HUB_RECRUITMENT", C::NAVIGATION, X::MANAGEMENT,
      ImGuiKey_F6);
  add(Ids::NAV_CLUB, "NAV_CLUB", C::NAVIGATION, X::MANAGEMENT, ImGuiKey_F7);
  add(Ids::NAV_PALETTE, "ACTION_PALETTE", C::NAVIGATION, X::MANAGEMENT,
      ImGuiMod_Ctrl | ImGuiKey_K);
  add(Ids::NAV_BACK, "ACTION_BACK", C::NAVIGATION, X::MANAGEMENT,
      ImGuiMod_Alt | ImGuiKey_LeftArrow);
  add(Ids::NAV_FORWARD, "ACTION_FORWARD", C::NAVIGATION, X::MANAGEMENT,
      ImGuiMod_Alt | ImGuiKey_RightArrow);
  add(Ids::NAV_CLOSE, "ACTION_CLOSE", C::NAVIGATION, X::MANAGEMENT,
      ImGuiKey_Escape);
  add(Ids::CAREER_CONTINUE, "ACTION_CONTINUE", C::CAREER, X::MANAGEMENT,
      ImGuiKey_Space, ImGuiKey_Enter);
  add(Ids::CAREER_SAVE, "ACTION_SAVE", C::CAREER, X::MANAGEMENT,
      ImGuiMod_Ctrl | ImGuiKey_S);
  add(Ids::CAREER_HELP, "ACTION_HELP", C::CAREER, X::MANAGEMENT, ImGuiKey_F8);
  add(Ids::SCREENSHOT, "ACTION_SCREENSHOT", C::CAREER, X::GLOBAL, ImGuiKey_F12,
      ImGuiKey_None, false);

  add(Ids::MATCH_PAUSE, "ACTION_MATCH_PAUSE", C::MATCH, X::MATCH,
      ImGuiKey_Space);
  add(Ids::MATCH_FASTER, "ACTION_MATCH_FASTER", C::MATCH, X::MATCH,
      ImGuiKey_Period);
  add(Ids::MATCH_SLOWER, "ACTION_MATCH_SLOWER", C::MATCH, X::MATCH,
      ImGuiKey_Comma);
  add(Ids::MATCH_SUBSTITUTIONS, "ACTION_MATCH_SUBSTITUTIONS", C::MATCH,
      X::MATCH, ImGuiKey_S);
  add(Ids::MATCH_TACTICS, "ACTION_MATCH_TACTICS", C::MATCH, X::MATCH,
      ImGuiKey_T);
  add(Ids::MATCH_MUTE, "ACTION_MATCH_MUTE", C::MATCH, X::MATCH, ImGuiKey_M);
  add(Ids::MATCH_PITCH_FOCUS, "ACTION_MATCH_PITCH_FOCUS", C::MATCH, X::MATCH,
      ImGuiKey_F);
  add(Ids::MATCH_BACK, "ACTION_MATCH_BACK", C::MATCH, X::MATCH,
      ImGuiKey_Escape);
  add(Ids::MATCH_FULLSCREEN, "ACTION_MATCH_FULLSCREEN", C::MATCH, X::MATCH,
      ImGuiMod_Alt | ImGuiKey_Enter);

  add(Ids::CAMERA_VIEW_TOGGLE, "ACTION_CAMERA_VIEW", C::CAMERA, X::MATCH,
      ImGuiKey_V);
  add(Ids::CAMERA_BROADCAST, "ACTION_CAMERA_BROADCAST", C::CAMERA, X::MATCH,
      ImGuiKey_1);
  add(Ids::CAMERA_TACTICAL, "ACTION_CAMERA_TACTICAL", C::CAMERA, X::MATCH,
      ImGuiKey_2);
  add(Ids::CAMERA_END, "ACTION_CAMERA_END", C::CAMERA, X::MATCH, ImGuiKey_3);
  add(Ids::CAMERA_PLAYER, "ACTION_CAMERA_PLAYER", C::CAMERA, X::MATCH,
      ImGuiKey_4);
  add(Ids::CAMERA_FREE, "ACTION_CAMERA_FREE", C::CAMERA, X::MATCH, ImGuiKey_5);
  add(Ids::CAMERA_DIRECTOR, "ACTION_CAMERA_DIRECTOR", C::CAMERA, X::MATCH,
      ImGuiKey_6);
  add(Ids::CAMERA_FOLLOW_BALL, "ACTION_CAMERA_FOLLOW_BALL", C::CAMERA, X::MATCH,
      ImGuiKey_B);
  add(Ids::CAMERA_RESET, "ACTION_CAMERA_RESET", C::CAMERA, X::MATCH,
      ImGuiKey_R);

  // Play mode: the keys drive the active footballer instead of the
  // manager's match keys (the contexts never overlap).
  add(Ids::PLAY_UP, "ACTION_PLAY_UP", C::PLAY, X::PLAY, ImGuiKey_W,
      ImGuiKey_UpArrow);
  add(Ids::PLAY_DOWN, "ACTION_PLAY_DOWN", C::PLAY, X::PLAY, ImGuiKey_S,
      ImGuiKey_DownArrow);
  add(Ids::PLAY_LEFT, "ACTION_PLAY_LEFT", C::PLAY, X::PLAY, ImGuiKey_A,
      ImGuiKey_LeftArrow);
  add(Ids::PLAY_RIGHT, "ACTION_PLAY_RIGHT", C::PLAY, X::PLAY, ImGuiKey_D,
      ImGuiKey_RightArrow);
  add(Ids::PLAY_PASS, "ACTION_PLAY_PASS", C::PLAY, X::PLAY, ImGuiKey_J);
  add(Ids::PLAY_SHOOT, "ACTION_PLAY_SHOOT", C::PLAY, X::PLAY, ImGuiKey_K);
  add(Ids::PLAY_THROUGH, "ACTION_PLAY_THROUGH", C::PLAY, X::PLAY, ImGuiKey_L);
  add(Ids::PLAY_LOB, "ACTION_PLAY_LOB", C::PLAY, X::PLAY, ImGuiKey_I);
  add(Ids::PLAY_SWITCH, "ACTION_PLAY_SWITCH", C::PLAY, X::PLAY, ImGuiKey_Q);
  add(Ids::PLAY_JOCKEY, "ACTION_PLAY_JOCKEY", C::PLAY, X::PLAY, ImGuiKey_E);
  add(Ids::PLAY_PAUSE, "ACTION_PLAY_PAUSE", C::PLAY, X::PLAY, ImGuiKey_Escape,
      ImGuiKey_P);

  // Shift with the number row, then the key right of 0 (MatchShoutsBar).
  static constexpr std::array<ImGuiKey, Ids::SHOUT_COUNT> SHOUT_KEYS = {
      ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4, ImGuiKey_5,    ImGuiKey_6,
      ImGuiKey_7, ImGuiKey_8, ImGuiKey_9, ImGuiKey_0, ImGuiKey_Minus};
  static_assert(MatchChanges::SHOUTS.size() == Ids::SHOUT_COUNT);
  for (std::size_t index = 0; index < Ids::SHOUT_COUNT; ++index)
    add(Ids::shout(index), MatchChanges::SHOUTS[index].labelKey, C::SHOUTS,
        X::MATCH, ImGuiMod_Shift | SHOUT_KEYS[index]);
}
}  // namespace

std::string Ids::shout(std::size_t index)
{
  return std::format("match.shout.{}", index);
}

const char* categoryKey(Category category)
{
  using enum Category;
  switch (category)
  {
    case NAVIGATION:
      return "CONTROLS_CATEGORY_NAVIGATION";
    case CAREER:
      return "CONTROLS_CATEGORY_CAREER";
    case MATCH:
      return "CONTROLS_CATEGORY_MATCH";
    case CAMERA:
      return "CONTROLS_CATEGORY_CAMERA";
    case SHOUTS:
      return "CONTROLS_CATEGORY_SHOUTS";
    case PLAY:
      return "CONTROLS_CATEGORY_PLAY";
    case COUNT:
      break;
  }
  return "CONTROLS_CATEGORY_NAVIGATION";
}

bool contextsOverlap(Context first, Context second)
{
  // PLAY keys replace the manager's match keys while the player controls
  // the team (the match scene listens to one set at a time), so the two
  // never clash: WASD can move a player and S still open substitutions.
  return first == Context::GLOBAL || second == Context::GLOBAL ||
         first == second;
}

bool isBindableKey(ImGuiKey key) { return keyInfo(key) != nullptr; }

std::string chordToString(ImGuiKeyChord chord)
{
  const KeyInfo* info = keyInfo(keyOf(chord));
  if (info == nullptr) return {};
  std::string text;
  for (const ModInfo& mod : MODS)
    if ((chord & mod.mod) != 0) text += std::string(mod.name) + "+";
  return text + info->name;
}

std::optional<ImGuiKeyChord> chordFromString(std::string_view text)
{
  if (text.empty()) return ImGuiKeyChord{ImGuiKey_None};
  ImGuiKeyChord mods = 0;
  // Modifiers come first, each followed by '+'; the key may itself be "+"
  // free (Minus/Equal are spelled out), so split on '+' safely.
  while (true)
  {
    const std::size_t plus = text.find('+');
    if (plus == std::string_view::npos || plus + 1 >= text.size()) break;
    const std::string_view part = text.substr(0, plus);
    const auto mod = std::ranges::find(MODS, part, &ModInfo::name);
    if (mod == MODS.end()) return std::nullopt;
    mods |= mod->mod;
    text.remove_prefix(plus + 1);
  }
  const auto key = std::ranges::find_if(
      KEYS, [text](const KeyInfo& info) { return text == info.name; });
  if (key == KEYS.end()) return std::nullopt;
  return key->key | mods;
}

std::string chordLabel(ImGuiKeyChord chord)
{
  const KeyInfo* info = keyInfo(keyOf(chord));
  if (info == nullptr) return "-";
  std::string text;
  for (const ModInfo& mod : MODS)
    if ((chord & mod.mod) != 0) text += std::string(mod.name) + "+";
  return text + (info->label != nullptr ? info->label : info->name);
}

std::optional<ImGuiKeyChord> capturePressedChord()
{
  const ImGuiIO& io = ImGui::GetIO();
  for (const KeyInfo& info : KEYS)
    if (ImGui::IsKeyPressed(info.key, false))
      return info.key | (io.KeyMods & MOD_MASK);
  return std::nullopt;
}

std::array<ImGuiKeyChord, 2> chordsOfEvent(const SDL_KeyboardEvent& event)
{
  const ImGuiKeyChord mods = modsOfEvent(event.mod);
  return {chordOf(keyOfKeycode(event.key), mods),
          chordOf(keyOfScancode(event.scancode), mods)};
}

ActionRegistry::ActionRegistry() = default;

ActionId ActionRegistry::registerAction(const ActionDef& def)
{
  if (const auto existing = find(def.id); existing.has_value())
    return *existing;
  const ActionId id = actions.size();
  actions.push_back({def, {def.primary, def.alternate}});
  by_id.try_emplace(def.id, id);
  applyStored(id);
  ++revision_;
  return id;
}

std::optional<ActionId> ActionRegistry::find(std::string_view id) const
{
  const auto found = by_id.find(id);
  if (found == by_id.end()) return std::nullopt;
  return found->second;
}

const Action& ActionRegistry::action(ActionId id) const
{
  return actions.at(id);
}

ImGuiKeyChord ActionRegistry::chord(ActionId id, std::size_t slot) const
{
  if (id >= actions.size() || slot >= BINDING_SLOTS) return ImGuiKey_None;
  return actions[id].chords[slot];
}

std::string ActionRegistry::label(ActionId id) const
{
  const ImGuiKeyChord first = chord(id, 0);
  return chordLabel(first != ImGuiKey_None ? first : chord(id, 1));
}

bool ActionRegistry::pressed(ActionId id, ImGuiInputFlags flags) const
{
  if (id >= actions.size()) return false;
  bool hit = false;
  for (const ImGuiKeyChord bound : actions[id].chords)
    if (bound != ImGuiKey_None && ImGui::Shortcut(bound, flags)) hit = true;
  return hit;
}

bool ActionRegistry::pressed(std::string_view id, ImGuiInputFlags flags) const
{
  const auto found = find(id);
  return found && pressed(*found, flags);
}

bool ActionRegistry::matches(ActionId id, const SDL_KeyboardEvent& event) const
{
  if (id >= actions.size()) return false;
  const auto chords = chordsOfEvent(event);
  return std::ranges::any_of(
      actions[id].chords,
      [&chords](ImGuiKeyChord bound)
      {
        return bound != ImGuiKey_None &&
               (bound == chords[0] || bound == chords[1]);
      });
}

bool ActionRegistry::matches(std::string_view id,
                             const SDL_KeyboardEvent& event) const
{
  const auto found = find(id);
  return found && matches(*found, event);
}

bool ActionRegistry::matchesKey(ActionId id,
                                const SDL_KeyboardEvent& event) const
{
  if (id >= actions.size()) return false;
  const ImGuiKey byCharacter = keyOfKeycode(event.key);
  const ImGuiKey byPosition = keyOfScancode(event.scancode);
  return std::ranges::any_of(actions[id].chords,
                             [&](ImGuiKeyChord bound)
                             {
                               const ImGuiKey key = keyOf(bound);
                               return key != ImGuiKey_None &&
                                      (key == byCharacter || key == byPosition);
                             });
}

bool ActionRegistry::matchesKey(std::string_view id,
                                const SDL_KeyboardEvent& event) const
{
  const auto found = find(id);
  return found && matchesKey(*found, event);
}

std::optional<std::pair<ActionId, std::size_t>> ActionRegistry::conflictFor(
    ActionId id, ImGuiKeyChord chord, std::size_t slot) const
{
  if (chord == ImGuiKey_None || id >= actions.size()) return std::nullopt;
  const Context context = actions[id].def.context;
  for (ActionId other = 0; other < actions.size(); ++other)
  {
    if (!contextsOverlap(context, actions[other].def.context)) continue;
    for (std::size_t otherSlot = 0; otherSlot < BINDING_SLOTS; ++otherSlot)
    {
      if (other == id && otherSlot == slot) continue;
      if (actions[other].chords[otherSlot] == chord)
        return std::make_pair(other, otherSlot);
    }
  }
  return std::nullopt;
}

RebindResult ActionRegistry::rebind(ActionId id, std::size_t slot,
                                    ImGuiKeyChord chord, bool replace)
{
  RebindResult result;
  if (id >= actions.size() || slot >= BINDING_SLOTS ||
      !actions[id].def.rebindable)
    return result;
  if (chord != ImGuiKey_None && !isBindableKey(keyOf(chord))) return result;
  if (const auto clash = conflictFor(id, chord, slot))
  {
    if (!replace || !actions[clash->first].def.rebindable)
    {
      result.conflict = clash->first;
      result.conflict_slot = clash->second;
      return result;
    }
    // The same chord may sit in this action's other slot: clear it too.
    actions[clash->first].chords[clash->second] = ImGuiKey_None;
    store(clash->first);
  }
  actions[id].chords[slot] = chord;
  store(id);
  ++revision_;
  result.applied = true;
  return result;
}

void ActionRegistry::resetToDefault(ActionId id)
{
  if (id >= actions.size()) return;
  Action& entry = actions[id];
  entry.chords = {entry.def.primary, entry.def.alternate};
  store(id);
  ++revision_;
}

void ActionRegistry::resetAll()
{
  for (Action& action : actions)
    action.chords = {action.def.primary, action.def.alternate};
  // Bindings of actions not registered in this run are dropped as well.
  SettingsManager::instance()->get().key_bindings.clear();
  ++revision_;
}

bool ActionRegistry::isDefault(ActionId id) const
{
  if (id >= actions.size()) return true;
  const Action& entry = actions[id];
  return entry.chords[0] == entry.def.primary &&
         entry.chords[1] == entry.def.alternate;
}

void ActionRegistry::reloadFromSettings()
{
  for (ActionId id = 0; id < actions.size(); ++id)
  {
    actions[id].chords = {actions[id].def.primary, actions[id].def.alternate};
    applyStored(id);
  }
  ++revision_;
}

void ActionRegistry::forEach(
    Category category,
    const std::function<void(ActionId, const Action&)>& visit) const
{
  for (ActionId id = 0; id < actions.size(); ++id)
    if (actions[id].def.category == category) visit(id, actions[id]);
}

void ActionRegistry::applyStored(ActionId id)
{
  Action& entry = actions[id];
  if (!entry.def.rebindable) return;
  const auto& stored = SettingsManager::instance()->get().key_bindings;
  const auto found = stored.find(entry.def.id);
  if (found == stored.end()) return;
  for (std::size_t slot = 0;
       slot < BINDING_SLOTS && slot < found->second.size(); ++slot)
  {
    const auto parsed = chordFromString(found->second[slot]);
    // A broken name keeps the default rather than unbinding the action.
    if (parsed.has_value()) entry.chords[slot] = *parsed;
  }
}

void ActionRegistry::store(ActionId id) const
{
  const Action& entry = actions[id];
  auto& stored = SettingsManager::instance()->get().key_bindings;
  if (isDefault(id))
  {
    stored.erase(entry.def.id);
    return;
  }
  std::vector<std::string> names;
  names.reserve(BINDING_SLOTS);
  for (const ImGuiKeyChord bound : entry.chords)
    names.push_back(chordToString(bound));
  stored[entry.def.id] = std::move(names);
}

ActionRegistry& registry()
{
  static ActionRegistry instance = []
  {
    ActionRegistry created;
    registerBuiltins(created);
    return created;
  }();
  return instance;
}

}  // namespace Input
