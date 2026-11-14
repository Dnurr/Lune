#version 440

// Environment as the viewport background, drawn behind the scene when the Environment's
// "background" option is on (the grid is drawn on top of it).
layout(location = 0) in vec3 v_dir; // world direction of the view ray, not normalized
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform backgroundBuf {
    mat4 invViewProj; // used by background.vert
    vec4 cameraPos;
    vec4 params;      // x: NDC y sign (background.vert)
};
// Shared with mesh.frag: see environmentBuf there.
layout(std140, binding = 1) uniform environmentBuf {
    mat4 envRotation; // world direction -> environment direction (mat3(envRotation) * dir)
    vec4 envParams;   // x: intensity, y: prefiltered max mip (levels - 1), z: background blur 0..1
};
layout(binding = 2) uniform samplerCube environment; // sharp, full mip chain
layout(binding = 3) uniform samplerCube prefiltered; // blurred by roughness, one roughness per mip

void main()
{
    vec3 dir = mat3(envRotation) * normalize(v_dir);
    vec3 color;
    if (envParams.z > 0.0)
        color = textureLod(prefiltered, dir, envParams.z * envParams.y).rgb;
    else
        color = texture(environment, dir).rgb;

    // With blur > 0, the prefiltered map gives a smooth blur (blur as roughness -> mip).
    fragColor = vec4(color * envParams.x, 1.0);
}
