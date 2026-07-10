#version 450
#define LIGHT_STAGE_OVERRIDE 1

layout(std140, set = 2, binding = 0) uniform MaterialParams {
    vec4 baseColor;
    float roughness;
    float metallic;
    uint albedoMap_idx;
    uint normalMap_idx;
    uint metallicRoughnessMap_idx;
    uint aoMap_idx;
    uint emissiveMap_idx;
};

#define albedoMap globalTextures[nonuniformEXT(albedoMap_idx)]
#define normalMap globalTextures[nonuniformEXT(normalMap_idx)]
#define metallicRoughnessMap globalTextures[nonuniformEXT(metallicRoughnessMap_idx)]
#define aoMap globalTextures[nonuniformEXT(aoMap_idx)]
#define emissiveMap globalTextures[nonuniformEXT(emissiveMap_idx)]

#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragPos;
layout(location = 4) in vec4 fragTangent;

layout(location = 0) out vec4 outColor;

layout(std140, set = 0, binding = 0) uniform GlobalUBO {
    vec4 lightDir;
    vec4 viewPos;
    vec4 lightColor;
    mat4 view;
    mat4 proj;
    float exposure;
    float gamma;
} ubo;

layout(set = 0, binding = 1) uniform samplerCube skybox;
layout(set = 0, binding = 2) uniform samplerCube irradianceMap;
layout(set = 0, binding = 3) uniform samplerCube prefilterMap;
layout(set = 0, binding = 4) uniform sampler2D brdfLUT;

layout(set = 1, binding = 0) uniform sampler2D globalTextures[];

layout(push_constant) uniform PushConstants {
    mat4 model;
    uint debugMode;
} pc;

// Custom dynamic material uniforms
// [Stripped dynamic uniform] uniform vec4 baseColor;
// [Stripped dynamic uniform] uniform float roughness;
// [Stripped dynamic uniform] uniform float metallic;
// [Stripped dynamic uniform] uniform sampler2D albedoMap;
// [Stripped dynamic uniform] uniform sampler2D normalMap;
// [Stripped dynamic uniform] uniform sampler2D metallicRoughnessMap;
// [Stripped dynamic uniform] uniform sampler2D aoMap;
// [Stripped dynamic uniform] uniform sampler2D emissiveMap;

const float PI = 3.14159265359;

// ---------------- PBR ----------------

float DistributionGGX(vec3 N, vec3 H, float r) {
    float a = r * r;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float d = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-7);
}

float GeometrySchlickGGX(float NdotV, float r) {
    float a = r * r;
    float k = ((a + 1.0) * (a + 1.0)) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float r) {
    return GeometrySchlickGGX(max(dot(N, V), 0.0), r) *
           GeometrySchlickGGX(max(dot(N, L), 0.0), r);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float r) {
    return F0 + (max(vec3(1.0 - r), F0) - F0) *
                 pow(1.0 - cosTheta, 5.0);
}

// ---------------- MAIN ----------------

// Global material properties for custom light processors
vec3 ALBEDO;
float METALLIC;
float ROUGHNESS;
vec3 F0;

#ifndef LIGHT_STAGE_OVERRIDE
void light(
    inout vec3 diffuse, 
    inout vec3 specular, 
    vec3 light_dir, 
    vec3 light_color, 
    float attenuation, 
    vec3 normal, 
    vec3 view, 
    float roughness
) {
    vec3 H = normalize(view + light_dir);
    float NDF = DistributionGGX(normal, H, roughness);
    float G   = GeometrySmith(normal, view, light_dir, roughness);
    vec3 F    = fresnelSchlick(max(dot(H, view), 0.0), F0);

    vec3 spec = (NDF * G * F) /
        max(4.0 * max(dot(normal, view), 0.0) *
                 max(dot(normal, light_dir), 0.0), 1e-4);

    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - METALLIC);

    float NdotL = max(dot(normal, light_dir), 0.0);
    diffuse += (kD * ALBEDO / PI) * light_color * attenuation * NdotL;
    specular += spec * light_color * attenuation * NdotL;
}
#endif

