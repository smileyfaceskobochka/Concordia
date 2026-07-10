#include "visibility_system.h"
#include "mundus/components.h"
#include <functional>
#include <SDL3/SDL.h>

namespace Mundus {

void VisibilitySystem::update(flecs::world &ecs) {
  SDL_Log("DBG VS: update begin");
  std::function<void(flecs::entity, bool)> recurse;
  recurse = [&](flecs::entity e, bool parentVisible) {
    const Visibility *v = e.try_get<Visibility>();
    if (!v) return;
    bool effective = v->visible && parentVisible;
    auto &ev = e.get_mut<EffectiveVisibility>();
    ev.visible = effective;

    e.children([&](flecs::entity child) {
      recurse(child, effective);
    });
  };

  SDL_Log("DBG VS: building root query");
  ecs.query_builder<>()
      .with<Visibility>()
      .without(flecs::ChildOf, flecs::Wildcard)
      .build()
      .each([&](flecs::iter &it, size_t i) {
        SDL_Log("DBG VS: root entity %u", it.entity(i).raw_id());
        recurse(it.entity(i), true);
      });
  SDL_Log("DBG VS: update end");
}

} // namespace Mundus
