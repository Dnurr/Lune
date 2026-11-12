#include "renderer.h"

#include <QChildEvent>
#include <QFile>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QtMath>
#include <rhi/qrhi.h>

#include <cstring>

namespace
{
constexpr int kMaxGlassRects = 16;  // must match MAX_RECTS in composite.frag
constexpr int kBlurDownscale = 4;   // blur resolution = target / 4
constexpr int kGridUbufSize = 64 + 8 * 16;
constexpr int kCompositeUbufSize = 2 * 16 + kMaxGlassRects * 16;

QShader loadShader(const QString &name)
{
    QFile f(QStringLiteral(":/shaders/") + name + QStringLiteral(".qsb"));
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("Viewport: missing shader %s", qPrintable(name));
        return {};
    }
    return QShader::fromSerialized(f.readAll());
}

void putVec4(float *dst, const QColor &c, float w)
{
    dst[0] = float(c.redF());
    dst[1] = float(c.greenF());
    dst[2] = float(c.blueF());
    dst[3] = w;
}
} // namespace

Viewport::Viewport(QWidget *parent)
    : QRhiWidget(parent)
{
}

Viewport::~Viewport() = default;

void Viewport::resize(int x, int y)
{
    QRhiWidget::resize(x, y);
}

void Viewport::update(std::function<void()> f)
{
    if (f)
    {
        f();
    }
    QRhiWidget::update();
}

// ---------------------------------------------------------------------------
// Resources
// ---------------------------------------------------------------------------

void Viewport::initialize(QRhiCommandBuffer *)
{
    // Called on the first frame and whenever the render target changes (resize)
    // or the QRhi itself changes (e.g. the widget moved to another window).
    if (m_rhi != rhi()) {
        releaseResources();
        m_rhi = rhi();

        m_sampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                          QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        m_sampler->create();

        auto ubuf = [this](int size) {
            std::unique_ptr<QRhiBuffer> b(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, size));
            b->create();
            return b;
        };
        m_gridUbuf = ubuf(kGridUbufSize);
        m_downUbuf = ubuf(16);
        for (auto &b : m_blurUbufs)
            b = ubuf(16);
        m_compositeUbuf = ubuf(kCompositeUbufSize);
    }

    const QSize size = renderTarget()->pixelSize();
    if (size != m_pixelSize || !m_sceneTex) {
        m_pixelSize = size;
        createSizeDependentResources();
    }

    createPipelines();
}

void Viewport::createSizeDependentResources()
{
    const QSize blurSize = (m_pixelSize / kBlurDownscale).expandedTo(QSize(1, 1));

    auto texture = [this](const QSize &s) {
        std::unique_ptr<QRhiTexture> t(m_rhi->newTexture(QRhiTexture::RGBA8, s, 1, QRhiTexture::RenderTarget));
        t->create();
        return t;
    };
    m_sceneTex = texture(m_pixelSize);
    m_blurA = texture(blurSize);
    m_blurB = texture(blurSize);

    auto target = [this](QRhiTexture *t) {
        std::unique_ptr<QRhiTextureRenderTarget> rt(m_rhi->newTextureRenderTarget({ QRhiColorAttachment(t) }));
        // All offscreen targets have the same format: one render pass descriptor (and the
        // pipelines built with it) works for all of them, also after a resize.
        if (!m_offscreenRp)
            m_offscreenRp.reset(rt->newCompatibleRenderPassDescriptor());
        rt->setRenderPassDescriptor(m_offscreenRp.get());
        rt->create();
        return rt;
    };
    m_sceneRt = target(m_sceneTex.get());
    m_blurARt = target(m_blurA.get());
    m_blurBRt = target(m_blurB.get());

    using B = QRhiShaderResourceBinding;
    const auto stages = B::VertexStage | B::FragmentStage;
    auto srb = [this](std::initializer_list<QRhiShaderResourceBinding> bindings) {
        std::unique_ptr<QRhiShaderResourceBindings> s(m_rhi->newShaderResourceBindings());
        s->setBindings(bindings);
        s->create();
        return s;
    };

    m_gridSrb = srb({ B::uniformBuffer(0, stages, m_gridUbuf.get()) });
    m_downSrb = srb({ B::uniformBuffer(0, stages, m_downUbuf.get()),
                      B::sampledTexture(1, B::FragmentStage, m_sceneTex.get(), m_sampler.get()) });
    // Ping-pong: H reads A writes B, V reads B writes A (twice).
    for (int i = 0; i < 4; ++i) {
        QRhiTexture *src = (i % 2 == 0) ? m_blurA.get() : m_blurB.get();
        m_blurSrbs[i] = srb({ B::uniformBuffer(0, stages, m_blurUbufs[i].get()),
                              B::sampledTexture(1, B::FragmentStage, src, m_sampler.get()) });
    }
    m_compositeSrb = srb({ B::uniformBuffer(0, stages, m_compositeUbuf.get()),
                           B::sampledTexture(1, B::FragmentStage, m_sceneTex.get(), m_sampler.get()),
                           B::sampledTexture(2, B::FragmentStage, m_blurA.get(), m_sampler.get()) });
}

