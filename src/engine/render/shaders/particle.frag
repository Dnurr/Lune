#version 440

// Particles: the color of one quad's pixel.
//
// The output is premultiplied: rgb already multiplied by alpha. The blend comes from the
// emitter: additive (rgb added to the scene, alpha ignored) or alpha (over the scene).
// Drawn into the HDR scene target (linear values, before exposure and tonemapping), depth
// tested against the meshes, without writing depth.

layout(location = 0) in vec2 v_corner; // -1..1 across the quad
layout(location = 1) in vec4 v_color;  // linear RGB (HDR), a = opacity

layout(location = 0) out vec4 fragColor;

void main()
{
    // TODO: soft particles (fade near surfaces), once the scene depth is available.
    float r = length(v_corner);
    if (r > 1.0) discard;

    float falloff = 1.0 - smoothstep(0.0, 1.0, r);
    float alpha   = v_color.a * falloff;

    fragColor = vec4(v_color.rgb * alpha, alpha);
}
