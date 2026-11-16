// Shadow passes of a node material (ShadowMaps), for clip and blend materials. The
// Renderer builds the fragment shader from mesh.frag's declarations (everything above its
// material begin marker), the material's generated code, then this file.
//
//   clip   drawn into the depth atlases: pixels the material cuts away cast no shadow
//   blend  drawn into shadowTranslucency (no depth atlas), blended per texel:
//            rgb *= the output's rgb      (every layer lets its share of light through)
//            a    = min(a, the output's a) (the nearest translucent caster's linear depth)

// Same block as shadow_material.vert.
layout(std140, binding = 20) uniform shadowViewBuf {
    mat4 shadowViewProj;
    vec4 shadowView; // x: 1 for a point light's face, y: near, z: far
};

void main()
{
    Surface surf = materialSurface();
    // Clip: material.z is the cutoff. Blend: fully clear pixels must not count as the
    // nearest caster either.
    if (surf.alpha < max(material.z, 1.0 / 255.0))
        discard;

    // Linear depth 0..1 (see shadowTranslucency in mesh.frag): orthographic depth is
    // already linear; a point light's perspective depth becomes the distance along the
    // face's axis, divided by far.
    float depth = gl_FragCoord.z;
    if (shadowView.x > 0.5) {
        float near = shadowView.y, far = shadowView.z;
        depth = near / (far - depth * (far - near));
    }
    // The light a surface with this alpha lets through (the depth pass has no color target
    // and ignores it).
    vec3 through = mix(vec3(1.0), surf.baseColor * (1.0 - surf.absorption), surf.alpha);
    fragColor = vec4(through, depth);
}
