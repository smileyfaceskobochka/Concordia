#pragma once

#include "entity.h"
#include <vector>
#include <memory>

namespace Mundus {

class Scene {
public:
    Scene() = default;
    ~Scene() = default;

    void addEntity(const Entity& entity) { m_entities.push_back(entity); }
    std::vector<Entity>& getEntities() { return m_entities; }
    const std::vector<Entity>& getEntities() const { return m_entities; }

    void update(float deltaTime);

private:
    std::vector<Entity> m_entities;
};

} // namespace Mundus
