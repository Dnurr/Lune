#include "renderer.h"

#include "camera.h"
#include "engine/scene/scene.h"

#include <QColor>
#include <QFile>
#include <rhi/qrhi.h>

#include <cmath>
#include <cstring>

namespace
{
constexpr int kBlurDownscale = 4; // blur resolution = target / 4
constexpr float kBlurRadius = 2;
constexpr int kGridUbufSize = 64 + 8 * 16;
constexpr int kCompositeUbufSize = 2 * 16 + Renderer::MaxGlassRects * 16;
constexpr int kEntityUbufSize = 3 * 64 + 5 * 16; // see mesh.vert
constexpr int kOverlayVertexSize = int(sizeof(OverlayVertex));
// Lights uniform block (mesh.frag, binding 1): ivec4 header + MaxLights * 3 vec4.
constexpr int kLightSize = 3 * 16;
constexpr int kLightsUbufSize = 16 + Renderer::MaxLights * kLightSize;
// Environment uniform block (mesh.frag binding 2, background.frag binding 1): mat4 + vec4.
constexpr int kEnvUbufSize = 64 + 16;
constexpr int kBackgroundUbufSize = 64 + 2 * 16;
// Shadow uniform block (mesh.frag binding 6, shadow.vert binding 1): 2 mat4 + vec4.
constexpr int kShadowUbufSize = 2 * 64 + 16;
// Hardware depth bias of the shadow pass, growing with the surface's slope as seen from the
// light: removes most shadow acne on its own. The shader can add its own on top.
constexpr float kShadowSlopeBias = 1.5f;

// Editor look of meshes.
const QColor kOutlineColor("#8FB0F2"); // palette primary (navy accent)
constexpr float kOutlineWidth = 2.0f;  // logical pixels

QShader loadShader(const QString &name)
{
    QFile f(QStringLiteral(":/shaders/") + name + QStringLiteral(".qsb"));
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("Renderer: missing shader %s", qPrintable(name));
        return {};
    }
    return QShader::fromSerialized(f.readAll());
}

float *putVec4(float *dst, const QColor &c, float w)
{
    dst[0] = float(c.redF());
    dst[1] = float(c.greenF());
    dst[2] = float(c.blueF());
    dst[3] = w;
    return dst + 4;
}

float sRGBToLinear(float c)
{
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

// Colors picked in the UI are display (sRGB) values; mesh lighting happens in linear space.
float *putLinearColor(float *dst, const QColor &c, float a)
{
    dst[0] = sRGBToLinear(c.redF());
    dst[1] = sRGBToLinear(c.greenF());
    dst[2] = sRGBToLinear(c.blueF());
    dst[3] = a;
    return dst + 4;
}

float *putMat4(float *dst, const QMatrix4x4 &m)
{
    std::memcpy(dst, m.constData(), 64); // column-major, as std140 expects
    return dst + 16;
}

QRhiVertexInputLayout meshInputLayout()
{
    QRhiVertexInputLayout layout;
    layout.setBindings({ { quint32(sizeof(MeshVertex)) } });
    layout.setAttributes({ { 0, 0, QRhiVertexInputAttribute::Float3, 0 },
                           { 0, 1, QRhiVertexInputAttribute::Float3, quint32(sizeof(QVector3D)) } });
    return layout;
}
} // namespace

Renderer::Renderer() = default;

Renderer::~Renderer() = default;

void Renderer::setGlassRects(const QList<QRectF> &rects, float cornerRadius)
{
    m_glassRects = rects.mid(0, MaxGlassRects);
    m_glassRadius = cornerRadius;
}

const char *Renderer::backendName() const
{
    return m_rhi ? m_rhi->backendName() : "none";
}

void Renderer::setDisplayParams(float exposureEv, int tonemapper)
{
    m_exposure = exposureEv;
    m_tonemapper = tonemapper;
}

void Renderer::setOverlay(std::vector<OverlayVertex> vertices, float pixelScale)
{
    m_overlay = std::move(vertices);
    m_overlayScale = pixelScale;
}

// ---------------------------------------------------------------------------
// Resources
// ---------------------------------------------------------------------------

