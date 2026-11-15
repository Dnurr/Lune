// Material nodes: every function named node_<id> is a node type in the node editor
// (see engine/material/nodelibrary.h for the full rules).
//
//   - parameters are the ports: plain ones are inputs, `out` ones are outputs
//   - types: float, vec2, vec3; sampler2D inputs are images picked on the node
//   - the signature must fit on one line
//   - "// @" lines right above the function describe it:
//       // @node Title (Category)
//       // @param name <default> [range(min, max)] [color] [fixed]
//       // @param name expr(<glsl>)                 unlinked: this expression, e.g. expr(v_uv)
//       // @param image [white|black|gray|normal] [linear]   sampler2D: fallback, data image
//
// These functions are pasted into every material shader (before materialSurface()), so
// they can use anything mesh.frag declares above the material markers: v_world, v_normal,
// v_uv, v_tangent, the uniforms, the samplers.
//
// Colors arrive linear (the editor converts the sRGB values it shows; sRGB images are
// converted by the sampler).

// ---------------------------------------------------------------------------
// Inputs (Claude: plumbing examples)
// ---------------------------------------------------------------------------

// @node Value (Input)
// @param value 0.5 fixed
void node_value(float value, out float result) { result = value; }

// @node Color (Input)
// @param value 0.8 0.8 0.8 color fixed
// @param result color
void node_color(vec3 value, out vec3 result) { result = value; }

// @node Texture Coordinate (Input)
void node_texcoord(out vec2 uv) { uv = v_uv; }

// ---------------------------------------------------------------------------
// Textures (Claude: plumbing examples)
// ---------------------------------------------------------------------------

// @node Image Texture (Texture)
// @param image white
// @param uv expr(v_uv)
// @param color color
void node_image_texture(sampler2D image, vec2 uv, out vec3 color, out float alpha)
{
    vec4 t = texture(image, uv);
    color = t.rgb;
    alpha = t.a;
}

// ---------------------------------------------------------------------------
// Math (Claude: plumbing examples)
// ---------------------------------------------------------------------------

// @node Add (Math)
void node_add(float a, float b, out float result) { result = a + b; }

// @node Multiply (Math)
// @param a 1
// @param b 1
void node_multiply(float a, float b, out float result) { result = a * b; }

// @node Multiply Color (Color)
// @param a 1 color
// @param b 1 color
// @param result color
void node_multiply_color(vec3 a, vec3 b, out vec3 result) { result = a * b; }

// @node Separate RGB (Color)
// @param color 0.8 color
void node_separate_rgb(vec3 color, out float r, out float g, out float b) { r = color.r; g = color.g; b = color.b; }

// @node Mix (Color)
// @param a 0.8 0.8 0.8 color
// @param b 0.2 0.2 0.2 color
// @param factor 0.5 range(0, 1)
// @param result color
void node_mix(vec3 a, vec3 b, float factor, out vec3 result) { result = mix(a, b, factor); }

// ---------------------------------------------------------------------------
// Shading nodes (Author): normal map, noise, fresnel, triplanar, layering...
//
// The glTF importer links normal maps through a node with this signature, if it exists
// (otherwise it leaves the normal map's Image Texture unlinked):
//   void node_normal_map(vec3 color, float strength, out vec3 normal)
//   color: the texel of a tangent-space normal map (0..1, linear); strength: glTF's scale;
//   normal: world space, normalized, for Material Output's normal.
// ---------------------------------------------------------------------------
