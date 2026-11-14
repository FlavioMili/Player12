// Football Management Project — see LICENSE in the project root.
#pragma once

#include <functional>
#include <future>
#include <memory>

#include "gui/gui_scene.h"

class MatchSandboxScene;
struct ImFont;

/** Sandbox-only startup screen. The loader owns the controller until its
 * future completes; rendering never reads game data. Destruction joins it. */
class SandboxSplashScene final : public GUIScene
{
 public:
  SandboxSplashScene(GUIView* view, std::function<void()> load);
  ~SandboxSplashScene() override;
  void onEnter() override;
  void handleEvent(const SDL_Event& event) override;
  void update(float deltaTime) override;
  void render() override;
  SceneID getID() const override { return SceneID::MATCH_SANDBOX; }

 private:
  ImFont* splash_font = nullptr;
  std::function<void()> loader;
  float elapsed = 0.0f;
  float exit_elapsed = 0.0f;
  bool rendered = false;
  bool ready = false;
  bool skip = false;
  std::unique_ptr<MatchSandboxScene> setup;
  // Declared last so it joins before anything the worker could use is freed.
  std::future<void> loading;
};
