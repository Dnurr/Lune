#version 440

// Shadow pass: each mesh's depth as seen from the shadowed directional light.
layout(location = 0) in vec3 position;

// Per-entity uniforms (the same block as mesh.vert; only `model` is used here).
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

// Shadow uniforms (the same block as mesh.frag, binding 6 there).
layout(std140, binding = 1) uniform shadowBuf {
    mat4 shadowMatrix;
    mat4 lightViewProj;
    vec4 shadowParams;
};

void main()
{
    gl_Position = lightViewProj * model * vec4(position, 1.0);
}
