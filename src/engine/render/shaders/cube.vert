#version 440

// Fullscreen triangle over one face of a cubemap (IblMaps): gives each fragment the
// direction from the cube's center through its texel, so the fragment shader only has
// to compute the value for that direction (normalize v_dir first).
//
// Faces are in the usual cubemap order (+X, -X, +Y, -Y, +Z, -Z) and orientation, so
// texture(cubeMap, dir) later reads back exactly what was written for dir.
layout(location = 0) out vec3 v_dir;

layout(std140, binding = 0) uniform cubeBuf {
    vec4 face; // x: face (0..5), y: 1 = flip V, z: roughness (prefilter), w: input size in pixels
};

vec3 faceDirection(int index, vec2 uv)
{
    vec2 c = uv * 2.0 - 1.0; // -1..1 across the face, V going down the texture rows
    switch (index) {
    case 0: return vec3(1.0, -c.y, -c.x);  // +X
    case 1: return vec3(-1.0, -c.y, c.x);  // -X
    case 2: return vec3(c.x, 1.0, c.y);    // +Y
    case 3: return vec3(c.x, -1.0, -c.y);  // -Y
    case 4: return vec3(c.x, -c.y, 1.0);   // +Z
    default: return vec3(-c.x, -c.y, -1.0); // -Z
    }
}

void main()
{
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2); // (0,0) (2,0) (0,2)
    vec2 uv = face.y > 0.5 ? vec2(p.x, 1.0 - p.y) : p;
    // The direction is linear in uv, so interpolating it across the triangle is exact.
    v_dir = faceDirection(int(face.x), uv);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
