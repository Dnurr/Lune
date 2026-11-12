#version 440

// Same as fullscreen.vert, with V flipped: used by backends where "up" in clip space
// and "up" in textures disagree (Direct3D, Metal), so texture (0,0) is the top-left.
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec2 v_ndc;

void main()
{
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    v_uv = vec2(p.x, 1.0 - p.y);
    v_ndc = p * 2.0 - 1.0;
    gl_Position = vec4(v_ndc, 0.0, 1.0);
}
