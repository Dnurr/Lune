#version 440

// Display pass: converts the HDR scene (linear values, possibly > 1) into what the screen
// shows (0..1, sRGB). Everything after this pass works in display colors.
//
// Step 2 of the HDR work goes here: exposure and tonemapping, before the sRGB encoding.
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec2 v_ndc;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    vec4 params; // free: filled by Renderer::render() ("Display uniforms"), all 0 for now
};
layout(binding = 1) uniform sampler2D sceneTex;

vec3 linearToSRGB(vec3 c)
{
    c = clamp(c, 0.0, 1.0);
    vec3 l = c * 12.92;
    vec3 h = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
    return mix(l, h, step(0.0031308, c));
}

void main()
{
    vec3 scene = texture(sceneTex, v_uv).rgb;
    fragColor = vec4(linearToSRGB(scene), 1.0);
}
