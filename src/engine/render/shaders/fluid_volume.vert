#version 440

// A Fluid Volume's box: 36 vertices of the cube -0.5..0.5 in box space (no vertex buffer), drawn
// with front faces culled so that the inside of the box is drawn too when the camera is in it.
// fluid_volume.frag marches through the volume along each pixel's ray.

layout(location = 0) out vec3 v_world; // the box's surface (back faces), world space

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

void main()
{
    // Two triangles per face, counter-clockwise seen from outside.
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
    gl_Position = viewProj * world;
}
