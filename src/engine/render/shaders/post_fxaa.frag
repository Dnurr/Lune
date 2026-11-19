#version 440

// FXAA 3.11 Quality (after Simon Rodriguez's "Implementing FXAA"), the first display effect:
// it runs on the tonemapped sRGB image, before chromatic aberration, vignette and grain.
//
// texA      the image
// inputSize.zw is one texel.

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

#define QUALITY(q) ((q) < 5 ? 1.0 : ((q) > 5 ? ((q) < 10 ? 2.0 : ((q) < 11 ? 4.0 : 8.0)) : 1.5))
#define ColorOffset(o) textureOffset(texA, v_uv, o).rgb
const int ITERATIONS = 12;
const float SUBPIXEL_QUALITY = 0.75;

float RGB2Luma(vec3 color)
{
    return dot(color, vec3(0.299, 0.587, 0.114));
}
                                                
void main()
{ 
    vec3 color = texture(texA, v_uv).rgb;
    float luma = RGB2Luma(color);
    
    float lumaS = RGB2Luma(ColorOffset(ivec2(0, -1)));
    float lumaN = RGB2Luma(ColorOffset(ivec2(0,  1)));
    float lumaW = RGB2Luma(ColorOffset(ivec2(-1, 0)));
    float lumaE = RGB2Luma(ColorOffset(ivec2(1,  0)));

    float lumaMin = min(luma, min(min(lumaS, lumaN), min(lumaW, lumaE)));
    float lumaMax = max(luma, max(max(lumaS, lumaN), max(lumaW, lumaE)));

    float range = lumaMax - lumaMin;

    if (range < max(0.0312, lumaMax * 0.125))
    {
        fragColor = vec4(color, 1.0);
        return;
    }

    float lumaSW = RGB2Luma(ColorOffset(ivec2(-1, -1)));
    float lumaNE = RGB2Luma(ColorOffset(ivec2( 1,  1)));
    float lumaNW = RGB2Luma(ColorOffset(ivec2(-1,  1)));
    float lumaSE = RGB2Luma(ColorOffset(ivec2( 1, -1)));

    float lumaSN = lumaS + lumaN;
    float lumaWE = lumaW + lumaE;

    float lumaWCorners = lumaSW + lumaNW;
    float lumaSCorners = lumaSW + lumaSE;
    float lumaECorners = lumaSE + lumaNE;
    float lumaNCorners = lumaNE + lumaNW;

    float edgeH = abs(-2.0 * lumaW + lumaWCorners) + abs(-2.0 * luma + lumaSN) * 2.0 + abs(-2.0 * lumaE + lumaECorners);
    float edgeV = abs(-2.0 * lumaN + lumaNCorners) + abs(-2.0 * luma + lumaWE) * 2.0 + abs(-2.0 * lumaS + lumaSCorners);

    bool isH = (edgeH >= edgeV);

    float luma1 = isH ? lumaS : lumaW;
    float luma2 = isH ? lumaN : lumaE;

    float grad1 = luma1 - luma;
    float grad2 = luma2 - luma;

    bool isSteepest = abs(grad1) >= abs(grad2);

    float gradScaled = 0.25 * max(abs(grad1), abs(grad2));

    float stepLen = isH ? inputSize.w : inputSize.z;
    float lumaLocalAvg = 0.0;

    if (isSteepest)
    {
        stepLen *= -1.0;
        lumaLocalAvg = 0.5 * (luma1 + luma);
    } else {
        lumaLocalAvg = 0.5 * (luma2 + luma);
    }

    vec2 uv = v_uv;
    if (isH)
    {
        uv.y += stepLen * 0.5;
    } else {
        uv.x += stepLen * 0.5;
    }

    vec2 offset = isH ? vec2(inputSize.z, 0.0) : vec2(0.0, inputSize.w);

    vec2 uv1 = uv - offset;
    vec2 uv2 = uv + offset;
    
    float lumaEnd1 = RGB2Luma(texture(texA, uv1).rgb);
    float lumaEnd2 = RGB2Luma(texture(texA, uv2).rgb);
    lumaEnd1 -= lumaLocalAvg;
    lumaEnd2 -= lumaLocalAvg;
    
    bool reached1    = abs(lumaEnd1) >= gradScaled;
    bool reached2    = abs(lumaEnd2) >= gradScaled;
    bool reachedBoth = reached1 && reached2;

    if (!reached1)
    {
        uv1 -= offset;
    }
    if (!reached2)
    {
        uv2 += offset;
    }

    if (!reachedBoth)
    {
        for (int i = 2; i < ITERATIONS; i++)
        {
            if (!reached1)
            {
                lumaEnd1 = RGB2Luma(texture(texA, uv1).rgb);
                lumaEnd1 -= lumaLocalAvg;
            }

            if (!reached2)
            {
                lumaEnd2 = RGB2Luma(texture(texA, uv2).rgb);
                lumaEnd2 -= lumaLocalAvg;
            }

            reached1    = abs(lumaEnd1) >= gradScaled;
            reached2    = abs(lumaEnd2) >= gradScaled;
            reachedBoth = reached1 && reached2;

            if (!reached1)
            {
                uv1 -= offset * QUALITY(i);
            }
            if (!reached2)
            {
                uv2 += offset * QUALITY(i);
            }

            if (reachedBoth) break;
        }
    }
    
    float dist1 = isH ? (v_uv.x - uv1.x) : (v_uv.y - uv1.y);
    float dist2 = isH ? (uv2.x - v_uv.x) : (uv2.y - v_uv.y);

    bool isDir1     = dist1 < dist2;
    float distFinal = min(dist1, dist2);

    float edgeThickness = (dist1 + dist2);

    float pxlOffset = -distFinal / edgeThickness + 0.5;

    bool isLumaCenterSmaller = luma < lumaLocalAvg;
    bool currentVariation    = ((isDir1 ? lumaEnd1 : lumaEnd2) < 0.0) != isLumaCenterSmaller;

    float finalOffset = currentVariation ? pxlOffset : 0.0;

    float lumaAvg = (1.0 / 12.0) * (2.0 * (lumaSN + lumaWE) + lumaWCorners + lumaECorners);
    
    float subPxlOffset1 = clamp(abs(lumaAvg - luma) / range, 0.0, 1.0);
    float subPxlOffset2 = (-2.0 * subPxlOffset1 + 3.0) * subPxlOffset1 * subPxlOffset1;

    float subPxlOffsetFinal = subPxlOffset2 * subPxlOffset2 * SUBPIXEL_QUALITY;

    finalOffset = max(finalOffset, subPxlOffsetFinal);

    uv = v_uv;
    
    if (isH)
    {
        uv.y += finalOffset * stepLen;
    } else {
        uv.x += finalOffset * stepLen;
    }

    vec3 final = texture(texA, uv).rgb;

    fragColor = vec4(final, 1.0);
}