void Renderer::initialize(QRhi *rhi, QRhiRenderTarget *target)
{
    if (m_rhi != rhi) {
        release();
        m_rhi = rhi;

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
        m_displayUbuf = ubuf(16);
        m_overlayUbuf = ubuf(16);
        m_layoutUbuf = ubuf(kEntityUbufSize);
        m_lightsUbuf = ubuf(kLightsUbufSize);
        m_envUbuf = ubuf(kEnvUbufSize);
        m_backgroundUbuf = ubuf(kBackgroundUbufSize);
        m_shadowUbuf = ubuf(kShadowUbufSize);

        m_ibl.initialize(m_rhi);

        // Shadow map: the most precise depth format available, rendered without color.
        QRhiTexture::Format depthFormat = QRhiTexture::D16;
        for (QRhiTexture::Format f : { QRhiTexture::D32F, QRhiTexture::D24 }) {
            if (m_rhi->isTextureFormatSupported(f)) {
                depthFormat = f;
                break;
            }
        }
        m_shadowMap.reset(m_rhi->newTexture(depthFormat, QSize(ShadowMapSize, ShadowMapSize), 1,
                                            QRhiTexture::RenderTarget));
        m_shadowMap->create();
        QRhiTextureRenderTargetDescription shadowDesc;
        shadowDesc.setDepthTexture(m_shadowMap.get());
        m_shadowRt.reset(m_rhi->newTextureRenderTarget(shadowDesc));
        m_shadowRp.reset(m_shadowRt->newCompatibleRenderPassDescriptor());
        m_shadowRt->setRenderPassDescriptor(m_shadowRp.get());
        m_shadowRt->create();
        // Comparison sampler: texture(shadowMap, vec3(uv, depth)) = 1 where depth <= stored depth,
        // filtered (linear) across the 4 nearest texels.
        m_shadowSampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                                QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        m_shadowSampler->setTextureCompareOp(QRhiSampler::LessOrEqual);
        m_shadowSampler->create();

        using B = QRhiShaderResourceBinding;
        const auto stages = B::VertexStage | B::FragmentStage;
        m_meshLayoutSrb.reset(m_rhi->newShaderResourceBindings());
        {
            const auto b = meshBindings(m_layoutUbuf.get());
            m_meshLayoutSrb->setBindings(b.cbegin(), b.cend());
        }
        m_meshLayoutSrb->create();
        m_shadowLayoutSrb.reset(m_rhi->newShaderResourceBindings());
        {
            const auto b = shadowBindings(m_layoutUbuf.get());
            m_shadowLayoutSrb->setBindings(b.cbegin(), b.cend());
        }
        m_shadowLayoutSrb->create();
        m_backgroundSrb.reset(m_rhi->newShaderResourceBindings());
        m_backgroundSrb->setBindings({ B::uniformBuffer(0, stages, m_backgroundUbuf.get()),
                                       B::uniformBuffer(1, B::FragmentStage, m_envUbuf.get()),
                                       B::sampledTexture(2, B::FragmentStage, m_ibl.environment(), m_ibl.cubeSampler()),
                                       B::sampledTexture(3, B::FragmentStage, m_ibl.prefiltered(), m_ibl.cubeSampler()) });
        m_backgroundSrb->create();
        m_overlaySrb.reset(m_rhi->newShaderResourceBindings());
        m_overlaySrb->setBindings({ B::uniformBuffer(0, B::VertexStage, m_overlayUbuf.get()) });
        m_overlaySrb->create();
    }

    const QSize size = target->pixelSize();
    if (size != m_pixelSize || !m_sceneTex) {
        m_pixelSize = size;
        createSizeDependentResources();
    }

    createPipelines(target);
}

void Renderer::createSizeDependentResources()
{
    const QSize blurSize = (m_pixelSize / kBlurDownscale).expandedTo(QSize(1, 1));

    auto texture = [this](const QSize &s, QRhiTexture::Format format) {
        std::unique_ptr<QRhiTexture> t(m_rhi->newTexture(format, s, 1, QRhiTexture::RenderTarget));
        t->create();
        return t;
    };
    m_sceneTex = texture(m_pixelSize, m_rhi->isTextureFormatSupported(QRhiTexture::RGBA16F, QRhiTexture::RenderTarget) ? QRhiTexture::RGBA16F : QRhiTexture::RGBA8);
    // After the display pass the values are already for the screen (0..1, sRGB): 8 bits are enough.
    m_displayTex = texture(m_pixelSize, QRhiTexture::RGBA8);
    m_blurA = texture(blurSize, QRhiTexture::RGBA8);
    m_blurB = texture(blurSize, QRhiTexture::RGBA8);

    m_sceneDepth.reset(m_rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, m_pixelSize));
    m_sceneDepth->create();

    // Render pass descriptors are created once and reused after a resize (same formats),
    // so they always match the pipelines built with them.
    QRhiTextureRenderTargetDescription sceneDesc { QRhiColorAttachment(m_sceneTex.get()) };
    sceneDesc.setDepthStencilBuffer(m_sceneDepth.get());
    m_sceneRt.reset(m_rhi->newTextureRenderTarget(sceneDesc));
    if (!m_sceneRp)
        m_sceneRp.reset(m_sceneRt->newCompatibleRenderPassDescriptor());
    m_sceneRt->setRenderPassDescriptor(m_sceneRp.get());
    m_sceneRt->create();

    // Display and blur targets have the same format: one render pass descriptor for all three.
    auto ldrTarget = [this](QRhiTexture *t) {
        std::unique_ptr<QRhiTextureRenderTarget> rt(m_rhi->newTextureRenderTarget({ QRhiColorAttachment(t) }));
        if (!m_ldrRp)
            m_ldrRp.reset(rt->newCompatibleRenderPassDescriptor());
        rt->setRenderPassDescriptor(m_ldrRp.get());
        rt->create();
        return rt;
    };
    m_displayRt = ldrTarget(m_displayTex.get());
    m_blurARt = ldrTarget(m_blurA.get());
    m_blurBRt = ldrTarget(m_blurB.get());

    using B = QRhiShaderResourceBinding;
    const auto stages = B::VertexStage | B::FragmentStage;
    auto srb = [this](std::initializer_list<QRhiShaderResourceBinding> bindings) {
        std::unique_ptr<QRhiShaderResourceBindings> s(m_rhi->newShaderResourceBindings());
        s->setBindings(bindings);
        s->create();
        return s;
    };

    m_gridSrb = srb({ B::uniformBuffer(0, stages, m_gridUbuf.get()) });
    m_displaySrb = srb({ B::uniformBuffer(0, stages, m_displayUbuf.get()),
                         B::sampledTexture(1, B::FragmentStage, m_sceneTex.get(), m_sampler.get()) });
    // The blur and the final image read the display image (gizmo included), not the HDR scene.
    m_downSrb = srb({ B::uniformBuffer(0, stages, m_downUbuf.get()),
                      B::sampledTexture(1, B::FragmentStage, m_displayTex.get(), m_sampler.get()) });
    // Ping-pong: H reads A writes B, V reads B writes A (twice).
    for (int i = 0; i < 4; ++i) {
        QRhiTexture *src = (i % 2 == 0) ? m_blurA.get() : m_blurB.get();
        m_blurSrbs[i] = srb({ B::uniformBuffer(0, stages, m_blurUbufs[i].get()),
                              B::sampledTexture(1, B::FragmentStage, src, m_sampler.get()) });
    }
    m_compositeSrb = srb({ B::uniformBuffer(0, stages, m_compositeUbuf.get()),
                           B::sampledTexture(1, B::FragmentStage, m_displayTex.get(), m_sampler.get()),
                           B::sampledTexture(2, B::FragmentStage, m_blurA.get(), m_sampler.get()) });
}

