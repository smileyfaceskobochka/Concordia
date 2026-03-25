#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec3 fragTexCoord;

layout(set = 0, binding = 0) uniform GlobalUBO {
    vec4 lightDir;
    vec4 viewPos;
    vec4 lightColor;
    mat4 view;
    mat4 proj;
    float exposure;
    float gamma;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 baseColor;
    float roughness;
    float metallic;
} pc;

void main() {
    fragTexCoord = inPosition;
    // Remove translation from view matrix
    mat4 viewNoTransform = mat4(mat3(ubo.view));
    vec4 pos = ubo.proj * viewNoTransform * pc.model * vec4(inPosition, 1.0);
    // Skybox depth trick
    gl_Position = pos.xyww;
}
