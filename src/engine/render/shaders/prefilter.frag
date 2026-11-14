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

void main()
{
    vec3 R = normalize(v_dir);
    float roughness = face.z;

    // TODO (author): importance-sample the GGX distribution around N = V = R
    // and average the environment, weighted by N.L.
    fragColor = vec4(0.0, 0.0, 0.0, 1.0);
}
