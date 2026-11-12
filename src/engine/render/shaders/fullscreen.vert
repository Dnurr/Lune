#version 440

// Fullscreen triangle generated from gl_VertexIndex (no vertex buffer).
// v_uv: texture coordinates, v_ndc: clip-space xy of the pixel.
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec2 v_ndc;

void main()
{
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2); // (0,0) (2,0) (0,2)
    v_uv = p;
    v_ndc = p * 2.0 - 1.0;
    gl_Position = vec4(v_ndc, 0.0, 1.0);
}
