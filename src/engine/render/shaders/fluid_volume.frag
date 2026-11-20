#version 440

// A Fluid Volume seen through one pixel: ray marching from the camera (or where the ray enters
// the box) to where it leaves it, or to the scene's surface if that's nearer. Author: to be
// written.
//
// Drawn over the scene (dual-source blending: rgb = light scattered and emitted toward the
// camera, plus `transmittance` = how much of what's behind gets through, per channel; a = how
// much is hidden, on average), into the HDR target, after the meshes and
// before the particles, without depth testing: the march stops at sceneDistance itself.
//
// Until then: nothing (fully transparent).

layout(location = 0) in vec3 v_world; // where the ray leaves the box (a back face), world space
layout(location = 0) out vec4 fragColor;
// Dual-source blending: the second output multiplies what's behind, channel by channel
// (result = behind * transmittance.rgb + fragColor.rgb). Write T here (vec4(T, 1.0) or
// vec4(vec3(T), 1.0)); fragColor.a is still 1 - T (an average if T is per channel).
layout(location = 0, index = 1) out vec4 transmittance;

// One per Fluid Volume (FluidSystem::prepareDraw()).
layout(std140, binding = 0) uniform VolumeBuf {
    mat4 viewProj;       // world -> clip, for this backend
    mat4 model;          // box space (the cube -0.5..0.5) -> world
    mat4 worldToBox;     // world -> box space; texture coordinates = box position + 0.5
    vec4 eye;            // xyz: the camera, world space
    vec4 lightDir;       // xyz: the direction the main light shines along (world, unit); w = 1 with a light
    vec4 lightColor;     // rgb: its radiance (linear, intensity included)
    vec4 look;           // x = density (extinction per unit of smoke per world unit), y = samples,
                         // z = emission (brightness per unit of temperature),
                         // w = 1 when sceneDistance holds the scene (0: a stand-in)
    vec4 color;          // rgb: scattering color (linear)
    vec4 emissionColor;  // rgb: fire color (linear)
    vec4 grid;           // xyz = voxels, w = voxel size (world units)
    vec4 params;         // xy = viewport size in pixels, z = scene time (seconds)
    vec4 extinction;     // rgb: extinction per channel, relative to look.x (linear; white = grey)
};
layout(binding = 1) uniform sampler3D density;        // x = smoke, y = temperature; linear, clamped
layout(binding = 2) uniform sampler2D sceneDistance;  // view depth of the scene per pixel, like
                                                      // particle.frag's (texelFetch at gl_FragCoord)

void main()
{
    vec3 o = (worldToBox * vec4(eye.xyz, 1.0)).xyz;
    vec3 e = (worldToBox * vec4(v_world, 1.0)).xyz;

    vec3 dir = e - o;

    vec3 t0      = (vec3(-0.5) - o) / dir;
    vec3 t1      = (vec3( 0.5) - o) / dir;
    vec3 tmin    = min(t0, t1);
    float tEnter = max(max(tmin.x, tmin.y), max(tmin.z, 0.0));
    float tExit  = 1.0;
        
    if (look.w > 0.5)
    {
        float sceneDepth = texelFetch(sceneDistance, ivec2(gl_FragCoord.xy), 0).r;
        float exitDepth  = 1.0 / gl_FragCoord.w;
        tExit            = min(tExit, sceneDepth / exitDepth);
    }
    
    if (tExit <= tEnter) discard;

    int n    = int(look.y);
    float dt = (tExit - tEnter) / float(n);
    float ds = length(v_world - eye.xyz) * dt;
    
    vec3 L              = vec3(0.0);
    vec3 T              = vec3(1.0);
    vec3 scatteredLight = color.rgb * lightColor.rgb * lightDir.w;

    const int LIGHT_STEPS = 6;
    vec3 toLight = mat3(worldToBox) * (-lightDir.xyz);

    for (int i = 0; i < n; i++)
    {
        vec3 p = o + dir * (tEnter + (float(i) + 0.5) * dt);
        vec2 s      = textureLod(density, p + 0.5, 0.0).xy;
        vec3 sigma = s.x * look.x * extinction.rgb;
        vec3 stepT = exp(-sigma * ds);

        float smoke  = 0.0;
        float dl     = 0.0;

        if (max(sigma.r, max(sigma.g, sigma.b)) > 1e-4)
        {
            vec3 l0     = (vec3(-0.5) - p) / toLight;
            vec3 l1     = (vec3( 0.5) - p) / toLight;
            vec3 lmax   = max(l0, l1);
            float lExit = min(min(lmax.x, lmax.y), lmax.z);
            dl          = lExit / float(LIGHT_STEPS);

            for (int j = 0; j < LIGHT_STEPS; j++)
            {
                vec3 q = p + toLight * ((float(j) + 0.5) * dl);
                smoke += textureLod(density, q + 0.5, 0.0).x;
            }
        }

        vec3 shadow = exp(-smoke * look.x * extinction.rgb * dl);

        if (max(T.r, max(T.g, T.b)) < 0.01) break;
        L += T * (1.0 - stepT) * scatteredLight * shadow;
        L += T * emissionColor.rgb * s.y * look.z * ds;
        T *= stepT;
    }

    transmittance = vec4(T, 1.0);
    fragColor = vec4(L, 1.0 - dot(T, vec3(1.0 / 3.0)));
}
