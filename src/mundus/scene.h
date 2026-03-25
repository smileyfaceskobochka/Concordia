#pragma once

#include "entity.h"
#include <vector>
#include <memory>

namespace Mundus {

class Scene {
public:
    Scene() = default;
    ~Scene() = default;

    int addEntity(const std::string& name, int parentIndex = -1) {
        Entity ent{};
        ent.name = name;
        ent.parentIndex = parentIndex;
        m_entities.push_back(ent);
        int idx = static_cast<int>(m_entities.size()) - 1;
        if (parentIndex >= 0 && parentIndex < idx) {
            m_entities[parentIndex].children.push_back(idx);
        }
        return idx;
    }

    void clear() { m_entities.clear(); }

    std::vector<Entity>& getEntities() { return m_entities; }
    const std::vector<Entity>& getEntities() const { return m_entities; }

    void update(float deltaTime);

    glm::vec3 globalLightDir = {-0.5f, -1.0f, -0.2f};
    glm::vec3 globalLightColor = {1.0f, 1.0f, 1.0f};

private:
    std::vector<Entity> m_entities;
};

} // namespace Mundus
