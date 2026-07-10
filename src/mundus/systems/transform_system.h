#pragma once

#include <flecs.h>

namespace Mundus {

struct TransformSystem {
  static void update(flecs::world &ecs, float deltaTime);
};

} // namespace Mundus
