#version 440

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec3 v_world;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    mat4 model;
    mat4 normalMatrix;
    vec4 color;
    vec4 outline;
    vec4 viewport;
    vec4 cameraPos;
    vec4 material;  // x: roughness, y: metallic (0..1)
};

// Scene lights, filled by the Renderer once per frame (binding 1, shared by every mesh).
// A scene without enabled lights gets one default sun, so lightCount.x is never 0.
#define MAX_LIGHTS 8 // Renderer::MaxLights
struct Light
{
    vec4 position;  // xyz: world position (point lights), w: type (0 directional, 1 point)
    vec4 direction; // xyz: normalized direction the light travels (directional lights)
    vec4 color;     // rgb: linear color, w: intensity -> radiance = color.rgb * color.w
};
layout(std140, binding = 1) uniform lightsBuf {
    ivec4 lightCount; // x: number of lights in use
    Light lights[MAX_LIGHTS];
};

// Image-based lighting (IblMaps), shared by every mesh. Without an Environment entity
// the maps hold a sky/ground gradient, so they're always valid.
layout(std140, binding = 2) uniform environmentBuf {
    mat4 envRotation; // world direction -> environment direction: mat3(envRotation) * dir
    vec4 envParams;   // x: intensity, y: prefiltered max mip (levels - 1), z: background blur, w: background shown
};
// Diffuse irradiance per normal direction (already divided by PI): kD * albedo * irradiance.
layout(binding = 3) uniform samplerCube irradianceMap;
// Specular reflection, blurred per roughness: mip = roughness * envParams.y (textureLod).
layout(binding = 4) uniform samplerCube prefilteredMap;
// Split-sum BRDF terms: texture(brdfLut, vec2(NdotV, roughness)).rg = (scale, bias) for F0.
layout(binding = 5) uniform sampler2D brdfLut;

// Direction in the world -> direction to sample the environment maps with.
vec3 toEnvironment(vec3 dir)
{
    return mat3(envRotation) * dir;
}

const float PI = 3.14159265359;

float DistGGX(vec3 N, vec3 H, float a)
{
    float a2 = a*a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;

    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return a2 / denom;
}

float GeomShlickGGX(float NdotV, float k)
{
    float denom = NdotV * (1.0 - k) + k;
    
    return NdotV / denom;
}

float GeomSmith(vec3 N, vec3 V, vec3 L, float k)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1 = GeomShlickGGX(NdotV, k);
    float ggx2 = GeomShlickGGX(NdotL, k);

    return ggx1 * ggx2;
}

vec3 Fresnel(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 FresnelRoughness(float NdotV, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - NdotV, 5.0);
}

// Cook-Torrance PBR Model
void main()
{
    vec3 N = normalize(v_normal);
    vec3 V = normalize(cameraPos.xyz - v_world);

    if (dot(N, V) < 0.0) N = -N;

    vec3 albedo = color.rgb;
    float roughness = max(material.x, 0.045);
    float metallic = material.y;

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    float NdotV = max(dot(N, V), 0.0);
    
    vec3 kS = FresnelRoughness(NdotV, F0, roughness);

    vec3 R = reflect(-V, N);
    vec3 prefiltered = textureLod(prefilteredMap, toEnvironment(R), roughness * envParams.y).rgb;
    vec3 lut = texture(brdfLut, vec2(NdotV, roughness)).rgb;
    vec3 ambientSpec = prefiltered * (kS * lut.r + lut.g) * envParams.x;

    vec3 final = vec3(0.0);

    for (int i = 0; i < lightCount.x; i++)
    {
        vec3 radiance = lights[i].color.rgb * lights[i].color.w;
        vec3 L;
        if (lights[i].position.w > 0.5) // point
        {
            vec3 toLight = lights[i].position.xyz - v_world;
            float dist2 = max(dot(toLight, toLight), 1e-4);
            L = toLight * inversesqrt(dist2);
            radiance /= dist2;
        }
        else L = -lights[i].direction.xyz;
        
        vec3 H = normalize(V + L);

        float D = DistGGX(N, H, roughness * roughness);
        float G = GeomSmith(N, V, L, (roughness + 1.0) * (roughness + 1.0) / 8.0);
        vec3 F = Fresnel(max(dot(H, V), 0.0), F0);

        float NdotL = max(dot(N, L), 0.0);
        
        vec3 spec = (D * G * F) / ((4.0 * NdotV * NdotL) + 0.001);
        
        vec3 kD = (1.0 - F) * (1.0 - metallic);

        final += (kD * albedo / PI + spec) * radiance * NdotL;
    }

    vec3 kD = (1.0 - kS) * (1.0 - metallic);
    vec3 irradiance = texture(irradianceMap, toEnvironment(N)).rgb;
    vec3 ambient = kD * albedo * irradiance * envParams.x;

    fragColor = vec4(final + ambient + ambientSpec, 1.0);
}