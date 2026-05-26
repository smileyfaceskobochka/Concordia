#pragma once
#include "vigil/overlay.h"
#include "mundus/scene.h"
#include "vista/camera.h"
#include <SDL3/SDL.h>
#include <string>
#include <vector>

namespace Vigil {

struct EditorKeyBinding {
  std::string action;
  SDL_Keycode key;
  bool ctrl = false;
  bool shift = false;
  bool alt = false;
};

struct EditorKeyConfig {
  SDL_Keycode cameraForward = SDLK_W;
  SDL_Keycode cameraBackward = SDLK_S;
  SDL_Keycode cameraLeft = SDLK_A;
  SDL_Keycode cameraRight = SDLK_D;
  SDL_Keycode cameraUp = SDLK_SPACE;
  SDL_Keycode cameraDown = SDLK_LSHIFT;
  SDL_Keycode captureExit = SDLK_ESCAPE;
  std::vector<EditorKeyBinding> bindings;
};

EditorKeyConfig loadEditorKeyConfig(const std::string &path);

bool processEditorKeys(const SDL_Event &ev, bool imguiCapturesKeyboard,
                       bool imguiCapturesMouse, bool inputCaptured,
                       Vigil::Overlay &overlay,
                       Mundus::Scene &scene, Vista::Camera &camera);

} // namespace Vigil
