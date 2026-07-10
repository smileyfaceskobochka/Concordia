#include "transform_system.h"
#include "mundus/components.h"
#include <glm/gtc/matrix_transform.hpp>
#include <functional>
#include <SDL3/SDL.h>

namespace Mundus {

void TransformSystem::update(flecs::world &ecs, float deltaTime) {
  SDL_Log("DBG TS: update begin");
  ecs.each([deltaTime](flecs::entity e, Transform &t) {
    (void)e;
    t.rotation += t.angularVelocity * deltaTime;
    SDL_Log("DBG TS: each %u angularVel=(%f,%f,%f)", e.raw_id(),
            t.angularVelocity.x, t.angularVelocity.y, t.angularVelocity.z);
  });

  std::function<void(flecs::entity, glm::mat4)> recurse;
  recurse = [&](flecs::entity e, glm::mat4 parentGlobal) {
    const Transform *t = e.try_get<Transform>();
    if (!t) return;
    SDL_Log("DBG TS: recurse %u", e.raw_id());
    auto &gt = e.get_mut<GlobalTransform>();
    gt.value = parentGlobal * t->getLocalMatrix();

    e.children([&](flecs::entity child) {
      recurse(child, gt.value);
    });
  };

  SDL_Log("DBG TS: building root query");
  ecs.query_builder<>()
      .with<Transform>()
      .without(flecs::ChildOf, flecs::Wildcard)
      .build()
      .each([&](flecs::iter &it, size_t i) {
        SDL_Log("DBG TS: root entity %u", it.entity(i).raw_id());
        recurse(it.entity(i), glm::mat4(1.0f));
      });
  SDL_Log("DBG TS: update end");
}

} // namespace Mundus
