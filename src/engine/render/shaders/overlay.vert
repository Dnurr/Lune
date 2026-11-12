#version 440

// 2D overlay (gizmo): triangles in widget pixels, origin top-left.
layout(location = 0) in vec2 position;
layout(location = 1) in vec4 color;
layout(location = 0) out vec4 v_color;

layout(std140, binding = 0) uniform buf {
    vec4 params; // xy: size in the same pixels as `position`, z: 1 if clip-space y is up
};

void main()
{
    vec2 ndc = vec2(position.x / params.x * 2.0 - 1.0, 1.0 - position.y / params.y * 2.0);
    if (params.z < 0.5)
        ndc.y = -ndc.y;
    v_color = color;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
