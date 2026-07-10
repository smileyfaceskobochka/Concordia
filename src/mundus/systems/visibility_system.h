#pragma once

#include <flecs.h>

namespace Mundus {

struct VisibilitySystem {
  static void update(flecs::world &ecs);
};

} // namespace Mundus
