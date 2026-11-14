#version 440

// Environment, step 4: the split-sum BRDF lookup table, the second half of the split sum.
// For (NdotV, roughness) it stores the scale and bias applied to F0:
//     specular = prefiltered * (F0 * lut.r + lut.g)
// It doesn't depend on the environment: computed once, at startup (IblMaps).
layout(location = 0) in vec2 v_uv; // x: NdotV, y: roughness (0..1, read back with the same uv)
layout(location = 0) out vec4 fragColor;

const float PI = 3.14159265359;

vec2 Hammersley(uint i, uint N)
{
    return vec2(float(i)/float(N), float(bitfieldReverse(i)) * 2.3283064365386963e-10);
}

vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float roughness)
{
    float a = roughness * roughness;
    
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    vec3 H = vec3(0.0);
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0); 
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);
    
    vec3 sampleVec = H.x * tangent + H.y * bitangent + H.z * N;
    return normalize(sampleVec);
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

void main()
{
    float NdotV = max(v_uv.x, 1e-3);
    float roughness = v_uv.y;

    vec3 V = vec3(0.0);
    V.x = sqrt(1.0 - NdotV * NdotV);
    V.y = 0.0;
    V.z = NdotV;

    float A = 0.0;
    float B = 0.0;

    vec3 N = vec3(0.0, 0.0, 1.0);

    const uint SAMPLES = 1024u;

    for (uint i = 0; i < SAMPLES; ++i)
    {
        vec2 Xi = Hammersley(i, SAMPLES);
        vec3 H = ImportanceSampleGGX(Xi, N, roughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);
    
        float NdotL = max(L.z, 0.0);
        float NdotH = max(H.z, 0.0);
        float VdotH = max(dot(V, H), 0.0);

        if (NdotL > 0.0)
        {
            float G = GeomSmith(N, V, L, roughness * roughness / 2.0);
            float G_vis = (G * VdotH) / (NdotH * NdotV);
            float Fc = pow(1.0 - VdotH, 5.0);

            A += (1.0 - Fc) * G_vis;
            B += Fc * G_vis;
        }
    }

    A /= float(SAMPLES);
    B /= float(SAMPLES);

    fragColor = vec4(A, B, 0.0, 1.0);
}
