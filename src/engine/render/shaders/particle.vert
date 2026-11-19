#version 440

// Particles: one instance per particle slot (ParticleSystem in engine/render/particles.h),
// six vertices each (two triangles of a quad). Author: to be written.
//
// Until then: a dot of a few pixels at each live particle, in its color.

// The Particle of particles.comp, straight from the simulation's buffer.
layout(location = 0) in vec4 position; // xyz = world position, w = age (seconds)
layout(location = 1) in vec4 velocity; // xyz = world velocity, w = lifetime (seconds)
layout(location = 2) in vec4 color;    // linear RGB (HDR), a = opacity
layout(location = 3) in vec4 extra;    // x = size (world units), y = rotation, z = random 0..1

layout(std140, binding = 0) uniform DrawBuf {
    mat4 viewProj;     // world -> clip, for this backend
    vec4 cameraRight;  // xyz: the camera's right axis in world space
    vec4 cameraUp;     // xyz: its up axis
    vec4 cameraPos;    // xyz: the eye
    vec4 params;       // xy = viewport size in pixels, z = scene time (seconds)
};

layout(location = 0) out vec2 v_corner; // -1..1 across the quad
layout(location = 1) out vec4 v_color;

void main()
{
    const vec2 corners[6] = vec2[](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
                                   vec2(-1.0, -1.0), vec2(1.0, 1.0), vec2(-1.0, 1.0));
    vec2 corner = corners[gl_VertexIndex];
    v_corner = corner;
    v_color = color;
    if (position.w >= velocity.w) { // dead: outside the clip volume, nothing drawn
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }
    // TODO (Author): a quad facing the camera, extra.x wide in world units (cameraRight,
    // cameraUp), turned by extra.y; or stretched along the velocity.
    vec4 clip = viewProj * vec4(position.xyz, 1.0);
    clip.xy += corner * 3.0 / params.xy * clip.w; // stub: 3 pixels around the particle
    gl_Position = clip;
}
