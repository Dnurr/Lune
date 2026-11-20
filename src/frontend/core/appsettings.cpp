#include "appsettings.h"

#include <QSettings>
#include <algorithm>

namespace
{
const char *kBackendKey = "graphics/backend";

struct BackendName
{
    QRhi::Implementation backend;
    const char *key;   // in the settings
    const char *label; // in the Preferences
};

const BackendName kBackends[] = {
    { QRhi::D3D11, "d3d11", "Direct3D 11" },
    { QRhi::D3D12, "d3d12", "Direct3D 12 (no fluid volumes)" },
    { QRhi::Vulkan, "vulkan", "Vulkan" },
    { QRhi::Metal, "metal", "Metal" },
    { QRhi::OpenGLES2, "opengl", "OpenGL" },
};

QRhi::Implementation platformDefault()
{
#if defined(Q_OS_WIN)
    return QRhi::D3D11;
#elif defined(Q_OS_MACOS)
    return QRhi::Metal;
#else
    return QRhi::OpenGLES2;
#endif
}
} // namespace

namespace AppSettings
{
std::vector<QRhi::Implementation> availableBackends()
{
    std::vector<QRhi::Implementation> out;
#if defined(Q_OS_WIN)
    out.push_back(QRhi::D3D11);
    out.push_back(QRhi::D3D12);
#endif
#if defined(Q_OS_MACOS)
    out.push_back(QRhi::Metal);
#endif
    // Vulkan as ImageRenderSession can create it (Qt built with Vulkan, headers at build time).
#if QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>)
    out.push_back(QRhi::Vulkan);
#endif
    out.push_back(QRhi::OpenGLES2);
    return out;
}

QString backendLabel(QRhi::Implementation backend)
{
    for (const BackendName &b : kBackends) {
        if (b.backend == backend)
            return QString::fromLatin1(b.label);
    }
    return QString::fromLatin1(QRhi::backendName(backend));
}

QRhi::Implementation backend()
{
    const QString key = QSettings().value(QString::fromLatin1(kBackendKey)).toString();
    const std::vector<QRhi::Implementation> available = availableBackends();
    for (const BackendName &b : kBackends) {
        if (key == QLatin1String(b.key) && std::find(available.begin(), available.end(), b.backend) != available.end())
            return b.backend;
    }
    return platformDefault();
}

void setBackend(QRhi::Implementation backend)
{
    for (const BackendName &b : kBackends) {
        if (b.backend == backend)
            QSettings().setValue(QString::fromLatin1(kBackendKey), QString::fromLatin1(b.key));
    }
}
} // namespace AppSettings
