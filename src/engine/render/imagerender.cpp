#include "imagerender.h"

#include "engine/scene/scene.h"

#include <QElapsedTimer>
#include <QOffscreenSurface>
#include <QThread>
#include <QWindow>
#include <rhi/qrhi_platform.h>

#include <memory>

namespace
{
constexpr qint64 kTextureWaitMs = 30000; // give up waiting for textures after this long

ViewSettings view(Shading shading, DebugView debug = DebugView::None, bool wireOverlay = false)
{
    ViewSettings v;
    v.shading = shading;
    v.debug = debug;
    v.wireOverlay = wireOverlay;
    return v;
}

std::unique_ptr<QRhi> createRhi(QRhi::Implementation backend, QWindow *window,
                                std::unique_ptr<QOffscreenSurface> &surface)
{
    QRhi *rhi = nullptr;
    switch (backend) {
#ifdef Q_OS_WIN
    case QRhi::D3D11: {
        QRhiD3D11InitParams params;
        rhi = QRhi::create(QRhi::D3D11, &params);
        break;
    }
    case QRhi::D3D12: {
        QRhiD3D12InitParams params;
        rhi = QRhi::create(QRhi::D3D12, &params);
        break;
    }
#endif
#if QT_CONFIG(vulkan) && __has_include(<vulkan/vulkan.h>)
    case QRhi::Vulkan: {
        QVulkanInstance *instance = window ? window->vulkanInstance() : nullptr;
        static QVulkanInstance ownInstance; // when the window has none
        if (!instance) {
            if (!ownInstance.isValid())
                ownInstance.create();
            instance = ownInstance.isValid() ? &ownInstance : nullptr;
        }
        if (instance) {
            QRhiVulkanInitParams params;
            params.inst = instance;
            rhi = QRhi::create(QRhi::Vulkan, &params);
        }
        break;
    }
#endif
#if QT_CONFIG(opengl)
    case QRhi::OpenGLES2: {
        surface.reset(QRhiGles2InitParams::newFallbackSurface());
        QRhiGles2InitParams params;
        params.fallbackSurface = surface.get();
        rhi = QRhi::create(QRhi::OpenGLES2, &params);
        break;
    }
#endif
#if QT_CONFIG(metal)
    case QRhi::Metal: {
        QRhiMetalInitParams params;
        rhi = QRhi::create(QRhi::Metal, &params);
        break;
    }
#endif
    default:
        break;
    }
    Q_UNUSED(window);
    return std::unique_ptr<QRhi>(rhi);
}
} // namespace

const std::vector<ImagePass> &imagePasses()
{
    static const std::vector<ImagePass> passes = {
        { QStringLiteral("rendered"), QStringLiteral("Rendered"), view(Shading::Rendered) },
        { QStringLiteral("wireframe"), QStringLiteral("Wireframe"), view(Shading::Wireframe) },
        { QStringLiteral("clay"), QStringLiteral("Clay"), view(Shading::Clay) },
        { QStringLiteral("unlit"), QStringLiteral("Unlit (base color)"), view(Shading::Unlit) },
        { QStringLiteral("preview"), QStringLiteral("Preview (environment light)"), view(Shading::Preview) },
        { QStringLiteral("wire_over"), QStringLiteral("Rendered + wireframe"), view(Shading::Rendered, DebugView::None, true) },
        { QStringLiteral("normals"), QStringLiteral("Normals"), view(Shading::Rendered, DebugView::Normals) },
        { QStringLiteral("depth"), QStringLiteral("Depth"), view(Shading::Rendered, DebugView::Depth) },
        { QStringLiteral("direct"), QStringLiteral("Direct light only"), view(Shading::Rendered, DebugView::DirectOnly) },
        { QStringLiteral("shadows"), QStringLiteral("Shadows only"), view(Shading::Rendered, DebugView::ShadowsOnly) },
    };
    return passes;
}