void Renderer::createPipelines(QRhiRenderTarget *target)
{
    // Backends where clip-space "up" and texture "up" disagree (D3D, Metal) need the
    // flipped fullscreen triangle so that texture (0,0) is the same corner everywhere.
    const bool flip = m_rhi->isYUpInNDC() != m_rhi->isYUpInFramebuffer();
    const QShader fullscreen = loadShader(flip ? QStringLiteral("fullscreen_flipped.vert") : QStringLiteral("fullscreen.vert"));

    struct Options
    {
        QRhiVertexInputLayout input;
        QRhiGraphicsPipeline::CullMode cull = QRhiGraphicsPipeline::None;
        bool depth = false;
        bool blend = false;
        float slopeBias = 0.0f;
    };
    auto pipeline = [&](const QShader &vert, const QString &frag, QRhiShaderResourceBindings *srb,
                        QRhiRenderPassDescriptor *rp, int sampleCount, const Options &o = {}) {
        std::unique_ptr<QRhiGraphicsPipeline> p(m_rhi->newGraphicsPipeline());
        p->setShaderStages({ { QRhiShaderStage::Vertex, vert }, { QRhiShaderStage::Fragment, loadShader(frag) } });
        p->setVertexInputLayout(o.input);
        p->setShaderResourceBindings(srb);
        p->setRenderPassDescriptor(rp);
        p->setSampleCount(sampleCount);
        p->setCullMode(o.cull);
        p->setDepthTest(o.depth);
        p->setDepthWrite(o.depth);
        p->setSlopeScaledDepthBias(o.slopeBias);
        if (o.blend) {
            QRhiGraphicsPipeline::TargetBlend b;
            b.enable = true;
            b.srcColor = QRhiGraphicsPipeline::One; // premultiplied alpha
            b.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
            b.srcAlpha = QRhiGraphicsPipeline::One;
            b.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
            p->setTargetBlends({ b });
        }
        p->create();
        return p;
    };

    Options mesh;
    mesh.input = meshInputLayout();
    mesh.depth = true;
    mesh.cull = QRhiGraphicsPipeline::None; // two-sided: imported meshes may have mixed winding
    Options outline = mesh;
    outline.cull = QRhiGraphicsPipeline::Front; // inverted hull: only the back faces
    Options shadow; // depth only: positions, no color
    shadow.input.setBindings({ { quint32(sizeof(MeshVertex)) } });
    shadow.input.setAttributes({ { 0, 0, QRhiVertexInputAttribute::Float3, 0 } });
    shadow.depth = true;
    shadow.slopeBias = kShadowSlopeBias;
    Options background; // opaque, then the grid blends over it
    Options grid;
    grid.blend = true; // premultiplied: opaque with its own sky, only the lines over an environment
    Options overlay;
    overlay.input.setBindings({ { quint32(kOverlayVertexSize) } });
    overlay.input.setAttributes({ { 0, 0, QRhiVertexInputAttribute::Float2, 0 },
                                  { 0, 1, QRhiVertexInputAttribute::Float4, quint32(2 * sizeof(float)) } });
    overlay.blend = true;

    // Rebuilt on every initialize(): they reference SRBs and the target's render pass,
    // which can be recreated on resize. Cheap enough at this scale.
    m_shadowPipe = pipeline(loadShader(QStringLiteral("shadow.vert")), QStringLiteral("shadow.frag"),
                            m_shadowLayoutSrb.get(), m_shadowRp.get(), 1, shadow);
    m_backgroundPipe = pipeline(loadShader(QStringLiteral("background.vert")), QStringLiteral("background.frag"),
                                m_backgroundSrb.get(), m_sceneRp.get(), 1, background);
    m_gridPipe = pipeline(fullscreen, QStringLiteral("grid.frag"), m_gridSrb.get(), m_sceneRp.get(), 1, grid);
    m_meshPipe = pipeline(loadShader(QStringLiteral("mesh.vert")), QStringLiteral("mesh.frag"), m_meshLayoutSrb.get(),
                          m_sceneRp.get(), 1, mesh);
    m_outlinePipe = pipeline(loadShader(QStringLiteral("outline.vert")), QStringLiteral("outline.frag"),
                             m_meshLayoutSrb.get(), m_sceneRp.get(), 1, outline);
    m_displayPipe = pipeline(fullscreen, QStringLiteral("display.frag"), m_displaySrb.get(), m_ldrRp.get(), 1);
    // The overlay is interface, not scene: drawn after the display conversion, in display colors,
    // so exposure/tonemapping never touch it. It's still before the blur, so panels blur it too.
    m_overlayPipe = pipeline(loadShader(QStringLiteral("overlay.vert")), QStringLiteral("overlay.frag"),
                             m_overlaySrb.get(), m_ldrRp.get(), 1, overlay);
    m_downPipe = pipeline(fullscreen, QStringLiteral("downsample.frag"), m_downSrb.get(), m_ldrRp.get(), 1);
    m_blurPipe = pipeline(fullscreen, QStringLiteral("blur.frag"), m_blurSrbs[0].get(), m_ldrRp.get(), 1);
    m_compositePipe = pipeline(fullscreen, QStringLiteral("composite.frag"), m_compositeSrb.get(),
                               target->renderPassDescriptor(), target->sampleCount());
}

