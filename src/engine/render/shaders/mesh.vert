#version 440

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec3 v_world;

// Per-entity uniforms (shared with mesh.frag, outline.vert, outline.frag).
layout(std140, binding = 0) uniform buf {
    mat4 mvp;           // clip-space correction * projection * view * model
    mat4 model;
    mat4 normalMatrix;  // inverse transpose of model (upper 3x3)
    vec4 color;         // base color
    vec4 outline;       // rgb: outline color, w: width in pixels
    vec4 viewport;      // xy: target size in pixels
    vec4 cameraPos;     // xyz
    vec4 material;      // x: roughness, y: metallic (0..1), zw: free
};

void main()
{
    v_normal = mat3(normalMatrix) * normal;
    v_world = (model * vec4(position, 1.0)).xyz;
    gl_Position = mvp * vec4(position, 1.0);
}
