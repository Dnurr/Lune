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

    // TODO (author): integrate the environment over the hemisphere around N
    // (a tangent frame around N, then a loop over the hemisphere's angles).
    fragColor = vec4(0.0, 0.0, 0.0, 1.0);
}
