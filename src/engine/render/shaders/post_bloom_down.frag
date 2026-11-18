#version 440

// Bloom, downsample step (HDR)
// Runs once per pyramid level, each at half the size of the previous one.
//
// texA      the level above: the scene for level 0, then the previous downsample
// params0   x = level (0 = reads the full-size scene), y = threshold, z = intensity,
//           w = radius (0..1)
// inputSize.zw is one texel of texA, outputSize this level's size.
//
// Until then: plain bilinear sampling (no threshold, no filter).

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

float RGBToLuminance(vec3 col)
{
    return dot(col, vec3(0.2126, 0.7152, 0.0722));
}

float KarisWeight(vec3 avg)
{
    return 1.0 / (1.0 + RGBToLuminance(avg));
}

void main()
{
    float x = inputSize.z;
    float y = inputSize.w;

    vec3 a = texture(texA, vec2(v_uv.x - 2.0 * x, v_uv.y + 2.0 * y)).rgb;
    vec3 b = texture(texA, vec2(v_uv.x,           v_uv.y + 2.0 * y)).rgb;
    vec3 c = texture(texA, vec2(v_uv.x + 2.0 * x, v_uv.y + 2.0 * y)).rgb;
    vec3 d = texture(texA, vec2(v_uv.x - 2.0 * x, v_uv.y)).rgb;
    vec3 e = texture(texA, vec2(v_uv.x,           v_uv.y)).rgb;
    vec3 f = texture(texA, vec2(v_uv.x + 2.0 * x, v_uv.y)).rgb;
    vec3 g = texture(texA, vec2(v_uv.x - 2.0 * x, v_uv.y - 2.0 * y)).rgb;
    vec3 h = texture(texA, vec2(v_uv.x,           v_uv.y - 2.0 * y)).rgb;
    vec3 i = texture(texA, vec2(v_uv.x + 2.0 * x, v_uv.y - 2.0 * y)).rgb;
    vec3 j = texture(texA, vec2(v_uv.x - x, v_uv.y + y)).rgb;
    vec3 k = texture(texA, vec2(v_uv.x + x, v_uv.y + y)).rgb;
    vec3 l = texture(texA, vec2(v_uv.x - x, v_uv.y - y)).rgb;
    vec3 m = texture(texA, vec2(v_uv.x + x, v_uv.y - y)).rgb;

    vec3 down = vec3(0.0);

    if (params0.x == 0.0)
    {
        vec3 g0 = (j + k + l + m) * 0.25;
        vec3 g1 = (a + b + d + e) * 0.25;
        vec3 g2 = (b + c + e + f) * 0.25;
        vec3 g3 = (d + e + g + h) * 0.25;
        vec3 g4 = (e + f + h + i) * 0.25;

        float p0 = 0.5   * KarisWeight(g0);
        float p1 = 0.125 * KarisWeight(g1);
        float p2 = 0.125 * KarisWeight(g2);
        float p3 = 0.125 * KarisWeight(g3);
        float p4 = 0.125 * KarisWeight(g4);
        
        down = (g0 * p0 + g1 * p1 + g2 * p2 + g3 * p3 + g4 * p4) / (p0 + p1 + p2 + p3 + p4);

        float bright = max(down.r, max(down.g, down.b));
        float over = max(bright - params0.y, 0.0);
        down *= over / max(bright, 0.0001);
    }
    else {
        down     = e * 0.125;
        down     += (a + c + g + i) * 0.03125; 
        down     += (b + d + f + h) * 0.0625;
        down     += (j + k + l + m) * 0.125;
    }

    fragColor = vec4(down, 1.0);
}
