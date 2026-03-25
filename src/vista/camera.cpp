#include "camera.h"
#include <glm/gtc/matrix_transform.hpp>

namespace Vista {

void Camera::setPerspective(float fovDeg, float aspect, float nearP, float farP) {
    m_fov = fovDeg;
    m_aspect = aspect;
    m_near = nearP;
    m_far = farP;
}

void Camera::lookAt(glm::vec3 eye, glm::vec3 target, glm::vec3 up) {
    m_eye = eye;
    m_target = target;
    m_up = up;
}

glm::mat4 Camera::getView() const {
    return glm::lookAt(m_eye, m_target, m_up);
}

glm::mat4 Camera::getProj() const {
    auto proj = glm::perspective(glm::radians(m_fov), m_aspect, m_near, m_far);
    proj[1][1] *= -1; // Vulkan Y is down, GLM is up
    return proj;
}

} // namespace Vista
