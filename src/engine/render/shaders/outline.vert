#version 440

// Selection outline ("inverted hull"): the back faces of the mesh, pushed outwards in
// screen space by `outline.w` pixels, away from the projected object center.
// Drawn with front-face culling, so only a rim around the silhouette is visible.
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

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

void main()
{
    vec4 clip = mvp * vec4(position, 1.0);
    vec4 center = mvp * vec4(0.0, 0.0, 0.0, 1.0);
    vec2 diffPx = (clip.xy / clip.w - center.xy / center.w) * viewport.xy * 0.5;
    float len = length(diffPx);
    vec2 dir = len > 1e-4 ? diffPx / len : vec2(0.0);
    clip.xy += dir * outline.w * 2.0 / viewport.xy * clip.w;
    gl_Position = clip;
}
