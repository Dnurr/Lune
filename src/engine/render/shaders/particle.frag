#version 440

// Particles: the color of one quad's pixel.
//
// The output is premultiplied: rgb already multiplied by alpha. The blend comes from the
// emitter: additive (rgb added to the scene, alpha ignored) or alpha (over the scene).
// Drawn into the HDR scene target (linear values, before exposure and tonemapping), depth
// tested against the meshes, without writing depth.
//
// Soft particles: sceneDistance holds, per pixel, how far the scene behind is from the camera
// (view depth, world units). The particle's own view depth at this pixel is
// 1.0 / gl_FragCoord.w: the same measure. Read the scene at this very pixel with
// texelFetch(sceneDistance, ivec2(gl_FragCoord.xy), 0).r (the texture has the target's size).
// Only when soft.x is 1; soft.y is the emitter's softness (world units).

layout(location = 0) in vec2 v_corner; // -1..1 across the quad
layout(location = 1) in vec4 v_color;  // linear RGB (HDR), a = opacity

layout(location = 0) out vec4 fragColor;

// The same block as particle.vert's (ParticleSystem::prepareDraw()).
layout(std140, binding = 0) uniform DrawBuf {
    mat4 viewProj;
    vec4 cameraRight;
    vec4 cameraUp;
    vec4 cameraPos;
    vec4 params;   // xy = viewport size in pixels, z = scene time (seconds)
    vec4 soft;     // x = 1 when sceneDistance holds the scene (0: a stand-in, don't fade),
                   // y = softness: how close to a surface the particle starts fading (world units)
};
layout(binding = 1) uniform sampler2D sceneDistance; // view depth of the scene, per pixel

void main()
{
    float r = length(v_corner);
    if (r > 1.0) discard;

    float falloff = 1.0 - smoothstep(0.0, 1.0, r);
    float alpha   = v_color.a * falloff;
    
    if (soft.x > 0.5)
    {
        float sceneDepth = texelFetch(sceneDistance, ivec2(gl_FragCoord.xy), 0).r;
        float particleDepth = 1.0 / gl_FragCoord.w;
        float fade = clamp((sceneDepth - particleDepth) / max(soft.y, 1e-4), 0.0, 1.0);
        alpha *= fade;
    }

    fragColor = vec4(v_color.rgb * alpha, alpha);
}