void Viewport::createPipelines()
{
    // Backends where clip-space "up" and texture "up" disagree (D3D, Metal) need the
    // flipped fullscreen triangle so that texture (0,0) is the same corner everywhere.
    const bool flip = m_rhi->isYUpInNDC() != m_rhi->isYUpInFramebuffer();
    const QShader vert = loadShader(flip ? QStringLiteral("fullscreen_flipped.vert") : QStringLiteral("fullscreen.vert"));

    auto pipeline = [&](const QString &frag, QRhiShaderResourceBindings *srb, QRhiRenderPassDescriptor *rp,
                        int sampleCount) {
        std::unique_ptr<QRhiGraphicsPipeline> p(m_rhi->newGraphicsPipeline());
        p->setShaderStages({ { QRhiShaderStage::Vertex, vert }, { QRhiShaderStage::Fragment, loadShader(frag) } });
        p->setVertexInputLayout({}); // fullscreen triangle from gl_VertexIndex
        p->setShaderResourceBindings(srb);
        p->setRenderPassDescriptor(rp);
        p->setSampleCount(sampleCount);
        p->create();
        return p;
    };

    // Rebuilt on every initialize(): they reference the SRBs and the widget's render pass,
    // both of which can be recreated on resize. Cheap enough at this scale.
    m_gridPipe = pipeline(QStringLiteral("grid.frag"), m_gridSrb.get(), m_offscreenRp.get(), 1);
    m_downPipe = pipeline(QStringLiteral("downsample.frag"), m_downSrb.get(), m_offscreenRp.get(), 1);
    m_blurPipe = pipeline(QStringLiteral("blur.frag"), m_blurSrbs[0].get(), m_offscreenRp.get(), 1);
    m_compositePipe = pipeline(QStringLiteral("composite.frag"), m_compositeSrb.get(),
                               renderTarget()->renderPassDescriptor(), renderTarget()->sampleCount());
}

