#include "nodelibrary.h"

#include <QFile>
#include <QRegularExpression>
#include <QVector2D>
#include <QVector3D>

const PortDesc *NodeType::input(const QString &name) const
{
    for (const PortDesc &p : inputs)
        if (p.name == name)
            return &p;
    return nullptr;
}

const PortDesc *NodeType::output(const QString &name) const
{
    for (const PortDesc &p : outputs)
        if (p.name == name)
            return &p;
    return nullptr;
}

QVariant TextureValue::toVariant() const
{
    return QVariantMap { { QStringLiteral("image"), image }, { QStringLiteral("linear"), linear } };
}

TextureValue TextureValue::fromVariant(const QVariant &v, bool defaultLinear)
{
    const QVariantMap m = v.toMap();
    TextureValue t;
    t.image = m.value(QStringLiteral("image")).toString();
    t.linear = m.contains(QStringLiteral("linear")) ? m.value(QStringLiteral("linear")).toBool() : defaultLinear;
    return t;
}

namespace
{
// The inputs are the fields of struct Surface in mesh.frag.
NodeType outputNode()
{
    NodeType t;
    t.id = QString::fromLatin1(NodeLibrary::OutputType);
    t.title = QStringLiteral("Material Output");
    t.isOutput = true;
    PortDesc baseColor { QStringLiteral("baseColor"), PortType::Vec3, true, true, QVector3D(0.73f, 0.73f, 0.77f) };
    PortDesc roughness { QStringLiteral("roughness"), PortType::Float, false, true, 0.5f, 0.0, 1.0, true };
    PortDesc metallic { QStringLiteral("metallic"), PortType::Float, false, true, 0.0f, 0.0, 1.0, true };
    // World-space shading normal; unlinked: the interpolated vertex normal.
    PortDesc normal { QStringLiteral("normal"), PortType::Vec3, false, true, QVector3D() };
    normal.expression = QStringLiteral("normalize(v_normal)");
    PortDesc occlusion { QStringLiteral("occlusion"), PortType::Float, false, true, 1.0f, 0.0, 1.0, true };
    PortDesc emission { QStringLiteral("emission"), PortType::Vec3, true, true, QVector3D(0, 0, 0) };
    // Used as the material's alpha mode says (Material::alphaMode): ignored when opaque.
    PortDesc alpha { QStringLiteral("alpha"), PortType::Float, false, true, 1.0f, 0.0, 1.0, true };
    // Blend materials: how much of the light a fully opaque (alpha 1) surface absorbs
    // instead of letting it through, for its translucent shadow (shadow_material.glsl).
    PortDesc absorption { QStringLiteral("absorption"), PortType::Float, false, true, 0.0f, 0.0, 1.0, true };
    // Subsurface scattering, applied by mesh.frag's light loop: how much of the diffuse light
    // scatters inside, its tint, and how far it travels (world units).
    PortDesc subsurface { QStringLiteral("subsurface"), PortType::Float, false, true, 0.0f, 0.0, 1.0, true };
    PortDesc subsurfaceColor { QStringLiteral("subsurfaceColor"), PortType::Vec3, true, true, QVector3D(1.0f, 0.4f, 0.25f) };
    PortDesc subsurfaceRadius { QStringLiteral("subsurfaceRadius"), PortType::Float, false, true, 0.1f, 0.0, 1.0, true };
    t.inputs = { baseColor, roughness, metallic, normal, occlusion, emission, alpha, absorption,
                 subsurface, subsurfaceColor, subsurfaceRadius };
    return t;
}

// Particle graphs: the output takes the acceleration (units / s^2) the forces give the particle.
NodeType particleOutputNode()
{
    NodeType t;
    t.id = QString::fromLatin1(NodeLibrary::OutputType);
    t.title = QStringLiteral("Particle Output");
    t.isOutput = true;
    t.inputs = { { QStringLiteral("acceleration"), PortType::Vec3, false, true, QVector3D() } };
    return t;
}

// Every Force component of the scene, added to the acceleration so far.
NodeType sceneForcesNode()
{
    NodeType t;
    t.id = QStringLiteral("scene_forces");
    t.title = QStringLiteral("Scene Forces");
    t.category = QStringLiteral("Forces");
    t.builtin = true;
    t.inputs = { { QStringLiteral("acceleration"), PortType::Vec3, false, true, QVector3D() } };
    t.outputs = { { QStringLiteral("result"), PortType::Vec3, false, true, QVector3D() } };
    return t;
}

// An entity of the scene, followed through the simulation (animated, moving): its position and
// its axes, to place forces on it.
NodeType objectNode()
{
    NodeType t;
    t.id = QStringLiteral("object");
    t.title = QStringLiteral("Object");
    t.category = QStringLiteral("Input");
    t.builtin = true;
    PortDesc entity { QStringLiteral("object"), PortType::Entity, false, false, 0.0 };
    t.inputs = { entity };
    t.outputs = { { QStringLiteral("position"), PortType::Vec3, false, true, QVector3D() },
                  { QStringLiteral("axis"), PortType::Vec3, false, true, QVector3D() },    // local +Y
                  { QStringLiteral("forward"), PortType::Vec3, false, true, QVector3D() }, // local -Z
                  { QStringLiteral("right"), PortType::Vec3, false, true, QVector3D() } }; // local +X
    return t;
}

// "expr(...)" in a @param spec: removed from `spec`, its content returned (parentheses
// may nest). Empty if there's none.
QString takeExpression(QString &spec, QString *error)
{
    const int start = spec.indexOf(QLatin1String("expr("));
    if (start < 0)
        return QString();
    int depth = 0;
    for (int i = start + 4; i < spec.size(); ++i) {
        if (spec[i] == u'(')
            ++depth;
        else if (spec[i] == u')' && --depth == 0) {
            const QString expr = spec.mid(start + 5, i - start - 5).trimmed();
            spec.remove(start, i - start + 1);
            if (expr.isEmpty())
                *error = QStringLiteral("empty expr()");
            return expr;
        }
    }
    *error = QStringLiteral("expr( without its closing parenthesis");
    return QString();
}

// "// @param factor 0.5 range(0, 1) color fixed" -> fills the port's extra info.
bool applyParam(PortDesc &port, const QString &spec, QString *error)
{
    QString rest = spec;
    port.expression = takeExpression(rest, error);
    if (!error->isEmpty())
        return false;
    if (!port.expression.isEmpty() && port.type == PortType::Texture) {
        *error = QStringLiteral("expr() on a sampler2D");
        return false;
    }
    static const QRegularExpression range(QStringLiteral(R"(range\(\s*([-\d.eE+]+)\s*,\s*([-\d.eE+]+)\s*\))"));
    const QRegularExpressionMatch m = range.match(rest);
    if (m.hasMatch()) {
        port.min = m.captured(1).toDouble();
        port.max = m.captured(2).toDouble();
        port.hasRange = true;
        rest.remove(m.capturedStart(), m.capturedLength());
    }
    std::vector<float> numbers;
    for (const QString &word : rest.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts)) {
        if (word == QLatin1String("color"))
            port.color = true;
        else if (word == QLatin1String("fixed"))
            port.linkable = false;
        else if (port.type == PortType::Texture && word == QLatin1String("linear"))
            port.linear = true;
        else if (port.type == PortType::Texture && word == QLatin1String("white"))
            port.fallback = TextureFallback::White;
        else if (port.type == PortType::Texture && word == QLatin1String("black"))
            port.fallback = TextureFallback::Black;
        else if (port.type == PortType::Texture && word == QLatin1String("gray"))
            port.fallback = TextureFallback::Gray;
        else if (port.type == PortType::Texture && word == QLatin1String("normal")) {
            port.fallback = TextureFallback::Normal;
            port.linear = true; // a normal map is data
        } else {
            bool ok = false;
            const float v = word.toFloat(&ok);
            if (!ok) {
                *error = QStringLiteral("unknown word \"%1\"").arg(word);
                return false;
            }
            numbers.push_back(v);
        }
    }
    if (port.color && port.type != PortType::Vec3) {
        *error = QStringLiteral("\"color\" on a non-vec3");
        return false;
    }
    if (!numbers.empty()) {
        if (port.type == PortType::Float && numbers.size() == 1)
            port.defaultValue = numbers[0];
        else if (port.type == PortType::Vec2 && numbers.size() == 1)
            port.defaultValue = QVector2D(numbers[0], numbers[0]);
        else if (port.type == PortType::Vec2 && numbers.size() == 2)
            port.defaultValue = QVector2D(numbers[0], numbers[1]);
        else if (port.type == PortType::Vec3 && numbers.size() == 1)
            port.defaultValue = QVector3D(numbers[0], numbers[0], numbers[0]);
        else if (port.type == PortType::Vec3 && numbers.size() == 3)
            port.defaultValue = QVector3D(numbers[0], numbers[1], numbers[2]);
        else {
            *error = QStringLiteral("wrong number of default values");
            return false;
        }
    }
    return true;
}
} // namespace

