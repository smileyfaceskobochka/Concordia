#pragma once

#include <glm/glm.hpp>

namespace Vista {

class Camera {
public:
    Camera() : m_eye(0.0f, 0.0f, 2.0f), m_target(0.0f), m_up(0.0f, 1.0f, 0.0f) {}

    void setPerspective(float fovDeg, float aspect, float nearP, float farP);
    void lookAt(glm::vec3 eye, glm::vec3 target, glm::vec3 up);

    glm::mat4 getView() const;
    glm::mat4 getProj() const;
    
    glm::vec3 getPosition() const { return m_eye; }
    glm::vec3 getFront() const { return glm::normalize(m_target - m_eye); }


private:
    glm::vec3 m_eye;
    glm::vec3 m_target;
    glm::vec3 m_up;

    float m_fov = 45.0f;
    float m_aspect = 1.0f;
    float m_near = 0.1f;
    float m_far = 100.0f;
};

} // namespace Vista