void Renderer::release()
{
    m_meshes.clear();
    m_entities.clear();
    m_gridPipe.reset();
    m_backgroundPipe.reset();
    m_shadowPipe.reset();
    m_meshPipe.reset();
    m_outlinePipe.reset();
    m_overlayPipe.reset();
    m_displayPipe.reset();
    m_downPipe.reset();
    m_blurPipe.reset();
    m_compositePipe.reset();
    m_gridSrb.reset();
    m_displaySrb.reset();
    m_downSrb.reset();
    m_compositeSrb.reset();
    m_overlaySrb.reset();
    m_meshLayoutSrb.reset();
    m_backgroundSrb.reset();
    m_shadowLayoutSrb.reset();
    for (auto &s : m_blurSrbs)
        s.reset();
    m_sceneRt.reset();
    m_displayRt.reset();
    m_blurARt.reset();
    m_blurBRt.reset();
    m_sceneRp.reset();
    m_ldrRp.reset();
    m_sceneDepth.reset();
    m_sceneTex.reset();
    m_displayTex.reset();
    m_blurA.reset();
    m_blurB.reset();
    m_gridUbuf.reset();
    m_downUbuf.reset();
    for (auto &b : m_blurUbufs)
        b.reset();
    m_compositeUbuf.reset();
    m_displayUbuf.reset();
    m_overlayUbuf.reset();
    m_overlayVbuf.reset();
    m_layoutUbuf.reset();
    m_lightsUbuf.reset();
    m_envUbuf.reset();
    m_backgroundUbuf.reset();
    m_shadowUbuf.reset();
    m_shadowRt.reset();
    m_shadowRp.reset();
    m_shadowMap.reset();
    m_shadowSampler.reset();
    m_ibl.release();
    m_sampler.reset();
    m_rhi = nullptr;
    m_pixelSize = {};
}

Renderer::GpuMesh &Renderer::gpuMesh(const MeshPtr &mesh, QRhiResourceUpdateBatch *rub)
{
    auto it = m_meshes.find(mesh.get());
    if (it != m_meshes.end())
        return it->second;

    GpuMesh g;
    g.mesh = mesh;
    g.indexCount = quint32(mesh->indices.size());
    const quint32 vbytes = quint32(mesh->vertices.size() * sizeof(MeshVertex));
    const quint32 ibytes = quint32(mesh->indices.size() * sizeof(quint32));
    g.vertices.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, vbytes));
    g.vertices->create();
    g.indices.reset(m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer, ibytes));
    g.indices->create();
    rub->uploadStaticBuffer(g.vertices.get(), mesh->vertices.data());
    rub->uploadStaticBuffer(g.indices.get(), mesh->indices.data());
    return m_meshes.emplace(mesh.get(), std::move(g)).first->second;
}

