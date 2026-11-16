#version 440

// Front-depth pass (ShadowMaps): writes the linear depth into alpha, where the blend keeps
// the smallest (nearest) one; rgb = 1 leaves the color untouched.
layout(location = 0) flat in vec3 v_view; // x: 1 for a point light's face, y: near, z: far

layout(location = 0) out vec4 fragColor;

void main()
{
    // Linear depth 0..1, as in shadow_material.glsl: orthographic depth is already linear;
    // a point light's perspective depth becomes the distance along the face's axis / far.
    float depth = gl_FragCoord.z;
    if (v_view.x > 0.5) {
        float near = v_view.y, far = v_view.z;
        depth = near / (far - depth * (far - near));
    }
    fragColor = vec4(1.0, 1.0, 1.0, depth);
}
