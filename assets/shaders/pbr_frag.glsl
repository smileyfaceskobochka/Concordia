#version 450
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
    vec4 baseColor;
    float roughness;
    float metallic;
    uint albedoIdx;
    uint normalIdx;
    uint mrIdx;
    uint aoIdx;
    uint emissiveIdx;
    uint debugMode;
} pc;

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

void main() {

    // --- TEXTURES ---

    vec4 albedoTex = texture(globalTextures[nonuniformEXT(pc.albedoIdx)], fragTexCoord);
    vec3 albedo = albedoTex.rgb * pc.baseColor.rgb;

    vec2 mr = texture(globalTextures[nonuniformEXT(pc.mrIdx)], fragTexCoord).gb;
    float roughness = clamp(mr.x * pc.roughness, 0.04, 1.0);
    float metallic  = clamp(mr.y * pc.metallic,  0.0, 1.0);

    float ao = texture(globalTextures[nonuniformEXT(pc.aoIdx)], fragTexCoord).r;
    vec3 emissive = texture(globalTextures[nonuniformEXT(pc.emissiveIdx)], fragTexCoord).rgb;

    // --- NORMALS ---

    vec3 N = normalize(fragNormal);

    // Guard: only apply normal map if TBN is well-formed
    // (T parallel to N produces degenerate TBN after Gram-Schmidt)
    vec3 Traw = normalize(fragTangent.xyz);
    float TdotN = dot(Traw, N);
    if (abs(TdotN) < 0.9999) {
        vec3 T = normalize(Traw - TdotN * N);
        vec3 B = normalize(cross(N, T)) * fragTangent.w;
        mat3 TBN = mat3(T, B, N);
        vec3 tangentNormal = texture(globalTextures[nonuniformEXT(pc.normalIdx)], fragTexCoord).xyz * 2.0 - 1.0;
        N = normalize(TBN * tangentNormal);
    }


    // --- VIEW ---

    vec3 V = normalize(ubo.viewPos.xyz - fragPos);
    vec3 R = reflect(-V, N);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // --- DIRECT LIGHT ---

    vec3 L = normalize(-ubo.lightDir.xyz);
    vec3 H = normalize(V + L);
    vec3 radiance = ubo.lightColor.rgb;

    float NDF = DistributionGGX(N, H, roughness);
    float G   = GeometrySmith(N, V, L, roughness);
    vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 spec = (NDF * G * F) /
        max(4.0 * max(dot(N, V), 0.0) *
                 max(dot(N, L), 0.0), 1e-4);

    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

    float NdotL = max(dot(N, L), 0.0);
    vec3 Lo = (kD * albedo / PI + spec) * radiance * NdotL;

    // --- DEBUG: visualize material values ---
    if (pc.debugMode == 1) { outColor = vec4(vec3(metallic), 1.0); return; }
    if (pc.debugMode == 2) { outColor = vec4(vec3(roughness), 1.0); return; }
    if (pc.debugMode == 3) { outColor = vec4(N * 0.5 + 0.5, 1.0); return; }
    if (pc.debugMode == 4) { outColor = vec4(fragColor, 1.0); return; }
 
    // --- AMBIENT (IBL) ---
    vec3 F_ibl = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);

    // Diffuse IBL: sample irradiance cubemap along the normal
    vec3 irradiance = texture(irradianceMap, N).rgb;
    vec3 diffuse = irradiance * albedo;

    // Specular IBL: split-sum approximation
    const float MAX_REFLECTION_LOD = 4.0;
    vec3 prefilteredColor = textureLod(prefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf = texture(brdfLUT, vec2(roughness, max(dot(N, V), 0.0))).rg;
    vec3 specular = prefilteredColor * (F_ibl * brdf.x + brdf.y);

    vec3 ambient = (kD * diffuse + specular) * ao;

    // --- FINAL ---

    vec3 color = ambient + Lo + emissive;

    // tone mapping
    color = color / (color + vec3(1.0));

    outColor = vec4(color, albedoTex.a);
}