#version 440

// Film grain (display colors, after tonemapping). Author: to be written.
//
// texA      the image
// params0   x = intensity (0..1), y = grain size in pixels (scaled to this image's height)
// camera.w  seconds, to animate the noise (0 until the timeline drives it)
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
                     // w = seconds (0 for now: the timeline will drive it)
    vec4 params0;    // the effect's settings: see below
    vec4 params1;
};
layout(binding = 1) uniform sampler2D texA;     // linear filtering, clamped to the edge
layout(binding = 2) uniform sampler2D texB;     // linear filtering, clamped to the edge
layout(binding = 3) uniform sampler2D depthTex; // the scene's depth buffer, nearest: values 0..1 on every
                                                // backend (OpenGL-style projection, remapped per backend):
                                                // view distance = 2nf / (f + n - (2d - 1)(f - n))

const float PI = 3.14159265359;

vec2 Hash(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

vec2 BoxMuller(vec2 u)
{
    float u1 = max(u.x, 1e-7);
    float u2 = u.y;

    float rad = sqrt(-2.0 * log(u1));
    float ang = 2.0 * PI * u2;

    return vec2(rad * cos(ang), rad * sin(ang));
}

void main()
{
    vec3 col = texture(texA, v_uv).rgb;
    
    vec2 pos = gl_FragCoord.xy;
    vec2 u = Hash(floor(pos / params0.y) + vec2(camera.w * 137.0, camera.w * 293.0));
    float g = BoxMuller(u).x;
    vec3 noise = vec3(g);

    vec3 final = col + noise * params0.x;

    fragColor = vec4(final, 1.0);
}