void light(
    inout vec3 diffuse, 
    inout vec3 specular, 
    vec3 light_dir, 
    vec3 light_color, 
    float attenuation, 
    vec3 normal, 
    vec3 view, 
    float roughness
) {
    float NdotL = max(dot(normal, light_dir), 0.0);
    
    // Stepped toon diffuse shading bands
    float toonDiff = 0.0;
    if (NdotL > 0.6) {
        toonDiff = 1.0;
    } else if (NdotL > 0.25) {
        toonDiff = 0.5;
    } else {
        toonDiff = 0.1;
    }

    // Accumulate diffuse lighting using step value, zero specular contribution
    diffuse += (ALBEDO / 3.14159265359) * light_color * attenuation * toonDiff;
    specular += vec3(0.0);
}


void main() {
    // --- TEXTURES ---
    vec4 albedoTex = texture(albedoMap, fragTexCoord);
    vec3 albedo = albedoTex.rgb * baseColor.rgb;

    vec2 mr = texture(metallicRoughnessMap, fragTexCoord).gb;
    float r = clamp(mr.x * roughness, 0.04, 1.0);
    float m  = clamp(mr.y * metallic,  0.0, 1.0);

    float ao = texture(aoMap, fragTexCoord).r;
    vec3 emissive = texture(emissiveMap, fragTexCoord).rgb;

    // --- NORMALS ---
    vec3 N = normalize(fragNormal);

    // Guard: only apply normal map if TBN is well-formed
    vec3 Traw = normalize(fragTangent.xyz);
    float TdotN = dot(Traw, N);
    if (abs(TdotN) < 0.9999) {
        vec3 T = normalize(Traw - TdotN * N);
        vec3 B = normalize(cross(N, T)) * fragTangent.w;
        mat3 TBN = mat3(T, B, N);
        vec3 tangentNormal = texture(normalMap, fragTexCoord).xyz * 2.0 - 1.0;
        N = normalize(TBN * tangentNormal);
    }

    // --- VIEW ---
    vec3 V = normalize(ubo.viewPos.xyz - fragPos);

    if (!gl_FrontFacing) {
        N = -N;
    }

    vec3 R = reflect(-V, N);

    // Initialize global properties for lighting stage
    ALBEDO = albedo;
    METALLIC = m;
    ROUGHNESS = r;
    F0 = mix(vec3(0.04), ALBEDO, METALLIC);

    // --- DIRECT LIGHT ---
    vec3 L = normalize(-ubo.lightDir.xyz);
    vec3 radiance = ubo.lightColor.rgb;

    vec3 diffuseLight = vec3(0.0);
    vec3 specularLight = vec3(0.0);
    
    // Call lighting function (can be overridden by user .light stage)
    light(diffuseLight, specularLight, L, radiance, 1.0, N, V, ROUGHNESS);

    vec3 Lo = diffuseLight + specularLight;

    // --- DEBUG: visualize material values ---
    float finalAlpha = albedoTex.a * baseColor.a;
    if (pc.debugMode == 1) { outColor = vec4(vec3(m), finalAlpha); return; }
    if (pc.debugMode == 2) { outColor = vec4(vec3(r), finalAlpha); return; }
    if (pc.debugMode == 3) { outColor = vec4(N * 0.5 + 0.5, finalAlpha); return; }
    if (pc.debugMode == 4) { outColor = vec4(fragColor, finalAlpha); return; }

    // --- AMBIENT (IBL) ---
    vec3 F_ibl = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, r);
    vec3 kD = (vec3(1.0) - F_ibl) * (1.0 - m);

    // Diffuse IBL
    vec3 irradiance = texture(irradianceMap, N).rgb;
    vec3 diffuse = irradiance * albedo;

    // Specular IBL
    const float MAX_REFLECTION_LOD = 4.0;
    vec3 prefilteredColor = textureLod(prefilterMap, R, r * MAX_REFLECTION_LOD).rgb;
    vec2 brdfVal = texture(brdfLUT, vec2(r, max(dot(N, V), 0.0))).rg;
    vec3 specular = prefilteredColor * (F_ibl * brdfVal.x + brdfVal.y);

    vec3 ambient = (kD * diffuse + specular) * ao;

    // --- FINAL ---
    vec3 color = ambient + Lo + emissive;

    // tone mapping
    color = color / (color + vec3(1.0));

    outColor = vec4(color, albedoTex.a * baseColor.a);
}
