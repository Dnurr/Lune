#version 440

// One direction of a separable 9-tap Gaussian blur, using 5 bilinear fetches.
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec2 v_ndc;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    vec4 params; // xy: texel size * direction (one of them is 0)
};
layout(binding = 1) uniform sampler2D src;

void main()
{
    vec2 step = params.xy * params.z;
    vec3 c = texture(src, v_uv).rgb * 0.2270270270;
    c += texture(src, v_uv + step * 1.3846153846).rgb * 0.3162162162;
    c += texture(src, v_uv - step * 1.3846153846).rgb * 0.3162162162;
    c += texture(src, v_uv + step * 3.2307692308).rgb * 0.0702702703;
    c += texture(src, v_uv - step * 3.2307692308).rgb * 0.0702702703;
    fragColor = vec4(c, 1.0);
}