const ImagePass *imagePass(const QString &id)
{
    for (const ImagePass &p : imagePasses())
        if (p.id == id)
            return &p;
    return nullptr;
}

std::vector<QImage> renderImages(QRhi::Implementation backend, QWindow *window, const Scene &scene, OrbitCamera camera,
                                 const ImageRenderRequest &request, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return std::vector<QImage>();
    };
    if (request.size.isEmpty())
        return fail(QStringLiteral("The image size is empty."));

    std::unique_ptr<QOffscreenSurface> surface; // OpenGL only; outlives the QRhi
    std::unique_ptr<QRhi> rhi = createRhi(backend, window, surface);
    if (!rhi)
        return fail(QStringLiteral("Couldn't create a %1 device for rendering.").arg(QString::fromLatin1(QRhi::backendName(backend))));
    if (request.size.width() > rhi->resourceLimit(QRhi::TextureSizeMax)
        || request.size.height() > rhi->resourceLimit(QRhi::TextureSizeMax))
        return fail(QStringLiteral("The image is larger than the GPU allows (%1 pixels).").arg(rhi->resourceLimit(QRhi::TextureSizeMax)));

    std::vector<QImage> images;
    {
        std::unique_ptr<QRhiTexture> texture(
            rhi->newTexture(QRhiTexture::RGBA8, request.size, 1, QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
        if (!texture->create())
            return fail(QStringLiteral("Couldn't create a %1 x %2 image (out of video memory?).")
                            .arg(request.size.width())
                            .arg(request.size.height()));
        std::unique_ptr<QRhiTextureRenderTarget> target(rhi->newTextureRenderTarget({ QRhiColorAttachment(texture.get()) }));
        std::unique_ptr<QRhiRenderPassDescriptor> rp(target->newCompatibleRenderPassDescriptor());
        target->setRenderPassDescriptor(rp.get());
        target->create();

        camera.shiftX = 0.0f; // centered: the panels' lens shift is for the viewport only
        Renderer renderer;
        renderer.initialize(rhi.get(), target.get());
        renderer.setGridLinesVisible(false);
        renderer.setDisplayParams(request.exposure, request.tonemapper);

        // One offscreen frame; with `readback`, the image is copied into `out`.
        auto frame = [&](bool readback, QImage *out) {
            QRhiCommandBuffer *cb = nullptr;
            if (rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess)
                return false;
            renderer.render(cb, target.get(), camera, &scene, InvalidEntity);
            QRhiReadbackResult result;
            if (readback) {
                QRhiResourceUpdateBatch *rub = rhi->nextResourceUpdateBatch();
                rub->readBackTexture({ texture.get() }, &result);
                cb->resourceUpdate(rub);
            }
            if (rhi->endOffscreenFrame() != QRhi::FrameOpSuccess) // waits for the GPU
                return false;
            if (readback) {
                QImage image(reinterpret_cast<const uchar *>(result.data.constData()), result.pixelSize.width(),
                             result.pixelSize.height(), QImage::Format_RGBA8888);
                // Rows come bottom-up where framebuffers are y-up (OpenGL). Opaque: the
                // composite pass's alpha means nothing.
                image = rhi->isYUpInFramebuffer() ? image.mirrored() : image.copy();
                *out = image.convertToFormat(QImage::Format_RGB32);
            }
            return true;
        };

        for (ViewSettings v : request.views) {
            v.skybox = request.skybox;
            v.post = request.post;
            renderer.setViewSettings(v);
            QElapsedTimer waiting;
            waiting.start();
            bool ok = frame(false, nullptr);
            while (ok && renderer.needsAnotherFrame() && waiting.elapsed() < kTextureWaitMs) {
                QThread::msleep(5);
                ok = frame(false, nullptr);
            }
            QImage image;
            if (!ok || !frame(true, &image) || image.isNull()) {
                renderer.release();
                return fail(QStringLiteral("Rendering failed (the GPU device was lost?)."));
            }
            images.push_back(image);
        }
        renderer.release();
    }
    return images;
}
