#include "scene.h"
#include <SDL3/SDL.h>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>

namespace Mundus {

glm::mat4 Transform::getLocalMatrix() const {
  glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
  m = glm::rotate(m, rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
  m = glm::rotate(m, rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
  m = glm::rotate(m, rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
  m = glm::scale(m, scale);
  return m;
}

void Scene::update(float deltaTime) {
  for (auto &ent : m_entities) {
    ent.transform.rotation += ent.transform.angularVelocity * deltaTime;
  }

  std::function<void(int, glm::mat4)> computeHierarchy =
      [&](int index, glm::mat4 parentGlobal) {
        auto &ent = m_entities[index];
        ent.globalTransform = parentGlobal * ent.transform.getLocalMatrix();

        // Sanity check for child hierarchies
        if (ent.parentIndex != -1) {
          // Log removed to avoid spam
        }

        for (int childIdx : ent.children) {
          computeHierarchy(childIdx, ent.globalTransform);
        }
      };

  for (size_t i = 0; i < m_entities.size(); ++i) {
    if (m_entities[i].parentIndex == -1) {
      computeHierarchy(static_cast<int>(i), glm::mat4(1.0f));
    }
  }
}

} // namespace Mundus