NodeLibrary::NodeLibrary(const QString &glslSource, Kind kind)
    : m_kind(kind)
    , m_source(glslSource)
{
    static const QRegularExpression function(QStringLiteral(R"(void\s+node_(\w+)\s*\(([^)]*)\))"));
    const QStringList lines = glslSource.split('\n');
    QStringList annotations; // "@..." lines collected above the next function
    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines[i].trimmed();
        // A signature over several lines: joined up to its closing parenthesis.
        if (line.startsWith(QLatin1String("void node_")) && !line.contains(u')')) {
            while (i + 1 < lines.size() && !line.contains(u')'))
                line += QLatin1Char(' ') + lines[++i].trimmed();
        }
        if (line.startsWith(QLatin1String("// @"))) {
            annotations.append(line.mid(4).trimmed());
            continue;
        }
        // Functions in comments (e.g. a signature in the docs) aren't nodes.
        const QRegularExpressionMatch fm = line.startsWith(QLatin1String("//")) ? QRegularExpressionMatch()
                                                                                 : function.match(line);
        if (!fm.hasMatch()) {
            if (!line.startsWith(QLatin1String("//")))
                annotations.clear(); // annotations belong to the function right below them
            continue;
        }

        NodeType t;
        t.id = fm.captured(1);
        t.title = t.id;
        QString error;
        for (const QString &param : fm.captured(2).split(',', Qt::SkipEmptyParts)) {
            const QStringList words = param.simplified().split(' ');
            const bool out = words.value(0) == QLatin1String("out");
            const QStringList decl = out || words.value(0) == QLatin1String("in") ? words.mid(1) : words;
            if (decl.size() != 2) {
                error = QStringLiteral("can't read parameter \"%1\"").arg(param.simplified());
                break;
            }
            PortDesc p;
            p.name = decl[1];
            if (decl[0] == QLatin1String("float")) {
                p.type = PortType::Float;
                p.defaultValue = 0.0f;
            } else if (decl[0] == QLatin1String("vec2")) {
                p.type = PortType::Vec2;
                p.defaultValue = QVector2D();
            } else if (decl[0] == QLatin1String("vec3")) {
                p.type = PortType::Vec3;
                p.defaultValue = QVector3D();
            } else if (decl[0] == QLatin1String("sampler2D") && !out) {
                p.type = PortType::Texture;
                p.linkable = false; // textures aren't values that flow between nodes
            } else {
                error = QStringLiteral("parameter type %1 isn't supported (float, vec2, vec3; sampler2D inputs)")
                            .arg(decl[0]);
                break;
            }
            (out ? t.outputs : t.inputs).push_back(p);
        }
        for (const QString &a : annotations) {
            if (!error.isEmpty())
                break;
            if (a.startsWith(QLatin1String("node "))) {
                QString title = a.mid(5).trimmed();
                static const QRegularExpression category(QStringLiteral(R"(\(([^)]*)\)\s*$)"));
                const QRegularExpressionMatch cm = category.match(title);
                if (cm.hasMatch()) {
                    t.category = cm.captured(1).trimmed();
                    title = title.left(cm.capturedStart()).trimmed();
                }
                t.title = title;
            } else if (a.startsWith(QLatin1String("param "))) {
                const QString spec = a.mid(6).trimmed();
                const QString name = spec.section(' ', 0, 0);
                PortDesc *port = nullptr;
                for (PortDesc &p : t.inputs)
                    if (p.name == name)
                        port = &p;
                for (PortDesc &p : t.outputs)
                    if (p.name == name)
                        port = &p;
                if (!port)
                    error = QStringLiteral("@param %1: no such parameter").arg(name);
                else if (!applyParam(*port, spec.section(' ', 1), &error))
                    error = QStringLiteral("@param %1: %2").arg(name, error);
            }
        }
        if (t.outputs.empty() && error.isEmpty())
            error = QStringLiteral("no outputs (out parameters)");
        if (type(t.id) && error.isEmpty())
            error = QStringLiteral("defined twice");
        if (!error.isEmpty())
            m_errors.append(QStringLiteral("node_%1 (line %2): %3").arg(t.id).arg(i + 1).arg(error));
        else
            m_types.push_back(t);
        annotations.clear();
    }
    if (kind == Kind::Particle) {
        m_types.push_back(sceneForcesNode());
        m_types.push_back(objectNode());
        m_types.push_back(particleOutputNode());
    } else {
        m_types.push_back(outputNode());
    }
}

const NodeLibrary &NodeLibrary::instance()
{
    static const NodeLibrary library = [] {
        QFile f(QStringLiteral(":/shadersrc/material_nodes.glsl"));
        return NodeLibrary(f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString());
    }();
    return library;
}

const NodeLibrary &NodeLibrary::particleInstance()
{
    static const NodeLibrary library = [] {
        QFile f(QStringLiteral(":/shadersrc/particle_nodes.glsl"));
        return NodeLibrary(f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString(), Kind::Particle);
    }();
    return library;
}

const NodeType *NodeLibrary::type(const QString &id) const
{
    for (const NodeType &t : m_types)
        if (t.id == id)
            return &t;
    return nullptr;
}
