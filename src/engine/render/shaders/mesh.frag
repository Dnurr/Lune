#version 440

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec3 v_world;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    mat4 model;
    mat4 normalMatrix;
    vec4 color;
    vec4 outline;
    vec4 viewport;
    vec4 cameraPos;
    vec4 material;  // x: roughness, y: metallic (0..1)
};

// Simple editor shading (studio-like): key + fill light and a sky/ground ambient.
// Lighting in linear space, gamma-encoded at the end (until the HDR pipeline arrives).
void main()
{
    vec3 n = normalize(v_normal);
    vec3 toEye = normalize(cameraPos.xyz - v_world);
    if (dot(n, toEye) < 0.0)
        n = -n; // two-sided: back faces are lit like front faces

    vec3 keyDir = normalize(vec3(0.45, 0.8, 0.35));
    vec3 fillDir = normalize(vec3(-0.6, 0.3, -0.5));
    float key = max(dot(n, keyDir), 0.0);
    float fill = max(dot(n, fillDir), 0.0);
    float hemi = 0.5 + 0.5 * n.y;
    vec3 ambient = mix(vec3(0.05, 0.05, 0.06), vec3(0.14, 0.15, 0.18), hemi);
    // Soft rim towards the camera edges, to read the silhouette on the dark background.
    float rim = pow(1.0 - max(dot(n, toEye), 0.0), 3.0) * 0.25;

    vec3 base = color.rgb;
    vec3 lit = base * (ambient + vec3(1.0, 0.97, 0.92) * key * 0.9 + vec3(0.55, 0.65, 0.9) * fill * 0.3) + rim;
    fragColor = vec4(lit, 1.0);
}
