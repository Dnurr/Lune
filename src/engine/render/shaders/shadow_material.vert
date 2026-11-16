#version 440

// Shadow passes of node materials whose alpha matters (clip and blend, see ShadowMaps):
// like shadow.vert, but with everything mesh.vert gives the material code (its fragment
// shader is mesh.frag's declarations + the material + shadow_material.glsl).
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;
layout(location = 3) in vec4 tangent;

layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec3 v_world;
layout(location = 2) out vec2 v_uv;
layout(location = 3) out vec4 v_tangent;

// Per-entity uniforms (the same block as mesh.vert).
layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    mat4 model;
    mat4 normalMatrix;
    vec4 color;
    vec4 outline;
    vec4 viewport;
    vec4 cameraPos;
    vec4 material;
};

// The tile being drawn (a dynamic offset into one buffer per frame). Binding 20: mesh.frag
// uses 0..19. The same block is in shadow_material.glsl.
layout(std140, binding = 20) uniform shadowViewBuf {
    mat4 shadowViewProj; // world -> the light's clip space, corrected for the backend
    vec4 shadowView;     // x: 1 for a point light's face, y: near, z: far
};

void main()
{
    v_normal = mat3(normalMatrix) * normal;
    v_world = (model * vec4(position, 1.0)).xyz;
    v_uv = uv;
    v_tangent = vec4(mat3(model) * tangent.xyz, tangent.w * (determinant(mat3(model)) < 0.0 ? -1.0 : 1.0));
    gl_Position = shadowViewProj * vec4(v_world, 1.0);
}