void Viewport::releaseResources()
{
    m_gridPipe.reset();
    m_downPipe.reset();
    m_blurPipe.reset();
    m_compositePipe.reset();
    m_gridSrb.reset();
    m_downSrb.reset();
    m_compositeSrb.reset();
    for (auto &s : m_blurSrbs)
        s.reset();
    m_sceneRt.reset();
    m_blurARt.reset();
    m_blurBRt.reset();
    m_offscreenRp.reset();
    m_sceneTex.reset();
    m_blurA.reset();
    m_blurB.reset();
    m_gridUbuf.reset();
    m_downUbuf.reset();
    for (auto &b : m_blurUbufs)
        b.reset();
    m_compositeUbuf.reset();
    m_sampler.reset();
    m_rhi = nullptr;
    m_pixelSize = {};
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

QVector3D Viewport::cameraEye() const
{
    const float yaw = qDegreesToRadians(m_yaw);
    const float pitch = qDegreesToRadians(m_pitch);
    const QVector3D offset(qCos(pitch) * qSin(yaw), qSin(pitch), qCos(pitch) * qCos(yaw));
    return m_target + offset * m_distance;
}

void Viewport::render(QRhiCommandBuffer *cb)
{
    QRhiResourceUpdateBatch *rub = m_rhi->nextResourceUpdateBatch();
    const QSize blurSize = m_blurA->pixelSize();

    // --- Grid uniforms ---
    {
        const QVector3D eye = cameraEye();
        QMatrix4x4 proj;
        proj.perspective(50.0f, float(m_pixelSize.width()) / qMax(1, m_pixelSize.height()), 0.1f, 1000.0f);
        QMatrix4x4 view;
        view.lookAt(eye, m_target, QVector3D(0, 1, 0));
        const QMatrix4x4 invViewProj = (proj * view).inverted();

        float data[kGridUbufSize / 4] = {};
        std::memcpy(data, invViewProj.constData(), 64);
        float *v = data + 16;
        v[0] = eye.x(); v[1] = eye.y(); v[2] = eye.z(); v[3] = 1.0f;
        v += 4;
        // NDC y sign (Vulkan has y down in clip space), fade distance, minor/major cell size
        v[0] = m_rhi->isYUpInNDC() ? 1.0f : -1.0f;
        v[1] = qMax(60.0f, m_distance * 8.0f);
        v[2] = 1.0f;
        v[3] = 10.0f;
        v += 4;
        putVec4(v, QColor("#1C1C20"), 1.0f); v += 4; // sky top
        putVec4(v, QColor("#121214"), 1.0f); v += 4; // sky bottom / horizon
        putVec4(v, QColor("#2E2E34"), 1.0f); v += 4; // minor lines
        putVec4(v, QColor("#46464E"), 1.0f); v += 4; // major lines
        putVec4(v, QColor("#C8434F"), 0.9f); v += 4; // X axis
        putVec4(v, QColor("#3F6FD8"), 0.9f);         // Z axis (navy-blue accent)
        rub->updateDynamicBuffer(m_gridUbuf.get(), 0, kGridUbufSize, data);
    }

    // --- Blur uniforms ---
    {
        const float down[4] = { 1.0f / m_pixelSize.width(), 1.0f / m_pixelSize.height(), 0, 0 };
        rub->updateDynamicBuffer(m_downUbuf.get(), 0, 16, down);
        const float tx = 1.0f / blurSize.width();
        const float ty = 1.0f / blurSize.height();
        for (int i = 0; i < 4; ++i) {
            const float p[4] = { (i % 2 == 0) ? tx : 0.0f, (i % 2 == 0) ? 0.0f : ty, 0, 0 };
            rub->updateDynamicBuffer(m_blurUbufs[i].get(), 0, 16, p);
        }
    }

    // --- Composite uniforms: glass rectangles from the child widgets ---
    {
        float data[kCompositeUbufSize / 4] = {};
        const qreal dpr = devicePixelRatio();
        int count = 0;
        float radius = 12.0f;
        for (QObject *o : children()) {
            auto *w = qobject_cast<QWidget *>(o);
            if (!w || !w->isVisible() || !w->property("glass").toBool() || count >= kMaxGlassRects)
                continue;
            const QVariant r = w->property("glassRadius");
            if (r.isValid())
                radius = r.toFloat();
            const QRect g = w->geometry();
            float *rect = data + 8 + count * 4;
            rect[0] = float(g.x() * dpr);
            rect[1] = float(g.y() * dpr);
            rect[2] = float(g.width() * dpr);
            rect[3] = float(g.height() * dpr);
            ++count;
        }
        data[0] = float(count);
        data[1] = float(radius * dpr);
        data[2] = m_rhi->isYUpInFramebuffer() ? 1.0f : 0.0f;
        data[4] = float(m_pixelSize.width());
        data[5] = float(m_pixelSize.height());
        rub->updateDynamicBuffer(m_compositeUbuf.get(), 0, kCompositeUbufSize, data);
    }

    auto fullscreen = [cb](QRhiGraphicsPipeline *pipe, QRhiShaderResourceBindings *srb, const QSize &s) {
        cb->setGraphicsPipeline(pipe);
        cb->setViewport({ 0, 0, float(s.width()), float(s.height()) });
        cb->setShaderResources(srb);
        cb->draw(3);
    };
    const QColor black(Qt::black);

    // 1. Scene
    cb->beginPass(m_sceneRt.get(), black, { 1.0f, 0 }, rub);
    fullscreen(m_gridPipe.get(), m_gridSrb.get(), m_pixelSize);
    cb->endPass();

    // 2. Blur: downsample into A, then H (A->B), V (B->A), twice
    cb->beginPass(m_blurARt.get(), black, { 1.0f, 0 });
    fullscreen(m_downPipe.get(), m_downSrb.get(), blurSize);
    cb->endPass();
    for (int i = 0; i < 4; ++i) {
        cb->beginPass((i % 2 == 0) ? m_blurBRt.get() : m_blurARt.get(), black, { 1.0f, 0 });
        fullscreen(m_blurPipe.get(), m_blurSrbs[i].get(), blurSize);
        cb->endPass();
    }

    // 3. Final image
    cb->beginPass(renderTarget(), black, { 1.0f, 0 });
    fullscreen(m_compositePipe.get(), m_compositeSrb.get(), m_pixelSize);
    cb->endPass();
}

// ---------------------------------------------------------------------------
// Glass widgets
// ---------------------------------------------------------------------------

void Viewport::childEvent(QChildEvent *event)
{
    if (event->added() && event->child()->isWidgetType())
        event->child()->installEventFilter(this);
    else if (event->removed())
        QRhiWidget::update();
    QRhiWidget::childEvent(event);
}

bool Viewport::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::Move:
    case QEvent::Resize:
    case QEvent::Show:
    case QEvent::Hide:
    case QEvent::DynamicPropertyChange:
        if (watched->parent() == this)
            QRhiWidget::update(); // glass rectangles changed
        break;
    default:
        break;
    }
    return QRhiWidget::eventFilter(watched, event);
}

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------

