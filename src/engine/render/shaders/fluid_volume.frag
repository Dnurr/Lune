#version 440

// A Fluid Volume seen through one pixel: ray marching from the camera (or where the ray enters
// the box) to where it leaves it, or to the scene's surface if that's nearer. Author: to be
// written.
//
// Drawn over the scene (dual-source blending: rgb = light scattered and emitted toward the
// camera, plus `transmittance` = how much of what's behind gets through, per channel; a = how
// much is hidden, on average), into the HDR target, after the meshes and
// before the particles, without depth testing: the march stops at sceneDistance itself.
//
// Until then: nothing (fully transparent).

layout(location = 0) in vec3 v_world; // where the ray leaves the box (a back face), world space
layout(location = 0) out vec4 fragColor;
// Dual-source blending: the second output multiplies what's behind, channel by channel
// (result = behind * transmittance.rgb + fragColor.rgb). Write T here (vec4(T, 1.0) or
// vec4(vec3(T), 1.0)); fragColor.a is still 1 - T (an average if T is per channel).
layout(location = 0, index = 1) out vec4 transmittance;

// One per Fluid Volume (FluidSystem::prepareDraw()).
layout(std140, binding = 0) uniform VolumeBuf {
    mat4 viewProj;       // world -> clip, for this backend
    mat4 model;          // box space (the cube -0.5..0.5) -> world
    mat4 worldToBox;     // world -> box space; texture coordinates = box position + 0.5
    vec4 eye;            // xyz: the camera, world space
    vec4 lightDir;       // xyz: the direction the main light shines along (world, unit); w = 1 with a light
    vec4 lightColor;     // rgb: its radiance (linear, intensity included)
    vec4 look;           // x = density (extinction per unit of smoke per world unit), y = samples,
                         // z = emission (brightness per unit of temperature),
                         // w = 1 when sceneDistance holds the scene (0: a stand-in)
    vec4 color;          // rgb: scattering color (linear)
    vec4 emissionColor;  // rgb: fire color (linear)
    vec4 grid;           // xyz = voxels, w = voxel size (world units)
    vec4 params;         // xy = viewport size in pixels, z = scene time (seconds)
    vec4 extinction;     // rgb: extinction per channel, relative to look.x (linear; white = grey)
};
layout(binding = 1) uniform sampler3D density;        // x = smoke, y = temperature; linear, clamped
layout(binding = 2) uniform sampler2D sceneDistance;  // view depth of the scene per pixel, like
                                                      // particle.frag's (texelFetch at gl_FragCoord)

void main()
{
    // TODO (Author): the ray from eye.xyz toward v_world, in box space (worldToBox); where it
    // enters and leaves the box; march look.y steps, sampling density at box position + 0.5:
    // extinction (look.x), light scattered (color, lightColor, a march toward the light for the
    // self-shadow), emission (emissionColor x temperature x look.z); stop at the scene.
    fragColor = vec4(0.0);
    transmittance = vec4(1.0);
}
