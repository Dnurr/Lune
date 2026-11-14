#version 440

// Fullscreen triangle for the environment background: gives each fragment the world
// direction of its view ray (normalize v_dir first).
layout(location = 0) out vec3 v_dir;

layout(std140, binding = 0) uniform backgroundBuf {
    mat4 invViewProj; // inverse of proj * view (OpenGL clip-space convention)
    vec4 cameraPos;   // xyz
    vec4 params;      // x: NDC y sign (1 or -1)
};

void main()
{
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vec2 ndc = p * 2.0 - 1.0;
    // A point on the far plane, still homogeneous: (far - eye) scaled by w is linear in
    // screen space, so it interpolates exactly and normalizes to the ray direction.
    vec4 far = invViewProj * vec4(ndc.x, ndc.y * params.x, 1.0, 1.0);
    v_dir = far.xyz - cameraPos.xyz * far.w;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
