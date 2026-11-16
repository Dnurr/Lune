#version 440

// Mesh edges (Renderer: Shading::Wireframe, or over the shaded meshes): the triangles drawn
// with polygon mode Line.
layout(location = 0) in vec3 position;

// Per-entity uniforms (the same block as mesh.vert).
layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    mat4 model;
    mat4 normalMatrix;
    vec4 color;
    vec4 outline;
    vec4 viewport;   // xy: target size in pixels, z: wire brightness (1 = light lines, 0 = dark)
    vec4 cameraPos;
    vec4 material;
};

void main()
{
    gl_Position = mvp * vec4(position, 1.0);
}
