#version 440

// Depth of field (HDR, before tonemapping)
//
// texA      the scene (linear HDR)
// depthTex  its depth (see the binding below for turning it into a distance; camera.xy = near, far)
// params0   x = focus distance (world units), y = f-stop, z = max blur radius in pixels
//           (already scaled to this image's height), w = focal length in mm (from the
//           vertical field of view, for a 24 mm tall sensor)
//
// Runs at half size (inputSize and params0.z are the half size's; texA and depthTex are
// full size, sampled by uv). post_dof_composite.frag then mixes the result with the sharp
// scene, by each full-size pixel's own circle of confusion and the alpha written here: the
// blur nearer objects spread onto the pixel.

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

const float GOLDEN_ANGLE = 2.39996323;
const float RAD_SCALE    = 0.5;

float CoC(float d)
{
    float near = camera.x;
    float far  = camera.y;
    float D    = 2.0 * near * far / (far + near - (2.0 * d - 1.0) * (far - near)) * 1000.0; // mm

    float S = params0.x * 1000.0; // focus distance, mm
    float N = params0.y;          // f-stop
    float F = params0.w;          // focal length, mm
    float A = F / N;              // aperture diameter, mm

    float cocMm = A * F * abs(D - S) / (D * (S - F)); // diameter on the sensor
    float cocPx = cocMm / 24.0 * inputSize.y;         // diameter in pixels
    return clamp(0.5 * cocPx, 0.0, params0.z);
}

void main()
{
    if (camera.z < 0.0)
    {
        fragColor = texture(texA, v_uv);
        return;
    }

    float centerDepth = texture(depthTex, v_uv).r;
    float centerCoC = CoC(centerDepth);

    vec3 col       = texture(texA, v_uv).rgb;
    float radScale = max(RAD_SCALE, params0.z / 16.0);
    float radius   = radScale;
    float tot      = 1.0;
    float bleed    = 0.0; // largest circle of confusion spreading onto this pixel from in front
    for (float ang = 0.0; radius < params0.z; ang += GOLDEN_ANGLE)
    {
        vec2 uv           = v_uv + vec2(cos(ang), sin(ang)) * inputSize.zw * radius;
        vec3 sampleCol    = texture(texA, uv).rgb;
        float sampleDepth = texture(depthTex, uv).r;
        float sampleCoC   = CoC(sampleDepth);
        if (sampleDepth > centerDepth) sampleCoC = clamp(sampleCoC, 0.0, centerCoC * 2.0);
        float m = smoothstep(radius - 0.5, radius + 0.5, sampleCoC);
        if (sampleDepth < centerDepth) bleed = max(bleed, m * sampleCoC);
        col    += mix(col / tot, sampleCol, m);
        tot    += 1.0;
        radius += radScale / radius;
    }

    // alpha: blur that nearer objects spread onto this pixel, for post_dof_composite.frag
    fragColor = vec4(col / tot, bleed);
}
