#pragma once

#include <QString>
#include <QStringList>
#include <QVariant>
#include <vector>

// The node types a material graph can use.
//
// They're read from GLSL (material_nodes.glsl): every function named node_<id> is a node
// type. Its parameters are the ports: plain ones are inputs, `out` ones are outputs;
// float, vec2 and vec3 are supported, and sampler2D for inputs (an image picked on the
// node). Comment lines starting with "// @" right above the function describe it:
//
//   // @node Mix (Color)                    title, and (optional) the category in the Add menu
//   // @param a 0.8 0.8 0.8 color           default value; `color`: a vec3 shown as a color
//   // @param factor 0.5 range(0, 1)        `range(min, max)`: slider limits
//   // @param seed 3 fixed                  `fixed`: a setting on the node, not linkable
//   // @param uv expr(v_uv)                 unlinked: this GLSL expression (no value to edit)
//   // @param image normal linear           sampler2D: the texture without an image (white,
//   //                                      black, gray or normal); `linear`: data, not color
//   // @param result color                  outputs: only `color` matters
//   void node_mix(vec3 a, vec3 b, float factor, out vec3 result) { result = mix(a, b, factor); }
//
// Inputs without a default get 0. The Material Output node ("output") is built in: its
// inputs are the fields of the Surface struct in mesh.frag.
enum class PortType
{
    Float,
    Vec2,
    Vec3,
    Texture, // sampler2D: an image, never linked
};

// What a texture input samples when it has no image (or until the image is loaded).
enum class TextureFallback
{
    White,
    Black,
    Gray,   // 0.5, linear
    Normal, // flat tangent-space normal (0.5, 0.5, 1), linear
};

// The value of a texture input. Stored in GraphNode::values as a QVariantMap
// { "image": reference, "linear": bool }.
struct TextureValue
{
    // A file path, or "<gltf file>#image<N>" for an image inside a glTF; empty = none.
    QString image;
    // Data (normal, roughness... maps): the texels are used as they are. Otherwise a color
    // image: sRGB texels, converted to linear when sampled.
    bool linear = false;

    bool operator==(const TextureValue &) const = default;
    QVariant toVariant() const;
    static TextureValue fromVariant(const QVariant &v, bool defaultLinear);
};

struct PortDesc
{
    QString name;
    PortType type = PortType::Float;
    bool color = false;    // vec3 holding a color (sRGB in the editor, linear in the shader)
    bool linkable = true;  // false: a fixed setting
    QVariant defaultValue; // float, QVector2D or QVector3D
    double min = 0.0, max = 1.0; // slider range, when hasRange
    bool hasRange = false;
    // Inputs: GLSL used when unlinked instead of a value (e.g. "v_uv"); no value to edit.
    QString expression;
    // Texture inputs: what to sample without an image, and whether images are data.
    TextureFallback fallback = TextureFallback::White;
    bool linear = false;

    bool hasValue() const { return expression.isEmpty() && type != PortType::Texture; }
};

struct NodeType
{
    QString id;       // "mix": the GLSL function is node_mix
    QString title;    // "Mix"
    QString category; // "Color"; empty = top level of the Add menu
    std::vector<PortDesc> inputs;
    std::vector<PortDesc> outputs;
    bool isOutput = false; // the Material Output node

    const PortDesc *input(const QString &name) const;
    const PortDesc *output(const QString &name) const;
};

class NodeLibrary
{
public:
    static constexpr const char *OutputType = "output";

    // Parses node types out of GLSL source (see above) and adds the built-in Material
    // Output. Problems (a malformed annotation, an unsupported parameter type) skip that
    // node and are listed in errors().
    explicit NodeLibrary(const QString &glslSource = QString());

    // The library of the running editor: material_nodes.glsl from the resources.
    static const NodeLibrary &instance();

    const std::vector<NodeType> &types() const { return m_types; }
    const NodeType *type(const QString &id) const;
    const QString &source() const { return m_source; } // the GLSL functions, put in every material shader
    const QStringList &errors() const { return m_errors; }

private:
    std::vector<NodeType> m_types;
    QString m_source;
    QStringList m_errors;
};
