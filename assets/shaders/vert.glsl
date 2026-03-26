#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in vec4 inTangent; // FIXED: vec4 (w = handedness)

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec3 fragPos;
layout(location = 4) out vec4 fragTangent; // FIXED

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
    vec4 worldPos = pc.model * vec4(inPosition, 1.0);
    gl_Position = ubo.proj * ubo.view * worldPos;

    fragPos = worldPos.xyz;

    mat3 normalMatrix = transpose(inverse(mat3(pc.model)));

    fragNormal = normalize(normalMatrix * inNormal);

    fragTangent.xyz = normalize(normalMatrix * inTangent.xyz);
    fragTangent.w   = inTangent.w; // preserve handedness

    fragColor = inColor;
    fragTexCoord = inTexCoord;
}