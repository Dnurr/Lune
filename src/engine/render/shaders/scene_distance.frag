#version 440

// The scene's distance from the camera, for soft particles: each pixel of the scene's depth
// buffer turned into view depth (world units along the view direction), the same measure
// as a fragment's 1.0 / gl_FragCoord.w. Pixel to pixel: the target has the depth's size.

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec2 v_ndc;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform DistanceBuf {
    vec4 camera; // x = near plane, y = far plane
};
layout(binding = 1) uniform sampler2D depthTex; // 0..1 on every backend (OpenGL-style projection, remapped)

void main()
{
    float d = texelFetch(depthTex, ivec2(gl_FragCoord.xy), 0).r;
    float n = camera.x, f = camera.y;
    fragColor = vec4(2.0 * n * f / (f + n - (2.0 * d - 1.0) * (f - n)), 0.0, 0.0, 1.0);
}
