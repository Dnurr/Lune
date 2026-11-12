#version 440

// Infinite world grid on the y = 0 plane, drawn as a fullscreen pass:
// each pixel casts a ray from the camera and intersects it with the plane.
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec2 v_ndc;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 invViewProj;   // inverse of proj * view (OpenGL clip-space convention)
    vec4 cameraPos;     // xyz
    vec4 params;        // x: NDC y sign (1 or -1), y: fade distance, z: minor cell size, w: major cell size
    vec4 skyTop;        // background looking up
    vec4 skyBottom;     // background towards the horizon / down
    vec4 minorColor;    // rgb + strength
    vec4 majorColor;    // rgb + strength
    vec4 axisXColor;    // line where z = 0
    vec4 axisZColor;    // line where x = 0
};

vec3 unproject(vec2 ndc, float z)
{
    vec4 p = invViewProj * vec4(ndc, z, 1.0);
    return p.xyz / p.w;
}

// 1 on a grid line (about `width` pixels wide), 0 elsewhere, anti-aliased with screen derivatives.
float gridLine(vec2 coord, float width)
{
    vec2 d = fwidth(coord);
    vec2 g = abs(fract(coord - 0.5) - 0.5) / (d * width);
    return 1.0 - min(min(g.x, g.y), 1.0);
}

void main()
{
    vec2 ndc = vec2(v_ndc.x, v_ndc.y * params.x);
    vec3 nearP = unproject(ndc, -1.0);
    vec3 farP = unproject(ndc, 1.0);
    vec3 dir = farP - nearP;
    vec3 ndir = normalize(dir);

    vec3 sky = mix(skyBottom.rgb, skyTop.rgb, smoothstep(-0.1, 0.7, ndir.y));

    // Everything below is computed for every pixel (derivatives need uniform control flow);
    // `onPlane` masks out rays that never reach the plane.
    float denom = abs(dir.y) < 1e-6 ? 1e-6 : dir.y;
    float t = -nearP.y / denom;
    float onPlane = t > 0.0 ? 1.0 : 0.0;
    vec3 P = nearP + max(t, 0.0) * dir;

    float minorSize = params.z;
    float majorSize = params.w;
    float minor = gridLine(P.xz / minorSize, 1.0);
    float major = gridLine(P.xz / majorSize, 1.5);

    vec2 dw = fwidth(P.xz);
    float axisX = 1.0 - min(abs(P.z) / (dw.y * 1.5), 1.0);
    float axisZ = 1.0 - min(abs(P.x) / (dw.x * 1.5), 1.0);

    // Fade with distance from the camera, and minor lines when cells get smaller than a few pixels.
    float dist = length(P - cameraPos.xyz);
    float fade = (1.0 - smoothstep(params.y * 0.3, params.y, dist)) * onPlane;
    float minorFade = 1.0 - smoothstep(0.15, 0.45, max(dw.x, dw.y) / minorSize);
    // At grazing angles lines crowd together: soften them near the horizon.
    fade *= smoothstep(0.0, 0.08, abs(ndir.y));

    vec3 col = sky;
    col = mix(col, minorColor.rgb, minor * minorColor.a * minorFade * fade);
    col = mix(col, majorColor.rgb, major * majorColor.a * fade);
    col = mix(col, axisXColor.rgb, axisX * axisXColor.a * fade);
    col = mix(col, axisZColor.rgb, axisZ * axisZColor.a * fade);

    fragColor = vec4(col, 1.0);
}
