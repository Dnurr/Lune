#include "gltfmaterialgraph.h"

#include <QHash>
#include <QVector3D>

#include <cmath>

namespace
{
float linearToSRGB(float c)
{
    c = std::max(c, 0.0f);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

QVector3D toSRGB(const QVector3D &linear)
{
    return QVector3D(linearToSRGB(linear.x()), linearToSRGB(linear.y()), linearToSRGB(linear.z()));
}

bool isOne(float v)
{
    return std::abs(v - 1.0f) < 1e-4f;
}

// Builds the graph column by column, right to left: Material Output, then the nodes
// feeding it. Texture nodes are shared by image (e.g. one ORM image for occlusion,
// roughness and metallic).
class Builder
{
public:
    Builder(const NodeLibrary &library)
        : m_library(library)
    {
        m_output = m_graph.addNode(QString::fromLatin1(NodeLibrary::OutputType), QPointF(260, 0));
    }

    bool has(const QString &type) const { return m_library.type(type) != nullptr; }

    void setValue(int node, const QString &port, const QVariant &value) { m_graph.node(node)->values[port] = value; }
    void setOutput(const QString &port, const QVariant &value) { setValue(m_output, port, value); }
    void link(int from, const QString &fromPort, int to, const QString &toPort)
    {
        m_graph.connect(from, fromPort, to, toPort);
    }
    int output() const { return m_output; }

    int add(const QString &type, int column)
    {
        const QPointF position(260 - 260 * column, m_rows[column]++ * 150 - 150);
        return m_graph.addNode(type, position);
    }

    // The Image Texture node for an image (created once).
    int texture(const QString &image, bool linear)
    {
        const QString key = image + (linear ? QStringLiteral("|data") : QString());
        if (m_textures.contains(key))
            return m_textures.value(key);
        const int id = add(QStringLiteral("image_texture"), 3);
        m_graph.node(id)->values[QStringLiteral("image")] = TextureValue { image, linear }.toVariant();
        m_textures.insert(key, id);
        return id;
    }

    // Separate RGB of a texture (created once per texture node).
    int separate(int textureNode)
    {
        if (m_separates.contains(textureNode))
            return m_separates.value(textureNode);
        const int id = add(QStringLiteral("separate_rgb"), 2);
        link(textureNode, QStringLiteral("color"), id, QStringLiteral("color"));
        m_separates.insert(textureNode, id);
        return id;
    }

    // `source` output times a constant: a Multiply node, or the source itself for 1.
    void scaledToOutput(int source, const QString &sourcePort, float factor, const QString &outputPort)
    {
        if (std::abs(factor) < 1e-6f) { // times 0: just the value
            setOutput(outputPort, 0.0f);
            return;
        }
        if (isOne(factor) || !has(QStringLiteral("multiply"))) {
            link(source, sourcePort, m_output, outputPort);
            return;
        }
        const int mul = add(QStringLiteral("multiply"), 1);
        link(source, sourcePort, mul, QStringLiteral("a"));
        m_graph.node(mul)->values[QStringLiteral("b")] = factor;
        link(mul, QStringLiteral("result"), m_output, outputPort);
    }

    // `source` color output times a color factor (sRGB in the graph).
    void tintedToOutput(int source, const QString &sourcePort, const QVector3D &linearFactor, const QString &outputPort)
    {
        const bool white = isOne(linearFactor.x()) && isOne(linearFactor.y()) && isOne(linearFactor.z());
        if (white || !has(QStringLiteral("multiply_color"))) {
            link(source, sourcePort, m_output, outputPort);
            return;
        }
        const int mul = add(QStringLiteral("multiply_color"), 1);
        link(source, sourcePort, mul, QStringLiteral("a"));
        m_graph.node(mul)->values[QStringLiteral("b")] = toSRGB(linearFactor);
        link(mul, QStringLiteral("result"), m_output, outputPort);
    }

    NodeGraph graph() const { return m_graph; }

private:
    const NodeLibrary &m_library;
    NodeGraph m_graph;
    int m_output = 0;
    QHash<int, int> m_rows; // nodes placed per column
    QHash<QString, int> m_textures;
    QHash<int, int> m_separates;
};
} // namespace

NodeGraph graphFromGltf(const GltfMaterial &m, const NodeLibrary &library)
{
    Builder b(library);
    const bool textures = b.has(QStringLiteral("image_texture"));
    const bool separate = textures && b.has(QStringLiteral("separate_rgb"));

    // Base color
    const QVector3D baseFactor = m.baseColorFactor.toVector3D();
    if (textures && !m.baseColorTexture.isEmpty())
        b.tintedToOutput(b.texture(m.baseColorTexture, false), QStringLiteral("color"), baseFactor, QStringLiteral("baseColor"));
    else
        b.setOutput(QStringLiteral("baseColor"), toSRGB(baseFactor));

    // Roughness, metallic: G and B of the metallic-roughness texture
    if (separate && !m.metallicRoughnessTexture.isEmpty()) {
        const int rgb = b.separate(b.texture(m.metallicRoughnessTexture, true));
        b.scaledToOutput(rgb, QStringLiteral("g"), m.roughnessFactor, QStringLiteral("roughness"));
        b.scaledToOutput(rgb, QStringLiteral("b"), m.metallicFactor, QStringLiteral("metallic"));
    } else {
        b.setOutput(QStringLiteral("roughness"), m.roughnessFactor);
        b.setOutput(QStringLiteral("metallic"), m.metallicFactor);
    }

    // Occlusion: R
    if (separate && !m.occlusionTexture.isEmpty()) {
        const int rgb = b.separate(b.texture(m.occlusionTexture, true));
        b.link(rgb, QStringLiteral("r"), b.output(), QStringLiteral("occlusion"));
    }

    // Normal map, through the Author's Normal Map node when there is one
    if (textures && !m.normalTexture.isEmpty()) {
        const int tex = b.texture(m.normalTexture, true);
        const NodeType *normalMap = library.type(QStringLiteral("normal_map"));
        if (normalMap && normalMap->input(QStringLiteral("color")) && normalMap->output(QStringLiteral("normal"))) {
            const int nm = b.add(QStringLiteral("normal_map"), 1);
            b.link(tex, QStringLiteral("color"), nm, QStringLiteral("color"));
            if (normalMap->input(QStringLiteral("strength")))
                b.setValue(nm, QStringLiteral("strength"), m.normalScale);
            b.link(nm, QStringLiteral("normal"), b.output(), QStringLiteral("normal"));
        }
    }

    // Emission
    if (!m.emissiveFactor.isNull()) {
        if (textures && !m.emissiveTexture.isEmpty())
            b.tintedToOutput(b.texture(m.emissiveTexture, false), QStringLiteral("color"), m.emissiveFactor,
                             QStringLiteral("emission"));
        else
            b.setOutput(QStringLiteral("emission"), toSRGB(m.emissiveFactor));
    }

    return b.graph();
}
