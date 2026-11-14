#version 440

// Environment, step 4: the split-sum BRDF lookup table, the second half of the split sum.
// For (NdotV, roughness) it stores the scale and bias applied to F0:
//     specular = prefiltered * (F0 * lut.r + lut.g)
// It doesn't depend on the environment: computed once, at startup (IblMaps).
layout(location = 0) in vec2 v_uv; // x: NdotV, y: roughness (0..1, read back with the same uv)
layout(location = 0) out vec4 fragColor;

const float PI = 3.14159265359;

void main()
{
    float NdotV = v_uv.x;
    float roughness = v_uv.y;

    // TODO (author): integrate the BRDF with GGX importance sampling, write (scale, bias, 0, 1).
    // Note: the geometry term for IBL uses k = a / 2 instead of (r + 1)^2 / 8.
    fragColor = vec4(0.0, 0.0, 0.0, 1.0);
}
