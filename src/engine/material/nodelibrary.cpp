#include "nodelibrary.h"

#include <QFile>
#include <QRegularExpression>
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

namespace
{
NodeType outputNode()
{
    NodeType t;
    t.id = QString::fromLatin1(NodeLibrary::OutputType);
    t.title = QStringLiteral("Material Output");
    t.isOutput = true;
    PortDesc baseColor { QStringLiteral("baseColor"), PortType::Vec3, true, true, QVector3D(0.73f, 0.73f, 0.77f) };
    PortDesc roughness { QStringLiteral("roughness"), PortType::Float, false, true, 0.5f, 0.0, 1.0, true };
    PortDesc metallic { QStringLiteral("metallic"), PortType::Float, false, true, 0.0f, 0.0, 1.0, true };
    t.inputs = { baseColor, roughness, metallic };
    return t;
}

// "// @param factor 0.5 range(0, 1) color fixed" -> fills the port's extra info.
bool applyParam(PortDesc &port, const QString &spec, QString *error)
{
    QString rest = spec;
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
        else {
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
        *error = QStringLiteral("\"color\" on a float");
        return false;
    }
    if (!numbers.empty()) {
        if (port.type == PortType::Float && numbers.size() == 1)
            port.defaultValue = numbers[0];
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

NodeLibrary::NodeLibrary(const QString &glslSource)
    : m_source(glslSource)
{
    static const QRegularExpression function(QStringLiteral(R"(void\s+node_(\w+)\s*\(([^)]*)\))"));
    const QStringList lines = glslSource.split('\n');
    QStringList annotations; // "@..." lines collected above the next function
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines[i].trimmed();
        if (line.startsWith(QLatin1String("// @"))) {
            annotations.append(line.mid(4).trimmed());
            continue;
        }
        const QRegularExpressionMatch fm = function.match(line);
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
            } else if (decl[0] == QLatin1String("vec3")) {
                p.type = PortType::Vec3;
                p.defaultValue = QVector3D();
            } else {
                error = QStringLiteral("parameter type %1 isn't supported (float, vec3)").arg(decl[0]);
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
    m_types.push_back(outputNode());
}

const NodeLibrary &NodeLibrary::instance()
{
    static const NodeLibrary library = [] {
        QFile f(QStringLiteral(":/shadersrc/material_nodes.glsl"));
        return NodeLibrary(f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString());
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
