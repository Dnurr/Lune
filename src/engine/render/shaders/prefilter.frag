#version 440

// Environment, step 3: prefiltered specular. Each mip level holds the environment blurred
// by the GGX lobe of one roughness (level / (levels - 1)): the first half of the split-sum
// approximation. Runs once per face and per mip, with cube.vert.
layout(location = 0) in vec3 v_dir; // the reflection direction R for this texel, not normalized
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform cubeBuf {
    vec4 face; // x: face, y: flip V (cube.vert), z: roughness of this mip, w: environment size in pixels
};
// The environment cubemap, with a full mip chain (see irradiance.frag).
layout(binding = 1) uniform samplerCube environment;

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

float DistGGX(vec3 N, vec3 H, float a)
{
    float a2 = a*a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;

    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return a2 / denom;
}

void main()
{
    vec3 R = normalize(v_dir);
    float roughness = face.z;

    if (roughness == 0.0) { fragColor = vec4(textureLod(environment, R, 0.0).rgb, 1.0); return; }

    const uint SAMPLES = 1024;
    float totalWeight = 0.0;

    vec3 color = vec3(0.0);
    for (uint i = 0; i < SAMPLES; ++i)
    {
        vec2 Xi = Hammersley(i, SAMPLES);
        vec3 H = ImportanceSampleGGX(Xi, R, roughness);
        vec3 L = normalize(2.0 * dot(R, H) * H - R);

        float NdotL = max(dot(R, L), 0.0);
        if (NdotL > 0.0)
        {
            float NdotH = max(dot(R, H), 0.0);

            float D = DistGGX(R, H, roughness * roughness);
            float pdf = D / 4.0;
            
            float saTexel = 4.0 * PI / (6.0 * face.w * face.w);
            float saSample = 1.0 / (float(SAMPLES) * pdf + 0.0001);

            float lod = 0.5 * log2(saSample / saTexel);

            color += textureLod(environment, L, lod).rgb * NdotL;
            totalWeight += NdotL;
        }
    }

    color = color / totalWeight;

    fragColor = vec4(color, 1.0);
}
