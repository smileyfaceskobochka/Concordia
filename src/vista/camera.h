#pragma once

#include <glm/glm.hpp>

namespace Vista {

class Camera {
public:
    Camera() : m_eye(0.0f, 0.0f, 2.0f) { updateCameraVectors(); }

    void setPerspective(float fovDeg, float aspect, float nearP, float farP);
    void lookAt(glm::vec3 eye, glm::vec3 target, glm::vec3 up);

    void processKeyboard(bool forward, bool backward, bool left, bool right, bool upKey, bool downKey, float deltaTime);
    void processMouse(glm::vec2 mouseDelta);

    glm::mat4 getView() const;
    glm::mat4 getProj() const;
    
    glm::vec3 getPosition() const { return m_eye; }
    glm::vec3 getFront() const { return m_front; }

    float moveSpeed = 5.0f;
    float mouseSensitivity = 0.1f;

private:
    void updateCameraVectors();

    glm::vec3 m_eye;
    glm::vec3 m_front{0.0f, 0.0f, -1.0f};
    glm::vec3 m_up{0.0f, 1.0f, 0.0f};
    glm::vec3 m_right{1.0f, 0.0f, 0.0f};
    glm::vec3 m_worldUp{0.0f, 1.0f, 0.0f};

    float m_yaw = -90.0f;
    float m_pitch = 0.0f;

    float m_fov = 45.0f;
    float m_aspect = 1.0f;
    float m_near = 0.1f;
    float m_far = 100.0f;
};

} // namespace Vista