Renderer::GpuEntity &Renderer::gpuEntity(EntityId id)
{
    auto it = m_entities.find(id);
    if (it != m_entities.end())
        return it->second;

    using B = QRhiShaderResourceBinding;
    GpuEntity g;
    g.uniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kEntityUbufSize));
    g.uniforms->create();
    g.srb.reset(m_rhi->newShaderResourceBindings());
    const auto bindings = meshBindings(g.uniforms.get());
    g.srb->setBindings(bindings.cbegin(), bindings.cend());
    g.srb->create();
    g.shadowSrb.reset(m_rhi->newShaderResourceBindings());
    const auto shadow = shadowBindings(g.uniforms.get());
    g.shadowSrb->setBindings(shadow.cbegin(), shadow.cend());
    g.shadowSrb->create();
    return m_entities.emplace(id, std::move(g)).first->second;
}

QList<QRhiShaderResourceBinding> Renderer::meshBindings(QRhiBuffer *entityUniforms) const
{
    using B = QRhiShaderResourceBinding;
    const auto fs = B::FragmentStage;
    return { B::uniformBuffer(0, B::VertexStage | fs, entityUniforms),
             B::uniformBuffer(1, fs, m_lightsUbuf.get()),
             B::uniformBuffer(2, fs, m_envUbuf.get()),
             B::sampledTexture(3, fs, m_ibl.irradiance(), m_ibl.cubeSampler()),
             B::sampledTexture(4, fs, m_ibl.prefiltered(), m_ibl.cubeSampler()),
             B::sampledTexture(5, fs, m_ibl.brdfLut(), m_ibl.lutSampler()),
             B::uniformBuffer(6, fs, m_shadowUbuf.get()),
             B::sampledTexture(7, fs, m_shadowMap.get(), m_shadowSampler.get()) };
}

