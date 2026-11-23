// Football Management Project — see LICENSE in the project root.
#pragma once

#include <functional>
#include <future>
#include <memory>
#include <thread>

#include "gui/gui_scene.h"

class MatchSandboxScene;
struct ImFont;

/** Sandbox-only startup screen. The loader owns the controller until its
 * future completes; rendering never reads game data. Destruction joins it. */
class SandboxSplashScene final : public GUIScene
{
 public:
  SandboxSplashScene(GUIView* view, std::function<void(std::stop_token)> load);
  ~SandboxSplashScene() override;
  void onEnter() override;
  void handleEvent(const SDL_Event& event) override;
  void update(float deltaTime) override;
  void render() override;
  SceneID getID() const override { return SceneID::MATCH_SANDBOX; }

 private:
  ImFont* splash_font = nullptr;
  std::function<void(std::stop_token)> loader;
  float elapsed = 0.0F;
  float exit_elapsed = 0.0F;
  bool rendered = false;
  bool ready = false;
  bool skip = false;
  bool closing = false;
  std::unique_ptr<MatchSandboxScene> setup;
  std::future<void> loading;
  // Destroyed first so the worker stops and joins before scene teardown.
  std::jthread loading_worker;
};
