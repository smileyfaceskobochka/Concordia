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
    m_front = glm::normalize(target - eye);
    m_worldUp = up;
    
    m_pitch = glm::degrees(asin(m_front.y));
    m_yaw   = glm::degrees(atan2(m_front.z, m_front.x));
    updateCameraVectors();
}

void Camera::processKeyboard(bool forward, bool backward, bool left, bool right, bool upKey, bool downKey, float deltaTime) {
    float velocity = moveSpeed * deltaTime;
    if (forward)  m_eye += m_front * velocity;
    if (backward) m_eye -= m_front * velocity;
    if (left)     m_eye -= m_right * velocity;
    if (right)    m_eye += m_right * velocity;
    if (upKey)    m_eye += m_worldUp * velocity;
    if (downKey)  m_eye -= m_worldUp * velocity;
}

void Camera::processMouse(glm::vec2 mouseDelta) {
    m_yaw += mouseDelta.x * mouseSensitivity;
    m_pitch -= mouseDelta.y * mouseSensitivity; // Reversed since Y coordinates go from top to bottom

    if (m_pitch > 89.0f) m_pitch = 89.0f;
    if (m_pitch < -89.0f) m_pitch = -89.0f;

    updateCameraVectors();
}

void Camera::updateCameraVectors() {
    glm::vec3 front;
    front.x = cos(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
    front.y = sin(glm::radians(m_pitch));
    front.z = sin(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
    m_front = glm::normalize(front);
    
    m_right = glm::normalize(glm::cross(m_front, m_worldUp));
    m_up    = glm::normalize(glm::cross(m_right, m_front));
}

glm::mat4 Camera::getView() const {
    return glm::lookAt(m_eye, m_eye + m_front, m_up);
}

glm::mat4 Camera::getProj() const {
    auto proj = glm::perspective(glm::radians(m_fov), m_aspect, m_near, m_far);
    proj[1][1] *= -1; // Vulkan Y is down, GLM is up
    return proj;
}

} // namespace Vista
