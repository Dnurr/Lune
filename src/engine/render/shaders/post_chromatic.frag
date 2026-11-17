#version 440

// Chromatic aberration (display colors, after tonemapping)
//
// texA      the image
// params0   x = intensity, y = pixel scale (image height / 1080)

layout(location = 0) in vec2 v_uv;  // 0..1 over the target, (0,0) = top left on every backend
layout(location = 1) in vec2 v_ndc;
layout(location = 0) out vec4 fragColor;

// Same block in every post_*.frag (PostProcessor in engine/render/postprocess.h).
layout(std140, binding = 0) uniform PostBuf {
    vec4 inputSize;  // texture a: xy = size in pixels, zw = 1 / size (one texel)
    vec4 outputSize; // this pass's target: xy = size in pixels, zw = 1 / size
    vec4 camera;     // x = near plane, y = far plane,
                     // z = 1 with a depth texture, -1 without (depthTex is a white 1x1 stand-in)
                     // w = seconds (0 for now: the timeline will drive it)
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
    vec2 center = v_uv - vec2(0.5);
    float intensity = length(v_uv - 0.5) * sqrt(2.0) * params0.x;

    float r = texture(texA, v_uv + (center * intensity * -0.006)).r;
    float g = texture(texA, v_uv + (center * intensity * -0.003)).g;
    float b = texture(texA, v_uv + (center * intensity * -0.015)).b;
    
    fragColor = vec4(vec3(r, g, b), 1.0);
}
