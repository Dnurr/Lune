#version 440

// Environment, step 2: diffuse irradiance. For each normal direction, the cosine-weighted
// integral of the environment over the hemisphere around it (already divided by PI, so
// mesh.frag uses it as  kD * albedo * irradiance). Runs once per face, with cube.vert.
layout(location = 0) in vec3 v_dir; // the normal N for this texel, not normalized
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform cubeBuf {
    vec4 face; // x: face, y: flip V (cube.vert), z: unused, w: environment size in pixels (one face)
};
// The environment cubemap, with a full mip chain: sampling a lower mip (textureLod)
// averages many texels at once, so fewer samples are needed without noise.
layout(binding = 1) uniform samplerCube environment;

const float PI = 3.14159265359;

void main()
{
    vec3 N = normalize(v_dir);
    
    vec3 irradiance = vec3(0.0);
    
    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
    vec3 right = normalize(cross(up, N));
    up = normalize(cross(N, right));

    float sampleDelta = 0.05;
    float nSamples = 0.0;
    float lod = log2(face.w * sampleDelta / (0.5 * PI));

    for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta)
    {
        for (float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta)
        {
            vec3 tangentSample = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            vec3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N;

            irradiance += textureLod(environment, sampleVec, lod).rgb * cos(theta) * sin(theta);
            nSamples++;
        }
    }

    irradiance = PI * irradiance * (1.0 / nSamples);

    fragColor = vec4(irradiance, 1.0);
}
