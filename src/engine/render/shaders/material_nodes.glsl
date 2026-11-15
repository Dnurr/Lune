// Material nodes: every function named node_<id> is a node type in the node editor
// (see engine/material/nodelibrary.h for the full rules).
//
//   - parameters are the ports: plain ones are inputs, `out` ones are outputs
//   - types: float and vec3; the signature must fit on one line
//   - "// @" lines right above the function describe it:
//       // @node Title (Category)
//       // @param name <default> [range(min, max)] [color] [fixed]
//
// These functions are pasted into every material shader (before materialSurface()), so
// they can use anything mesh.frag declares above the material markers: v_world, v_normal,
// the uniforms, the samplers.
//
// Colors arrive linear (the editor converts the sRGB values it shows).

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

// ---------------------------------------------------------------------------
// Math (Claude: plumbing examples)
// ---------------------------------------------------------------------------

// @node Add (Math)
void node_add(float a, float b, out float result) { result = a + b; }

// @node Multiply (Math)
// @param a 1
// @param b 1
void node_multiply(float a, float b, out float result) { result = a * b; }

// @node Mix (Color)
// @param a 0.8 0.8 0.8 color
// @param b 0.2 0.2 0.2 color
// @param factor 0.5 range(0, 1)
// @param result color
void node_mix(vec3 a, vec3 b, float factor, out vec3 result) { result = mix(a, b, factor); }

// ---------------------------------------------------------------------------
// Shading nodes (Author): textures, noise, fresnel, layering...
// ---------------------------------------------------------------------------
