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

// Cook-Torrance PBR Model
void main()
{
    vec3 N = normalize(v_normal);
    vec3 V = normalize(cameraPos.xyz - v_world);

    if (dot(N, V) < 0.0) N = -N;

    vec3 L = normalize(vec3(0.45, 0.8, 0.35));
    vec3 H = normalize(V + L);

    vec3 albedo = color.rgb;
    float roughness = max(material.x, 0.045);
    float metallic = material.y;

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    float D = DistGGX(N, H, roughness * roughness);
    float G = GeomSmith(N, V, L, (roughness + 1.0) * (roughness + 1.0) / 8.0);
    vec3 F = Fresnel(max(dot(H, V), 0.0), F0);

    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    
    vec3 spec = (D * G * F) / ((4.0 * NdotV * NdotL) + 0.001);
    
    vec3 kD = (1.0 - F) * (1.0 - metallic);

    vec3 Lo = (kD * albedo / PI + spec) * 3.0 * NdotL;
    
    vec3 ambient = 0.03 * albedo;

    fragColor = vec4(Lo + ambient, 1.0);
}
