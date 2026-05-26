#pragma once
#include "vigil/overlay.h"
#include "mundus/scene.h"
#include "vista/camera.h"
#include <SDL3/SDL.h>

namespace Vigil {

bool processEditorKeys(const SDL_Event &ev, bool imguiCapturesKeyboard,
                       bool imguiCapturesMouse, bool inputCaptured,
                       Vigil::Overlay &overlay,
                       Mundus::Scene &scene, Vista::Camera &camera);

} // namespace Vigil
