#version 440

// Particles: one instance per particle slot (ParticleSystem in engine/render/particles.h),
// six vertices each (two triangles of a quad).

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
    vec4 soft;         // x = 1 when the scene's distance is there, y = the emitter's softness (particle.frag)
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
    corner = vec2(corner.x * cos(extra.y) - corner.y * sin(extra.y), corner.x * sin(extra.y) + corner.y * cos(extra.y));
    float halfSize = 0.5 * extra.x;
    vec3 worldPos = position.xyz + (cameraRight.xyz * corner.x + cameraUp.xyz * corner.y) * halfSize;
    
    gl_Position = viewProj * vec4(worldPos, 1.0);
}
