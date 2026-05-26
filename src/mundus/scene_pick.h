#pragma once
#include "mundus/scene.h"
#include "vista/camera.h"

namespace Mundus {

int pickEntity(Scene &scene, Vista::Camera &camera,
               float mouseX, float mouseY,
               int viewportW, int viewportH);

} // namespace Mundus
