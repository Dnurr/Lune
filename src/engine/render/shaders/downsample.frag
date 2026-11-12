#version 440

// Downsample by averaging 4 bilinear taps (16 source texels).
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec2 v_ndc;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    vec4 params; // xy: source texel size, zw: unused
};
layout(binding = 1) uniform sampler2D src;

void main()
{
    vec2 o = params.xy;
    vec3 c = texture(src, v_uv + vec2(-o.x, -o.y)).rgb
           + texture(src, v_uv + vec2( o.x, -o.y)).rgb
           + texture(src, v_uv + vec2(-o.x,  o.y)).rgb
           + texture(src, v_uv + vec2( o.x,  o.y)).rgb;
    fragColor = vec4(c * 0.25, 1.0);
}
