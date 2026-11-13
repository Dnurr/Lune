#version 440

// Display pass: converts the HDR scene (linear values, possibly > 1) into what the screen
// shows (0..1, sRGB). Everything after this pass works in display colors.
//
// Step 2 of the HDR work goes here: exposure and tonemapping, before the sRGB encoding.
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec2 v_ndc;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    vec4 params; // x: exposure in EV, y: tonemapper index (0 none, 1 Reinhard, 2 ACES, 3 AgX), zw: free
                 // set from the viewport's Display menu (DisplaySettings) via Renderer::setDisplayParams
};
layout(binding = 1) uniform sampler2D sceneTex;

vec3 reinhard(vec3 col)
{
    return col / (1.0 + col);
}

// ACES fit by Stephen Hill (@self_shadow), from BakingLab (MIT):
// https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl
vec3 aces(vec3 col)
{
    mat3 in_m = mat3
    (
        0.59719, 0.35458, 0.04823,
        0.07600, 0.90834, 0.01566,
        0.02840, 0.13383, 0.83777
    );

    mat3 out_m = mat3
    (
         1.60475, -0.53108, -0.07367,
        -0.10208,  1.10813, -0.00605,
        -0.00327, -0.07276,  1.07602
    );

    col *= in_m;

    vec3 a = col * (col + 0.0245786) - 0.000090537;
    vec3 b = col * (0.983729 * col + 0.4329510) + 0.238081;

    col = a / b;

    col *= out_m;

    return clamp(col, 0.0, 1.0);
}

// AgX display transform by Troy Sobotka, minimal fit by Benjamin Wrensch:
// https://iolite-engine.com/blog_posts/minimal_agx_implementation
// (the page states no license for the code; credited here and in the README)
vec3 agx(vec3 col)
{
    const mat3 agx_mat = mat3(
        0.842479062253094, 0.0423282422610123, 0.0423756549057051,
        0.0784335999999992,  0.878468636469772,  0.0784336,
        0.0792237451477643, 0.0791661274605434, 0.879142973793104
    );
        
    const float min_ev = -12.47393;
    const float max_ev = 4.026069;

    col = agx_mat * col;
    
    col = max(col, 1e-10);
    col = clamp(log2(col), min_ev, max_ev);
    col = (col - min_ev) / (max_ev - min_ev);
    
    vec3 x2 = col * col;
    vec3 x4 = x2 * x2;

    col = + 15.5     * x4 * x2
          - 40.14    * x4 * col
          + 31.96    * x4
          - 6.868    * x2 * col
          + 0.4298   * x2
          + 0.1191   * col
          - 0.00232;

    const mat3 agx_mat_inv = mat3(
        1.19687900512017, -0.0528968517574562, -0.0529716355144438,
        -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
        -0.0990297440797205, -0.0989611768448433, 1.15107367264116
    );
        
    col = agx_mat_inv * col;
    
    col = max(col, 0.0);
    col = pow(col, vec3(2.2));

    return col;
}

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
    scene = max(scene, 0.0);
    
    scene *= exp2(params.x);
    switch (int(params.y))
    {
        case 1:
            scene = reinhard(scene);
            break;
        case 2:
            scene = aces(scene);
            break;
        case 3:
            scene = agx(scene);
            break;
        default:
            break;
    }
    
    fragColor = vec4(linearToSRGB(scene), 1.0);
}
