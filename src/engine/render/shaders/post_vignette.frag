#version 440

// Vignette (display colors, after tonemapping). Author: to be written.
//
// texA      the image
// params0   x = intensity (0..1), y = smoothness (0..1), z = aspect (width / height)
//
// Until then: a copy.

layout(location = 0) in vec2 v_uv;  // 0..1 over the target, (0,0) = top left on every backend
layout(location = 1) in vec2 v_ndc;
layout(location = 0) out vec4 fragColor;

// Same block in every post_*.frag (PostProcessor in engine/render/postprocess.h).
layout(std140, binding = 0) uniform PostBuf {
    vec4 inputSize;  // texture a: xy = size in pixels, zw = 1 / size (one texel)
    vec4 outputSize; // this pass's target: xy = size in pixels, zw = 1 / size
    vec4 camera;     // x = near plane, y = far plane,
                     // z = 1 with a depth texture, -1 without (depthTex is a white 1x1 stand-in)
                     // w = scene time in seconds (the Timeline's frame)
    vec4 params0;    // the effect's settings: see below
    vec4 params1;
};
layout(binding = 1) uniform sampler2D texA;     // linear filtering, clamped to the edge
layout(binding = 2) uniform sampler2D texB;     // linear filtering, clamped to the edge
layout(binding = 3) uniform sampler2D depthTex; // the scene's depth buffer, nearest: values 0..1 on every
                                                // backend (OpenGL-style projection, remapped per backend):
                                                // view distance = 2nf / (f + n - (2d - 1)(f - n))

void main()
{
    float vignette = 1.0 - length(v_uv - 0.5) * sqrt(2.0);
    vignette = smoothstep(0.0, max(params0.y, 0.0001), vignette);
    vec3 final = mix(texture(texA, v_uv).rgb, texture(texA, v_uv).rgb * vignette, params0.x);
    fragColor = vec4(final, 1.0);
}
