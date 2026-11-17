#version 440

// Depth of field (HDR, before tonemapping). Author: to be written.
//
// texA      the scene (linear HDR)
// depthTex  its depth (see camera.z for the convention, camera.xy for near and far)
// params0   x = focus distance (world units), y = f-stop, z = max blur radius in pixels
//           (already scaled to this image's height), w = focal length in mm (from the
//           vertical field of view, for a 24 mm tall sensor)
//
// Until then: a copy of the scene.

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