QList<QRhiShaderResourceBinding> Renderer::shadowBindings(QRhiBuffer *entityUniforms) const
{
    using B = QRhiShaderResourceBinding;
    return { B::uniformBuffer(0, B::VertexStage, entityUniforms), B::uniformBuffer(1, B::VertexStage, m_shadowUbuf.get()) };
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

void Renderer::render(QRhiCommandBuffer *cb, QRhiRenderTarget *target, const OrbitCamera &camera, const Scene *scene,
                      EntityId selected)
{
    // --- Environment: the first enabled one, or the default gradient ---
    Environment environment;
    QMatrix4x4 envRotation; // world direction -> environment direction
    bool hasEnvironment = false;
    if (scene) {
        for (const Entity &e : scene->entities()) {
            if (e.environment && e.environment->enabled) {
                environment = *e.environment;
                Transform rotationOnly;
                rotationOnly.rotation = e.transform.rotation;
                envRotation = rotationOnly.matrix().transposed(); // inverse of a rotation
                hasEnvironment = true;
                break;
            }
        }
    }
    const bool showBackground = hasEnvironment && environment.background;
    // Recomputes the maps (before anything samples them) only when the source changed.
    const int iblPasses = m_ibl.update(cb, { environment.image, environment.skyColor, environment.groundColor });

    QRhiResourceUpdateBatch *rub = m_rhi->nextResourceUpdateBatch();
    const QSize blurSize = m_blurA->pixelSize();
    const QVector3D eye = camera.eye();
    const float aspect = float(m_pixelSize.width()) / qMax(1, m_pixelSize.height());
    const QMatrix4x4 viewProj = camera.projection(aspect) * camera.view();

    // --- Grid uniforms ---
    {
        float data[kGridUbufSize / 4] = {};
        float *v = putMat4(data, viewProj.inverted());
        v[0] = eye.x(); v[1] = eye.y(); v[2] = eye.z(); v[3] = 1.0f;
        v += 4;
        // NDC y sign (Vulkan has y down in clip space), fade distance, minor/major cell size
        v[0] = m_rhi->isYUpInNDC() ? 1.0f : -1.0f;
        v[1] = qMax(60.0f, camera.distance * 8.0f);
        v[2] = 1.0f;
        v[3] = 10.0f;
        v += 4;
        // Sky top alpha = sky opacity: 0 over an environment background, so only the lines are drawn
        v = putLinearColor(v, QColor("#1C1C20"), showBackground ? 0.0f : 1.0f); // sky top
        v = putLinearColor(v, QColor("#121214"), 1.0f); // sky bottom / horizon
        v = putLinearColor(v, QColor("#2E2E34"), 1.0f); // minor lines
        v = putLinearColor(v, QColor("#46464E"), 1.0f); // major lines
        v = putLinearColor(v, QColor("#C8434F"), 0.9f); // X axis
        putLinearColor(v, QColor("#3F6FD8"), 0.9f);     // Z axis (navy-blue accent)
        rub->updateDynamicBuffer(m_gridUbuf.get(), 0, kGridUbufSize, data);
    }

    // --- Lights: one buffer for the frame, shared by every mesh ---
    // The first enabled directional light with shadows on casts them (index in lights[]).
    int shadowLight = -1;
    QVector3D shadowDirection;
    float shadowSoftness = 1.0f;
    {
        float data[kLightsUbufSize / 4] = {};
        int count = 0;
        auto put = [&](int type, const QVector3D &position, const QVector3D &direction, const QColor &color,
                       float intensity) {
            float *v = data + 4 + count * (kLightSize / 4);
            v[0] = position.x(); v[1] = position.y(); v[2] = position.z(); v[3] = float(type);
            v[4] = direction.x(); v[5] = direction.y(); v[6] = direction.z();
            putLinearColor(v + 8, color, intensity);
            ++count;
        };
        if (scene) {
            for (const Entity &e : scene->entities()) {
                if (!e.light || !e.light->enabled || count == MaxLights)
                    continue;
                if (shadowLight < 0 && e.light->shadows && e.light->type == Light::Directional) {
                    shadowLight = count;
                    shadowDirection = Light::direction(e.transform);
                    shadowSoftness = e.light->softness;
                }
                put(e.light->type, e.transform.position, Light::direction(e.transform), e.light->color,
                    e.light->intensity);
            }
        }
        // A scene without lights (older files, or all disabled) would be black:
        // light it with a default sun so it can still be edited.
        if (count == 0) {
            shadowLight = 0;
            shadowDirection = QVector3D(-0.45f, -0.8f, -0.35f).normalized();
            put(Light::Directional, QVector3D(), shadowDirection, Qt::white, 3.0f);
        }
        const qint32 header[4] = { count, 0, 0, 0 };
        std::memcpy(data, header, sizeof(header));
        rub->updateDynamicBuffer(m_lightsUbuf.get(), 0, kLightsUbufSize, data);
    }

    // --- Environment uniforms (meshes and background) ---
    {
        float data[kEnvUbufSize / 4] = {};
        float *v = putMat4(data, envRotation);
        v[0] = environment.intensity;
        v[1] = float(IblMaps::PrefilterLevels - 1);
        v[2] = environment.blur;
        v[3] = showBackground ? 1.0f : 0.0f;
        rub->updateDynamicBuffer(m_envUbuf.get(), 0, kEnvUbufSize, data);

        float bg[kBackgroundUbufSize / 4] = {};
        v = putMat4(bg, viewProj.inverted());
        v[0] = eye.x(); v[1] = eye.y(); v[2] = eye.z(); v[3] = 1.0f;
        v[4] = m_rhi->isYUpInNDC() ? 1.0f : -1.0f;
        rub->updateDynamicBuffer(m_backgroundUbuf.get(), 0, kBackgroundUbufSize, bg);
    }

    // --- Meshes: upload new ones, per-entity uniforms ---
    struct Draw
    {
        GpuMesh *mesh;
        QRhiShaderResourceBindings *srb;
        QRhiShaderResourceBindings *shadowSrb;
        bool selected;
    };
    std::vector<Draw> draws;
    QVector3D boundsMin(1e30f, 1e30f, 1e30f), boundsMax(-1e30f, -1e30f, -1e30f); // of every drawn mesh, world space
    {
        const QMatrix4x4 clipViewProj = m_rhi->clipSpaceCorrMatrix() * viewProj;
        std::unordered_map<EntityId, bool> seen;
        if (scene) {
            for (const Entity &e : scene->entities()) {
                if (!e.mesh || e.mesh->indices.empty() || !e.visible)
                    continue;
                GpuMesh &gm = gpuMesh(e.mesh, rub);
                GpuEntity &ge = gpuEntity(e.id);
                seen[e.id] = true;

                const QMatrix4x4 model = e.transform.matrix();
                float data[kEntityUbufSize / 4] = {};
                float *v = putMat4(data, clipViewProj * model);
                v = putMat4(v, model);
                v = putMat4(v, QMatrix4x4(model.normalMatrix()));
                v = putLinearColor(v, e.displayColor, 1.0f);
                v = putLinearColor(v, kOutlineColor, kOutlineWidth * m_overlayScale);
                v[0] = float(m_pixelSize.width());
                v[1] = float(m_pixelSize.height());
                v += 4;
                v[0] = eye.x(); v[1] = eye.y(); v[2] = eye.z();
                v += 4;
                v[0] = e.roughness; v[1] = e.metallic;
                rub->updateDynamicBuffer(ge.uniforms.get(), 0, kEntityUbufSize, data);

                draws.push_back({ &gm, ge.srb.get(), ge.shadowSrb.get(), e.id == selected });

                const QVector3D &lo = e.mesh->boundsMin, &hi = e.mesh->boundsMax;
                for (int c = 0; c < 8; ++c) {
                    const QVector3D corner = model.map(QVector3D(c & 1 ? hi.x() : lo.x(), c & 2 ? hi.y() : lo.y(),
                                                                 c & 4 ? hi.z() : lo.z()));
                    boundsMin = QVector3D(qMin(boundsMin.x(), corner.x()), qMin(boundsMin.y(), corner.y()),
                                          qMin(boundsMin.z(), corner.z()));
                    boundsMax = QVector3D(qMax(boundsMax.x(), corner.x()), qMax(boundsMax.y(), corner.y()),
                                          qMax(boundsMax.z(), corner.z()));
                }
            }
        }
        // Forget entities that are gone, and meshes no entity uses any more
        // (an undo-able deleted entity still holds its MeshPtr, so its mesh stays).
        std::erase_if(m_entities, [&](const auto &kv) { return !seen.count(kv.first); });
        std::erase_if(m_meshes, [](const auto &kv) { return kv.second.mesh.use_count() == 1; });
    }

    // --- Shadow uniforms ---
    // The light's view covers the bounding sphere of every mesh: independent from the camera,
    // so shadows don't swim while orbiting. Large scenes lose resolution (cascades: later).
    const bool shadows = shadowLight >= 0 && !draws.empty();
    {
        QMatrix4x4 lightClip;  // world -> light clip space, corrected for the backend (shadow pass)
        QMatrix4x4 shadowUv;   // world -> (shadow map uv, depth compared with the map)
        if (shadows) {
            const QVector3D center = (boundsMin + boundsMax) * 0.5f;
            const float radius = qMax(0.5f, (boundsMax - boundsMin).length() * 0.5f * 1.02f);
            const QVector3D up = qAbs(shadowDirection.y()) > 0.99f ? QVector3D(1, 0, 0) : QVector3D(0, 1, 0);
            QMatrix4x4 view;
            view.lookAt(center - shadowDirection * radius, center, up);
            QMatrix4x4 projection;
            projection.ortho(-radius, radius, -radius, radius, 0.0f, 2.0f * radius);
            lightClip = m_rhi->clipSpaceCorrMatrix() * projection * view;

            // Clip space -> texture: x, y from -1..1 to 0..1 (v flipped where texture rows and
            // clip-space y disagree), depth to 0..1 where the backend's clip depth is -1..1 (OpenGL).
            const bool zeroToOne = m_rhi->isClipDepthZeroToOne();
            const float vScale = m_rhi->isYUpInFramebuffer() == m_rhi->isYUpInNDC() ? 0.5f : -0.5f;
            QMatrix4x4 toTexture;
            toTexture.translate(0.5f, 0.5f, zeroToOne ? 0.0f : 0.5f);
            toTexture.scale(0.5f, vScale, zeroToOne ? 1.0f : 0.5f);
            shadowUv = toTexture * lightClip;
        }
        float data[kShadowUbufSize / 4] = {};
        float *v = putMat4(data, shadowUv);
        v = putMat4(v, lightClip);
        v[0] = shadows ? 1.0f : 0.0f;
        v[1] = float(qMax(0, shadowLight));
        v[2] = shadowSoftness;
        v[3] = 1.0f / ShadowMapSize;
        rub->updateDynamicBuffer(m_shadowUbuf.get(), 0, kShadowUbufSize, data);
    }

    // --- Overlay ---
    const quint32 overlayCount = quint32(m_overlay.size());
    if (overlayCount > 0) {
        const quint32 bytes = overlayCount * kOverlayVertexSize;
        if (!m_overlayVbuf || m_overlayVbuf->size() < bytes) {
            m_overlayVbuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, qMax(bytes * 2, 4096u)));
            m_overlayVbuf->create();
        }
        rub->updateDynamicBuffer(m_overlayVbuf.get(), 0, bytes, m_overlay.data());
        const float p[4] = { m_pixelSize.width() / m_overlayScale, m_pixelSize.height() / m_overlayScale,
                             m_rhi->isYUpInNDC() ? 1.0f : 0.0f, 0.0f };
        rub->updateDynamicBuffer(m_overlayUbuf.get(), 0, 16, p);
    }

    // --- Display uniforms: x = exposure (EV), y = tonemapper index, zw free ---
    {
        const float p[4] = { m_exposure, float(m_tonemapper), 0.0f, 0.0f };
        rub->updateDynamicBuffer(m_displayUbuf.get(), 0, 16, p);
    }

    // --- Blur uniforms ---
    {
        const float down[4] = { 1.0f / m_pixelSize.width(), 1.0f / m_pixelSize.height(), 0, 0 };
        rub->updateDynamicBuffer(m_downUbuf.get(), 0, 16, down);
        const float tx = 1.0f / blurSize.width();
        const float ty = 1.0f / blurSize.height();
        for (int i = 0; i < 4; ++i) {
            const float p[4] = { (i % 2 == 0) ? tx : 0.0f, (i % 2 == 0) ? 0.0f : ty, kBlurRadius, 0 };
            rub->updateDynamicBuffer(m_blurUbufs[i].get(), 0, 16, p);
        }
    }

    // --- Composite uniforms: glass rectangles ---
    {
        float data[kCompositeUbufSize / 4] = {};
        for (int i = 0; i < m_glassRects.size(); ++i) {
            const QRectF &r = m_glassRects[i];
            float *rect = data + 8 + i * 4;
            rect[0] = float(r.x());
            rect[1] = float(r.y());
            rect[2] = float(r.width());
            rect[3] = float(r.height());
        }
        data[0] = float(m_glassRects.size());
        data[1] = m_glassRadius;
        data[2] = m_rhi->isYUpInFramebuffer() ? 1.0f : 0.0f;
        data[4] = float(m_pixelSize.width());
        data[5] = float(m_pixelSize.height());
        rub->updateDynamicBuffer(m_compositeUbuf.get(), 0, kCompositeUbufSize, data);
    }

    const QRhiViewport fullViewport(0, 0, float(m_pixelSize.width()), float(m_pixelSize.height()));
    auto fullscreen = [cb](QRhiGraphicsPipeline *pipe, QRhiShaderResourceBindings *srb, const QSize &s) {
        cb->setGraphicsPipeline(pipe);
        cb->setViewport({ 0, 0, float(s.width()), float(s.height()) });
        cb->setShaderResources(srb);
        cb->draw(3);
    };
    auto drawMesh = [&](QRhiGraphicsPipeline *pipe, const Draw &d) {
        cb->setGraphicsPipeline(pipe);
        cb->setViewport(fullViewport);
        cb->setShaderResources(d.srb);
        const QRhiCommandBuffer::VertexInput vin(d.mesh->vertices.get(), 0);
        cb->setVertexInput(0, 1, &vin, d.mesh->indices.get(), 0, QRhiCommandBuffer::IndexUInt32);
        cb->drawIndexed(d.mesh->indexCount);
    };
    const QColor black(Qt::black);

    // Stats: grid + display + downsample + 4 blur + composite = 8 fullscreen draws in 8 passes,
    // plus the shadow pass.
    m_stats = {};
    m_stats.meshes = int(draws.size());
    m_stats.renderPasses = 9 + iblPasses;
    m_stats.drawCalls = 8 + iblPasses + (overlayCount > 0 ? 1 : 0) + (showBackground ? 1 : 0);
    for (const Draw &d : draws) {
        const int times = d.selected ? 2 : 1; // the outline draws the mesh again
        m_stats.drawCalls += times;
        m_stats.triangles += quint64(d.mesh->indexCount / 3) * quint64(times);
    }
    if (shadows) {
        m_stats.drawCalls += int(draws.size());
        for (const Draw &d : draws)
            m_stats.triangles += d.mesh->indexCount / 3;
    }

    // 0. Shadow map: every mesh's depth from the light. Always cleared, so it's valid to
    //    sample even without shadows (mesh.frag checks shadowParams.x).
    cb->beginPass(m_shadowRt.get(), black, { 1.0f, 0 }, rub);
    if (shadows) {
        cb->setGraphicsPipeline(m_shadowPipe.get());
        cb->setViewport({ 0, 0, float(ShadowMapSize), float(ShadowMapSize) });
        for (const Draw &d : draws) {
            cb->setShaderResources(d.shadowSrb);
            const QRhiCommandBuffer::VertexInput vin(d.mesh->vertices.get(), 0);
            cb->setVertexInput(0, 1, &vin, d.mesh->indices.get(), 0, QRhiCommandBuffer::IndexUInt32);
            cb->drawIndexed(d.mesh->indexCount);
        }
    }
    cb->endPass();

    // 1. Scene (HDR, linear): environment and grid as background (no depth), meshes, selection outline
    cb->beginPass(m_sceneRt.get(), black, { 1.0f, 0 });
    if (showBackground)
        fullscreen(m_backgroundPipe.get(), m_backgroundSrb.get(), m_pixelSize);
    fullscreen(m_gridPipe.get(), m_gridSrb.get(), m_pixelSize);
    for (const Draw &d : draws)
        drawMesh(m_meshPipe.get(), d);
    for (const Draw &d : draws) {
        if (d.selected)
            drawMesh(m_outlinePipe.get(), d);
    }
    cb->endPass();

    // 2. Display (8 bit, sRGB): scene converted for the screen, then the overlay (gizmo) on top
    cb->beginPass(m_displayRt.get(), black, { 1.0f, 0 });
    fullscreen(m_displayPipe.get(), m_displaySrb.get(), m_pixelSize);
    if (overlayCount > 0) {
        cb->setGraphicsPipeline(m_overlayPipe.get());
        cb->setViewport(fullViewport);
        cb->setShaderResources(m_overlaySrb.get());
        const QRhiCommandBuffer::VertexInput vin(m_overlayVbuf.get(), 0);
        cb->setVertexInput(0, 1, &vin);
        cb->draw(overlayCount);
    }
    cb->endPass();

    // 3. Blur of the display image: downsample into A, then H (A->B), V (B->A), twice
    cb->beginPass(m_blurARt.get(), black, { 1.0f, 0 });
    fullscreen(m_downPipe.get(), m_downSrb.get(), blurSize);
    cb->endPass();
    for (int i = 0; i < 4; ++i) {
        cb->beginPass((i % 2 == 0) ? m_blurBRt.get() : m_blurARt.get(), black, { 1.0f, 0 });
        fullscreen(m_blurPipe.get(), m_blurSrbs[i].get(), blurSize);
        cb->endPass();
    }

    // 4. Final image: display image + blurred copy inside the glass rectangles
    cb->beginPass(target, black, { 1.0f, 0 });
    fullscreen(m_compositePipe.get(), m_compositeSrb.get(), m_pixelSize);
    cb->endPass();
}