void Viewport::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton) {
        m_dragButton = event->button();
        m_lastMouse = event->position().toPoint();
        event->accept();
        return;
    }
    QRhiWidget::mousePressEvent(event);
}

void Viewport::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragButton == Qt::NoButton) {
        QRhiWidget::mouseMoveEvent(event);
        return;
    }
    const QPoint pos = event->position().toPoint();
    const QPoint d = pos - m_lastMouse;
    m_lastMouse = pos;

    if (m_dragButton == Qt::RightButton) {
        m_yaw -= d.x() * 0.3f;
        m_pitch = qBound(-89.0f, m_pitch + d.y() * 0.3f, 89.0f);
    } else {
        // Pan on the camera plane; speed proportional to the distance so it feels constant.
        const QVector3D forward = (m_target - cameraEye()).normalized();
        const QVector3D right = QVector3D::crossProduct(forward, QVector3D(0, 1, 0)).normalized();
        const QVector3D up = QVector3D::crossProduct(right, forward);
        const float k = m_distance * 0.0015f;
        m_target += (-right * d.x() + up * d.y()) * k;
    }
    QRhiWidget::update();
    event->accept();
}

void Viewport::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == m_dragButton) {
        m_dragButton = Qt::NoButton;
        event->accept();
        return;
    }
    QRhiWidget::mouseReleaseEvent(event);
}

void Viewport::wheelEvent(QWheelEvent *event)
{
    const float steps = event->angleDelta().y() / 120.0f;
    m_distance = qBound(0.5f, m_distance * qPow(0.88f, steps), 500.0f);
    QRhiWidget::update();
    event->accept();
}
