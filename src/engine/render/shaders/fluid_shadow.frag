#version 440

// A Fluid Volume's shadow: the light that gets through the volume, seen from a light, into the
// shadow maps' translucency map (FluidSystem::drawShadows())
//
// One fragment per texel of the tile, where the light enters the box (v_world, on a face
// toward the light). Output:
//   rgb  the fraction of the light that gets through the whole volume along the light's ray,
//        per channel (Beer-Lambert: exp(-extinction x smoke x length), as in the self-shadow
//        march of fluid_volume.frag); multiplied by the blend with what's already there
//   a    the linear depth of v_world (linearDepth() below); the blend keeps the nearest, and
//        mesh.frag only dims receivers farther from the light than it

layout(location = 0) in vec3 v_world; // where the light enters the box, world space

layout(location = 0) out vec4 fragColor;

// One per Fluid Volume and tile (FluidSystem::prepareShadows()).
layout(std140, binding = 0) uniform ShadowBuf {
    mat4 lightViewProj; // world -> the light's clip space (this tile), for this backend
    mat4 model;         // box space (the cube -0.5..0.5) -> world
    mat4 worldToBox;    // world -> box space; texture coordinates = box position + 0.5
    vec4 light;         // directional (w = 0): xyz = where it shines (world, unit);
                        // point (w = 1): xyz = its position (world)
    vec4 view;          // x = 1 for a point light's face, y = its near plane, z = its far plane
    vec4 look;          // x = density (extinction per unit of smoke per world unit), y = samples
    vec4 extinction;    // rgb: extinction per channel, relative to look.x (linear; white = grey)
};
layout(binding = 1) uniform sampler3D density; // x = smoke, y = temperature; linear, clamped

// The linear depth (0..1) of this fragment, as the translucency map keeps it (see
// shadow_front.frag): orthographic depth is already linear; a point light's perspective depth
// becomes the distance along the face's axis / far.
float linearDepth()
{
    float depth = gl_FragCoord.z;
    if (view.x > 0.5) {
        float near = view.y, far = view.z;
        depth = near / (far - depth * (far - near));
    }
    return depth;
}

const int LIGHT_STEPS = 6;

void main()
{
    vec3 dirW = light.w > 0.5 ? normalize(v_world - light.xyz) : light.xyz;
    vec3 dir  = mat3(worldToBox) * dirW;
    vec3 p    = (worldToBox * vec4(v_world, 1.0)).xyz;

    vec3 l0     = (vec3(-0.5) - p) / dir;
    vec3 l1     = (vec3( 0.5) - p) / dir;
    vec3 lmax   = max(l0, l1);
    float lExit = min(min(lmax.x, lmax.y), lmax.z);

    int   n     = int(look.y);
    float dl    = lExit / float(n);
    float smoke = 0.0;
    for (int j = 0; j < n; j++)
    {
        vec3 q = p + dir * ((float(j) + 0.5) * dl);
        smoke += textureLod(density, q + 0.5, 0.0).x;
    }

    vec3 shadow = exp(-smoke * look.x * extinction.rgb * dl);
    fragColor = vec4(shadow, linearDepth());
}
