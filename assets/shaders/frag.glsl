#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragPos;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform GlobalUBO {
    vec4 lightDir;
    vec4 viewPos;
    vec4 lightColor;
    mat4 view;
    mat4 proj;
    float exposure;
    float gamma;
} ubo;

layout(set = 1, binding = 0) uniform sampler2D texSampler;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 baseColor;
    float roughness;
    float metallic;
} pc;

void main() {
    // Ambient
    float ambientStrength = 0.1;
    vec3 ambient = ambientStrength * ubo.lightColor.xyz;
    
    // Diffuse
    vec3 norm = normalize(fragNormal);
    vec3 lDir = normalize(-ubo.lightDir.xyz);
    float diff = max(dot(norm, lDir), 0.0);
    vec3 diffuse = diff * ubo.lightColor.xyz;
    
    // Specular (Roughness based)
    float shininess = (1.0 - pc.roughness) * 128.0;
    vec3 viewDir = normalize(ubo.viewPos.xyz - fragPos);
    vec3 halfwayDir = normalize(lDir + viewDir);
    float spec = pow(max(dot(norm, halfwayDir), 0.0), max(shininess, 1.0));
    vec3 specular = 0.5 * spec * ubo.lightColor.xyz;
    
    vec4 texColor = texture(texSampler, fragTexCoord);
    vec3 lighting = (ambient + diffuse + specular);
    
    outColor = texColor * pc.baseColor * vec4(lighting, 1.0);
}
