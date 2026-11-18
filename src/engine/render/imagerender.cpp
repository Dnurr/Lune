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

struct ImageRenderSession::Impl
{
    std::unique_ptr<QOffscreenSurface> surface; // OpenGL only; outlives the QRhi
    std::unique_ptr<QRhi> rhi;
    std::unique_ptr<QRhiTexture> texture;
    std::unique_ptr<QRhiTextureRenderTarget> target;
    std::unique_ptr<QRhiRenderPassDescriptor> rp;
    Renderer renderer;
    bool rendererReady = false;

    // One offscreen frame; with `out`, the image is read back into it.
    bool frame(const Scene &scene, const OrbitCamera &camera, QImage *out)
    {
        QRhiCommandBuffer *cb = nullptr;
        if (rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess)
            return false;
        renderer.render(cb, target.get(), camera, &scene, InvalidEntity);
        QRhiReadbackResult result;
        if (out) {
            QRhiResourceUpdateBatch *rub = rhi->nextResourceUpdateBatch();
            rub->readBackTexture({ texture.get() }, &result);
            cb->resourceUpdate(rub);
        }
        if (rhi->endOffscreenFrame() != QRhi::FrameOpSuccess) // waits for the GPU
            return false;
        if (out) {
            QImage image(reinterpret_cast<const uchar *>(result.data.constData()), result.pixelSize.width(),
                         result.pixelSize.height(), QImage::Format_RGBA8888);
            // Rows come bottom-up where framebuffers are y-up (OpenGL). Opaque: the
            // composite pass's alpha means nothing.
            image = rhi->isYUpInFramebuffer() ? image.mirrored() : image.copy();
            *out = image.convertToFormat(QImage::Format_RGB32);
        }
        return true;
    }
};

ImageRenderSession::ImageRenderSession() = default;

ImageRenderSession::~ImageRenderSession()
{
    end();
}

bool ImageRenderSession::begin(QRhi::Implementation backend, QWindow *window, const QSize &size, float exposure,
                               int tonemapper, QString *error)
{
    end();
    auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        end();
        return false;
    };
    if (size.isEmpty())
        return fail(QStringLiteral("The image size is empty."));
    d = std::make_unique<Impl>();
    d->rhi = createRhi(backend, window, d->surface);
    if (!d->rhi)
        return fail(QStringLiteral("Couldn't create a %1 device for rendering.").arg(QString::fromLatin1(QRhi::backendName(backend))));
    const int maxSize = d->rhi->resourceLimit(QRhi::TextureSizeMax);
    if (size.width() > maxSize || size.height() > maxSize)
        return fail(QStringLiteral("The image is larger than the GPU allows (%1 pixels).").arg(maxSize));
    d->texture.reset(d->rhi->newTexture(QRhiTexture::RGBA8, size, 1, QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
    if (!d->texture->create())
        return fail(QStringLiteral("Couldn't create a %1 x %2 image (out of video memory?).").arg(size.width()).arg(size.height()));
    d->target.reset(d->rhi->newTextureRenderTarget({ QRhiColorAttachment(d->texture.get()) }));
    d->rp.reset(d->target->newCompatibleRenderPassDescriptor());
    d->target->setRenderPassDescriptor(d->rp.get());
    d->target->create();
    d->renderer.initialize(d->rhi.get(), d->target.get());
    d->rendererReady = true;
    d->renderer.setGridLinesVisible(false);
    d->renderer.setDisplayParams(exposure, tonemapper);
    return true;
}

QImage ImageRenderSession::render(const Scene &scene, OrbitCamera camera, const ViewSettings &view, float seconds,
                                  QString *error)
{
    if (!d || !d->rendererReady) {
        if (error)
            *error = QStringLiteral("The render device isn't ready.");
        return {};
    }
    camera.shiftX = 0.0f; // centered: the panels' lens shift is for the viewport only
    d->renderer.setViewSettings(view);
    d->renderer.setTime(seconds);
    QElapsedTimer waiting;
    waiting.start();
    bool ok = d->frame(scene, camera, nullptr);
    while (ok && d->renderer.needsAnotherFrame() && waiting.elapsed() < kTextureWaitMs) {
        QThread::msleep(5);
        ok = d->frame(scene, camera, nullptr);
    }
    QImage image;
    if (!ok || !d->frame(scene, camera, &image) || image.isNull()) {
        if (error)
            *error = QStringLiteral("Rendering failed (the GPU device was lost?).");
        return {};
    }
    return image;
}

void ImageRenderSession::end()
{
    if (!d)
        return;
    if (d->rendererReady)
        d->renderer.release();
    d->target.reset();
    d->rp.reset();
    d->texture.reset();
    d->rhi.reset();
    d->surface.reset();
    d.reset();
}

std::vector<QImage> renderImages(QRhi::Implementation backend, QWindow *window, const Scene &scene, OrbitCamera camera,
                                 const ImageRenderRequest &request, QString *error)
{
    ImageRenderSession session;
    if (!session.begin(backend, window, request.size, request.exposure, request.tonemapper, error))
        return {};
    std::vector<QImage> images;
    for (ViewSettings v : request.views) {
        v.skybox = request.skybox;
        v.post = request.post;
        const QImage image = session.render(scene, camera, v, request.seconds, error);
        if (image.isNull())
            return {};
        images.push_back(image);
    }
    return images;
}
