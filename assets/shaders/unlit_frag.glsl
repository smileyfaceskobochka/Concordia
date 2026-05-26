#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragPos;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D globalTextures[];

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 baseColor;
    float roughness;
    float metallic;
    uint albedoIdx;
    uint normalIdx;
    uint mrIdx;
    uint aoIdx;
    uint emissiveIdx;
} pc;

void main() {
    outColor = texture(globalTextures[nonuniformEXT(pc.albedoIdx)], fragTexCoord) * pc.baseColor;
}
