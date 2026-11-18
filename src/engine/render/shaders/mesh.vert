#version 440

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;
layout(location = 3) in vec4 tangent; // xyz: along +u, w: handedness (see MeshVertex)

layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec3 v_world;
layout(location = 2) out vec2 v_uv;
layout(location = 3) out vec4 v_tangent; // world space; w: bitangent = cross(normal, tangent.xyz) * w

// Per-entity uniforms (shared with mesh.frag, outline.vert, outline.frag).
layout(std140, binding = 0) uniform buf {
    mat4 mvp;           // clip-space correction * projection * view * model
    mat4 model;
    mat4 normalMatrix;  // inverse transpose of model (upper 3x3)
    vec4 color;         // base color
    vec4 outline;       // rgb: outline color, w: width in pixels
    vec4 viewport;      // xy: target size in pixels, w: scene time in seconds (Timeline)
    vec4 cameraPos;     // xyz
    vec4 material;      // x: roughness, y: metallic (0..1), z: alpha cutoff, w: alpha mode (see mesh.frag)
};

void main()
{
    v_normal = mat3(normalMatrix) * normal;
    v_world = (model * vec4(position, 1.0)).xyz;
    v_uv = uv;
    // A mirroring model matrix flips the handedness.
    v_tangent = vec4(mat3(model) * tangent.xyz, tangent.w * (determinant(mat3(model)) < 0.0 ? -1.0 : 1.0));
    gl_Position = mvp * vec4(position, 1.0);
}
