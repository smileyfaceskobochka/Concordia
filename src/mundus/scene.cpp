#include "scene.h"
#include <glm/gtc/matrix_transform.hpp>

namespace Mundus {

glm::mat4 Transform::getMatrix() const {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
    m = glm::rotate(m, rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
    m = glm::rotate(m, rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
    m = glm::rotate(m, rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
    m = glm::scale(m, scale);
    return m;
}

void Scene::update(float deltaTime) {
    // Basic scene update logic (e.g. simple animations)
    // Can be expanded with ECS-like systems later
}

} // namespace Mundus
