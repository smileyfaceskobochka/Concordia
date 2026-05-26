#version 450

layout(location = 0) out vec4 outColor;
layout(location = 0) in vec3 fragTexCoord;

layout(set = 0, binding = 0) uniform GlobalUBO {
    vec4 lightDir;
    vec4 viewPos;
    vec4 lightColor;
    mat4 view;
    mat4 proj;
    float exposure;
    float gamma;
} ubo;

layout(set = 0, binding = 1) uniform sampler2D skyboxHDRI;

void main() {
    vec3 dir = normalize(fragTexCoord);
    float theta = acos(clamp(dir.y, -1.0, 1.0));
    float phi = atan(dir.z, dir.x);
    float u = (phi / 6.28318530718 + 0.5);
    float v = theta / 3.14159265359;
    vec3 color = texture(skyboxHDRI, vec2(u, v)).rgb;

    // Tone mapping
    color = color / (color + vec3(1.0));

    outColor = vec4(color, 1.0);
}
