#version 440

// Shadow passes (ShadowMaps): each mesh's depth as seen from one shadowed light, drawn once
// per tile of the shadow atlases.
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

// The tile being drawn (a dynamic offset into one buffer per frame).
layout(std140, binding = 1) uniform viewBuf {
    mat4 lightViewProj; // world -> the light's clip space, corrected for the backend
};

void main()
{
    gl_Position = lightViewProj * model * vec4(position, 1.0);
}
