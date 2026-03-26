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

layout(set = 0, binding = 1) uniform samplerCube skybox;

void main() {
    vec3 color = texture(skybox, fragTexCoord).rgb;
    
    // Tone mapping (hardware sRGB handles gamma)
    color = color / (color + vec3(1.0));
    
    outColor = vec4(color, 1.0);
}
