#version 440

// Bloom, downsample step (HDR). Author: to be written.
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
                     // z = depth mode: 1 = depth 0..1 (D3D, Vulkan, Metal), 0 = -1..1 (OpenGL),
                     //     -1 = no depth texture (depthTex is a white 1x1 stand-in)
                     // w = seconds (0 for now: the timeline will drive it)
    vec4 params0;    // the effect's settings: see below
    vec4 params1;
};
layout(binding = 1) uniform sampler2D texA;     // linear filtering, clamped to the edge
layout(binding = 2) uniform sampler2D texB;     // linear filtering, clamped to the edge
layout(binding = 3) uniform sampler2D depthTex; // the scene's depth buffer (raw values), nearest

void main()
{
    fragColor = texture(texA, v_uv);
}
