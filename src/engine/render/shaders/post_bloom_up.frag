#version 440

// Bloom, upsample step (HDR)
// Runs from the smallest level back up: each step makes level i from the level below it.
//
// texA      the level below, already upsampled up to there (or the smallest downsample)
// texB      this level's downsample
// params0   x = level, y = threshold, z = intensity, w = radius (0..1)
// inputSize is texA's size (one level down), outputSize this level's.
//
// Until then: this level's downsample only (the levels below are ignored).

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
    float x = inputSize.z;
    float y = inputSize.w;

    vec3 a = texture(texA, vec2(v_uv.x - x, v_uv.y + y)).rgb;
    vec3 b = texture(texA, vec2(v_uv.x,     v_uv.y + y)).rgb;
    vec3 c = texture(texA, vec2(v_uv.x + x, v_uv.y + y)).rgb;
    vec3 d = texture(texA, vec2(v_uv.x - x, v_uv.y)).rgb;
    vec3 e = texture(texA, vec2(v_uv.x,     v_uv.y)).rgb;
    vec3 f = texture(texA, vec2(v_uv.x + x, v_uv.y)).rgb;
    vec3 g = texture(texA, vec2(v_uv.x - x, v_uv.y - y)).rgb;
    vec3 h = texture(texA, vec2(v_uv.x,     v_uv.y - y)).rgb;
    vec3 i = texture(texA, vec2(v_uv.x + x, v_uv.y - y)).rgb;

    vec3 up = e * 4.0;
    up     += (b + d + f + h) * 2.0;
    up     += (a + c + g + i);
    up     *= 1.0 / 16.0;

    fragColor = vec4(mix(texture(texB, v_uv).rgb, up, params0.w), 1.0);
}
