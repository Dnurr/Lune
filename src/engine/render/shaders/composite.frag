#version 440

// Final image: the scene, with its blurred copy inside the "glass" rectangles
// (the floating panels drawn on top of the viewport).
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec2 v_ndc;
layout(location = 0) out vec4 fragColor;

#define MAX_RECTS 16

layout(std140, binding = 0) uniform buf {
    vec4 info;              // x: rect count, y: corner radius (px), z: 1 if framebuffer y is up, w: unused
    vec4 size;              // xy: target size in pixels
    vec4 rects[MAX_RECTS];  // x, y, width, height in pixels, origin top-left
};
layout(binding = 1) uniform sampler2D sceneTex;
layout(binding = 2) uniform sampler2D blurTex;

float roundedBox(vec2 p, vec2 halfSize, float r)
{
    vec2 q = abs(p) - halfSize + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main()
{
    vec2 px = vec2(v_uv.x, info.z > 0.5 ? 1.0 - v_uv.y : v_uv.y) * size.xy;

    float glass = 0.0;
    int count = int(info.x);
    for (int i = 0; i < MAX_RECTS; ++i) {
        if (i >= count)
            break;
        vec4 r = rects[i];
        vec2 halfSize = r.zw * 0.5;
        float d = roundedBox(px - (r.xy + halfSize), halfSize, min(info.y, min(halfSize.x, halfSize.y)));
        glass = max(glass, clamp(0.5 - d, 0.0, 1.0));
    }

    vec3 scene = texture(sceneTex, v_uv).rgb;
    vec3 blurred = texture(blurTex, v_uv).rgb;
    fragColor = vec4(mix(scene, blurred, glass), 1.0);
}
