#include "shadernames.h"

#include <QList>
#include <QRegularExpression>

QShader withReflectedMemberNames(QShader shader)
{
    QList<QByteArray> names;
    for (const QShaderDescription::UniformBlock &block : shader.description().uniformBlocks()) {
        for (const QShaderDescription::BlockVariable &member : block.members)
            names.append(member.name);
    }
    if (names.isEmpty())
        return shader;
    const QList<QShaderKey> keys = shader.availableShaders();
    for (const QShaderKey &key : keys) {
        if (key.source() != QShader::GlslShader)
            continue;
        QShaderCode code = shader.shader(key);
        QString src = QString::fromUtf8(code.shader());
        bool changed = false;
        for (const QByteArray &name : names) {
            const QString n = QRegularExpression::escape(QString::fromUtf8(name));
            // Renamed: no `.name` anywhere, but a `_name` (declaration and accesses).
            const QRegularExpression original(QStringLiteral("\\.") + n + QStringLiteral("\\b"));
            const QRegularExpression renamed(QStringLiteral("\\b_") + n + QStringLiteral("\\b"));
            if (!src.contains(original) && src.contains(renamed)) {
                src.replace(renamed, QString::fromUtf8(name));
                changed = true;
            }
        }
        if (changed) {
            code.setShader(src.toUtf8());
            shader.setShader(key, code);
        }
    }
    return shader;
}
