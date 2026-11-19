#version 440

// Depth of field, second pass (full size): the sharp scene where it's in focus, the blurred
// half-size image (post_dof.frag) where it isn't.
//
// texA      the scene (linear HDR, full size)
// texB      post_dof.frag's result (half size): rgb = blurred scene, a = blur radius (in its
//           pixels) that nearer, out of focus objects spread onto the pixel
// depthTex  the scene's depth
// params0   post_dof.frag's: x = focus distance, y = f-stop, z = max blur radius in half-size
//           pixels, w = focal length in mm
//
// Each pixel's own blur comes from the full-size depth, and the half-size image is read
// only where its pixels lie at this pixel's depth: filtered across an object's outline, it
// would soften in-focus edges and tint the background next to them.

layout(location = 0) in vec2 v_uv;  // 0..1 over the target, (0,0) = top left on every backend
layout(location = 1) in vec2 v_ndc;
layout(location = 0) out vec4 fragColor;

// Same block in every post_*.frag (PostProcessor in engine/render/postprocess.h).
layout(std140, binding = 0) uniform PostBuf {
    vec4 inputSize;  // full size
    vec4 outputSize;
    vec4 camera;     // x = near, y = far, z = 1 with a depth texture, -1 without
    vec4 params0;
    vec4 params1;
};
layout(binding = 1) uniform sampler2D texA;
layout(binding = 2) uniform sampler2D texB;
layout(binding = 3) uniform sampler2D depthTex;

// View distance (world units) of a depth buffer value.
float viewDistance(float d)
{
    float near = camera.x;
    float far  = camera.y;
    return 2.0 * near * far / (far + near - (2.0 * d - 1.0) * (far - near));
}

// The circle of confusion's radius in half-size pixels at view distance `dist`: the thin
// lens of CoC() in post_dof.frag, measured on this image's height and halved.
float halfCoC(float dist)
{
    float D = dist * 1000.0; // mm
    float S = params0.x * 1000.0;
    float F = params0.w;
    float A = F / params0.y;
    float cocMm = A * F * abs(D - S) / (D * (S - F));
    return clamp(0.25 * cocMm / 24.0 * inputSize.y, 0.0, params0.z); // diameter -> radius, full -> half
}

// texB at v_uv, from its four nearest pixels like bilinear filtering, each also weighted by
// how close its depth is to `dist`. Its pixel (i, j) was computed at the full-size pixel
// (2i, 2j), uv (i + 0.25, j + 0.25) / its size (see PostProcessor): that's where it sits,
// and the depth post_dof.frag read for it.
vec4 blurredAt(float dist)
{
    vec2 size = vec2(textureSize(texB, 0));
    vec2 p = v_uv * size - 0.25;
    vec2 base = floor(p);
    vec2 f = p - base;
    vec4 sum = vec4(0.0);
    float total = 0.0;
    for (int j = 0; j < 2; ++j) {
        for (int i = 0; i < 2; ++i) {
            vec2 texel = base + vec2(i, j);
            float bilinear = (i == 0 ? 1.0 - f.x : f.x) * (j == 0 ? 1.0 - f.y : f.y);
            float relative = abs(viewDistance(texture(depthTex, (texel + 0.25) / size).r) - dist) / (0.02 * dist);
            float w = bilinear / (1.0 + relative * relative) + 1e-5;
            sum += texture(texB, (texel + 0.5) / size) * w;
            total += w;
        }
    }
    return sum / total;
}

void main()
{
    vec4 sharp = texture(texA, v_uv);
    if (camera.z < 0.0) { // no depth: post_dof.frag only copied the scene
        fragColor = sharp;
        return;
    }
    float dist = viewDistance(texture(depthTex, v_uv).r);
    vec4 blurred = blurredAt(dist);
    float blur = max(halfCoC(dist), blurred.a);
    // Half a pixel of blur there is one pixel here: from about one pixel on, the half-size
    // image is as good as a full-size blur would be, and below that it fades to the sharp one.
    float w = smoothstep(0.0, 1.0, blur);
    fragColor = vec4(mix(sharp.rgb, blurred.rgb, w), sharp.a);
}
