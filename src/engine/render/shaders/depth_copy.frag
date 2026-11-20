#version 440

// Copies a depth texture into a color one (R32F), texel for texel: ShadowMaps on OpenGL,
// where a texture's comparison mode belongs to the texture, not to the sampler, so the
// atlases can't be read through a comparison sampler and as raw depth in the same draw.
// mesh.frag reads the raw depth from the copy instead. Drawn with fullscreen.vert over
// a target the size of the source.
layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform sampler2D depthMap;

void main()
{
    fragColor = vec4(texelFetch(depthMap, ivec2(gl_FragCoord.xy), 0).r, 0.0, 0.0, 1.0);
}
