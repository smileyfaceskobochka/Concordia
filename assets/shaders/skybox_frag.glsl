#version 450

layout(location = 0) in vec3 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1) uniform samplerCube skybox;

void main() {
    // Diagnostic gradient
    outColor = vec4(normalize(fragTexCoord) * 0.5 + 0.5, 1.0);
}
