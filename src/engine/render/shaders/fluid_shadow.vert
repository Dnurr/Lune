#version 440

// A Fluid Volume's box seen from a light, for its shadow (FluidSystem::drawShadows()): the 36
// vertices of fluid_volume.vert, into one tile of the shadow maps' translucency map. The
// pipeline culls back faces: what's drawn is the side of the box facing the light, where the
// light enters it. fluid_shadow.frag marches through the volume from there.

layout(location = 0) out vec3 v_world; // where the light enters the box (a face toward it), world space

// One per Fluid Volume and tile (FluidSystem::prepareShadows(), a dynamic offset per tile).
layout(std140, binding = 0) uniform ShadowBuf {
    mat4 lightViewProj; // world -> the light's clip space (this tile), for this backend
    mat4 model;         // box space (the cube -0.5..0.5) -> world
    mat4 worldToBox;    // world -> box space; texture coordinates = box position + 0.5
    vec4 light;         // directional (w = 0): xyz = where it shines (world, unit);
                        // point (w = 1): xyz = its position (world)
    vec4 view;          // x = 1 for a point light's face, y = its near plane, z = its far plane
    vec4 look;          // x = density (extinction per unit of smoke per world unit), y = samples
    vec4 extinction;    // rgb: extinction per channel, relative to look.x (linear; white = grey)
    vec4 detail;        // as fluid_volume.frag's: x = amount, y = rest field A's weight, z = 1 with rest fields
    vec4 detailFrequency; // xyz: noise cycles across the box per axis
};

void main()
{
    // Two triangles per face, counter-clockwise seen from outside (as in fluid_volume.vert).
    const vec3 corners[8] = vec3[](vec3(-0.5, -0.5, -0.5), vec3(0.5, -0.5, -0.5), vec3(0.5, 0.5, -0.5),
                                   vec3(-0.5, 0.5, -0.5), vec3(-0.5, -0.5, 0.5), vec3(0.5, -0.5, 0.5),
                                   vec3(0.5, 0.5, 0.5), vec3(-0.5, 0.5, 0.5));
    const int faces[36] = int[](0, 3, 2, 0, 2, 1,  // -Z
                                4, 5, 6, 4, 6, 7,  // +Z
                                0, 4, 7, 0, 7, 3,  // -X
                                1, 2, 6, 1, 6, 5,  // +X
                                0, 1, 5, 0, 5, 4,  // -Y
                                3, 7, 6, 3, 6, 2); // +Y
    vec4 world = model * vec4(corners[faces[gl_VertexIndex]], 1.0);
    v_world = world.xyz;
    gl_Position = lightViewProj * world;
}
