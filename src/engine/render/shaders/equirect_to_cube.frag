#version 440

// Environment, step 1: the equirectangular source image -> one face of the environment cubemap.
// Runs once per face when the source changes (IblMaps), with cube.vert.
layout(location = 0) in vec3 v_dir; // direction of this texel, not normalized
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform cubeBuf {
    vec4 face; // x: face (0..5), y: flip V (used by cube.vert), z: unused, w: source width in pixels
};
// Equirectangular radiance, linear. U wraps around (Repeat), V = 0 is the top row:
// straight up (+Y). Y is up in the world.
layout(binding = 1) uniform sampler2D equirect;

const float PI = 3.14159265359;

void main()
{
    vec3 dir = normalize(v_dir);

    // TODO (author): turn `dir` into equirectangular (u, v) and sample `equirect`.
    // The longitude around Y gives u, the angle from +Y gives v.
    fragColor = vec4(0.0, 0.0, 0.0, 1.0);
}
