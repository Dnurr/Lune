#version 440

// Mesh edges: one flat color (linear, into the HDR scene), premultiplied alpha.
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    mat4 model;
    mat4 normalMatrix;
    vec4 color;
    vec4 outline;
    vec4 viewport;   // z: wire brightness (1 = light lines, alone; 0 = dark lines, over the surfaces)
    vec4 cameraPos;
    vec4 material;
};

void main()
{
    // Light: readable on the dark sky. Dark and half transparent: over shaded surfaces.
    vec4 light = vec4(vec3(0.55), 1.0);
    vec4 dark = vec4(vec3(0.0), 0.6);
    fragColor = mix(dark, light, viewport.z);
}
