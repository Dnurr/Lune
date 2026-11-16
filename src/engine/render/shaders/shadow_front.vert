#version 440

// Front-depth pass (ShadowMaps): like shadow.vert, for the front-depth regions of the
// translucency map: each caster's surface nearest to the light, as a linear depth.
layout(location = 0) in vec3 position;

layout(location = 0) flat out vec3 v_view;

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

// The tile being drawn (a dynamic offset into one buffer per frame).
layout(std140, binding = 1) uniform viewBuf {
    mat4 lightViewProj; // world -> the light's clip space, corrected for the backend
    vec4 lightView;     // x: 1 for a point light's face, y: near, z: far
};

void main()
{
    v_view = lightView.xyz;
    gl_Position = lightViewProj * model * vec4(position, 1.0);
}
