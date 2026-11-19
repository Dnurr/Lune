#version 440

// Particles: the color of one quad's pixel. Author: to be written.
//
// The output is premultiplied: rgb already multiplied by alpha. The blend comes from the
// emitter: additive (rgb added to the scene, alpha ignored) or alpha (over the scene).
// Drawn into the HDR scene target (linear values, before exposure and tonemapping), depth
// tested against the meshes, without writing depth.
//
// Until then: a flat dot in the particle's color.

layout(location = 0) in vec2 v_corner; // -1..1 across the quad
layout(location = 1) in vec4 v_color;  // linear RGB (HDR), a = opacity

layout(location = 0) out vec4 fragColor;

void main()
{
    // TODO (Author): a round, soft shape from v_corner; soft particles near surfaces.
    fragColor = vec4(v_color.rgb * v_color.a, v_color.a);
}
