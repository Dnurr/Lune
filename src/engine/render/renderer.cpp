#include "renderer.h"

#include "camera.h"
#include "engine/material/materialcompiler.h"
#include "engine/scene/scene.h"

#include <QColor>
#include <QFile>
#include <rhi/qrhi.h>
#include <rhi/qshaderbaker.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace
{
constexpr int kBlurDownscale = 4; // blur resolution = target / 4
constexpr float kBlurRadius = 2;
constexpr int kGridUbufSize = 64 + 8 * 16;
constexpr int kCompositeUbufSize = 2 * 16 + Renderer::MaxGlassRects * 16;
// See mesh.vert; mesh.frag adds the node material values at the end.
constexpr int kEntityMaterialOffset = 3 * 64 + 4 * 16; // vec4 material
constexpr int kEntityParamsOffset = 3 * 64 + 5 * 16; // materialParams[0]
constexpr int kEntityUbufSize = kEntityParamsOffset + MaterialCompiler::MaxParams * 16;
constexpr int kOverlayVertexSize = int(sizeof(OverlayVertex));
// Lights uniform block (mesh.frag, binding 1): ivec4 header + MaxLights * 3 vec4.
constexpr int kLightSize = 3 * 16;
constexpr int kLightsUbufSize = 16 + Renderer::MaxLights * kLightSize;
// Environment uniform block (mesh.frag binding 2, background.frag binding 1): mat4 + vec4.
constexpr int kEnvUbufSize = 64 + 16;
constexpr int kBackgroundUbufSize = 64 + 2 * 16;

// Editor look of meshes.
const QColor kOutlineColor("#8FB0F2"); // palette primary (navy accent)
constexpr float kOutlineWidth = 2.0f;  // logical pixels
// Shading::Clay: one material for everything.
const QColor kClayColor("#B4B4B8");
constexpr float kClayRoughness = 0.6f;

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
    layout.setAttributes({ { 0, 0, QRhiVertexInputAttribute::Float3, quint32(offsetof(MeshVertex, position)) },
                           { 0, 1, QRhiVertexInputAttribute::Float3, quint32(offsetof(MeshVertex, normal)) },
                           { 0, 2, QRhiVertexInputAttribute::Float2, quint32(offsetof(MeshVertex, uv)) },
                           { 0, 3, QRhiVertexInputAttribute::Float4, quint32(offsetof(MeshVertex, tangent)) } });
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

bool Renderer::wireframeSupported() const
{
    return m_rhi && m_rhi->isFeatureSupported(QRhi::NonFillPolygonMode);
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
        m_distanceUbuf = ubuf(16);

        m_ibl.initialize(m_rhi);
        m_post.initialize(m_rhi);
        m_particles.initialize(m_rhi);
        m_fluids.initialize(m_rhi);
        m_shadows.initialize(m_rhi);
        m_textures.initialize(m_rhi);

        using B = QRhiShaderResourceBinding;
        const auto stages = B::VertexStage | B::FragmentStage;
        m_meshLayoutSrb.reset(m_rhi->newShaderResourceBindings());
        {
            const auto b = meshBindings(m_layoutUbuf.get(), fallbackTextures());
            m_meshLayoutSrb->setBindings(b.cbegin(), b.cend());
        }
        m_meshLayoutSrb->create();
        m_shadowLayoutSrb.reset(m_rhi->newShaderResourceBindings());
        {
            const auto b = casterBindings(m_layoutUbuf.get(), fallbackTextures());
            m_shadowLayoutSrb->setBindings(b.cbegin(), b.cend());
        }
        m_shadowLayoutSrb->create();
        m_outlineLayoutSrb.reset(m_rhi->newShaderResourceBindings());
        m_outlineLayoutSrb->setBindings({ B::uniformBuffer(0, stages, m_layoutUbuf.get()) });
        m_outlineLayoutSrb->create();
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
    const QRhiTexture::Format hdrFormat =
        m_rhi->isTextureFormatSupported(QRhiTexture::RGBA16F, QRhiTexture::RenderTarget) ? QRhiTexture::RGBA16F : QRhiTexture::RGBA8;
    m_sceneTex = texture(m_pixelSize, hdrFormat);
    // After the display pass the values are already for the screen (0..1, sRGB): 8 bits are enough.
    m_displayTex = texture(m_pixelSize, QRhiTexture::RGBA8);
    m_blurA = texture(blurSize, QRhiTexture::RGBA8);
    m_blurB = texture(blurSize, QRhiTexture::RGBA8);

    // Depth: a texture the post effects can sample where the backend allows it (no stencil
    // is used), a render buffer otherwise.
    QRhiTextureRenderTargetDescription sceneDesc { QRhiColorAttachment(m_sceneTex.get()) };
    m_sceneDepth.reset();
    m_sceneDepthTex.reset();
    if (m_rhi->isTextureFormatSupported(QRhiTexture::D32F)) {
        m_sceneDepthTex.reset(m_rhi->newTexture(QRhiTexture::D32F, m_pixelSize, 1, QRhiTexture::RenderTarget));
        m_sceneDepthTex->create();
        sceneDesc.setDepthTexture(m_sceneDepthTex.get());
    } else {
        m_sceneDepth.reset(m_rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, m_pixelSize));
        m_sceneDepth->create();
        sceneDesc.setDepthStencilBuffer(m_sceneDepth.get());
    }

    // Render pass descriptors are created once and reused after a resize (same formats),
    // so they always match the pipelines built with them.
    m_sceneRt.reset(m_rhi->newTextureRenderTarget(sceneDesc));
    if (!m_sceneRp)
        m_sceneRp.reset(m_sceneRt->newCompatibleRenderPassDescriptor());
    m_sceneRt->setRenderPassDescriptor(m_sceneRp.get());
    m_sceneRt->create();
    m_sceneKeepRt.reset(m_rhi->newTextureRenderTarget(sceneDesc, QRhiTextureRenderTarget::PreserveColorContents
                                                                     | QRhiTextureRenderTarget::PreserveDepthStencilContents));
    if (!m_sceneKeepRp)
        m_sceneKeepRp.reset(m_sceneKeepRt->newCompatibleRenderPassDescriptor());
    m_sceneKeepRt->setRenderPassDescriptor(m_sceneKeepRp.get());
    m_sceneKeepRt->create();

    // The scene's view depth for soft particles: one float per pixel (half floats where 32-bit
    // ones can't be rendered to).
    m_distanceSrb.reset();
    m_distanceRt.reset();
    m_distanceTex.reset();
    if (m_sceneDepthTex) {
        const QRhiTexture::Format distanceFormat =
            m_rhi->isTextureFormatSupported(QRhiTexture::R32F, QRhiTexture::RenderTarget) ? QRhiTexture::R32F : hdrFormat;
        m_distanceTex = texture(m_pixelSize, distanceFormat);
        m_distanceRt.reset(m_rhi->newTextureRenderTarget({ QRhiColorAttachment(m_distanceTex.get()) }));
        if (!m_distanceRp)
            m_distanceRp.reset(m_distanceRt->newCompatibleRenderPassDescriptor());
        m_distanceRt->setRenderPassDescriptor(m_distanceRp.get());
        m_distanceRt->create();
        using B = QRhiShaderResourceBinding;
        m_distanceSrb.reset(m_rhi->newShaderResourceBindings());
        m_distanceSrb->setBindings({ B::uniformBuffer(0, B::FragmentStage, m_distanceUbuf.get()),
                                     B::sampledTexture(1, B::FragmentStage, m_sceneDepthTex.get(), m_sampler.get()) });
        m_distanceSrb->create();
    }
    m_particles.setSceneDistance(m_distanceTex.get());
    m_fluids.setSceneDistance(m_distanceTex.get());

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
    m_post.resize(m_pixelSize, hdrFormat, m_sceneDepthTex.get(), m_ldrRp.get());

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
    for (int i = 0; i < 2; ++i)
        m_displayPostSrbs[size_t(i)] = srb({ B::uniformBuffer(0, stages, m_displayUbuf.get()),
                                             B::sampledTexture(1, B::FragmentStage, m_post.hdrTexture(i), m_sampler.get()) });
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
    m_backgroundPipe = pipeline(loadShader(QStringLiteral("background.vert")), QStringLiteral("background.frag"),
                                m_backgroundSrb.get(), m_sceneRp.get(), 1, background);
    m_gridPipe = pipeline(fullscreen, QStringLiteral("grid.frag"), m_gridSrb.get(), m_sceneRp.get(), 1, grid);
    m_meshPipe = pipeline(loadShader(QStringLiteral("mesh.vert")), QStringLiteral("mesh.frag"), m_meshLayoutSrb.get(),
                          m_sceneRp.get(), 1, mesh);
    for (auto &entry : m_materialShaders) { // recreated on next use, for the new render pass
        entry.second.pipeline.reset();
        for (auto &p : entry.second.blendPipelines)
            p.reset();
    }
    m_outlinePipe = pipeline(loadShader(QStringLiteral("outline.vert")), QStringLiteral("outline.frag"),
                             m_outlineLayoutSrb.get(), m_sceneRp.get(), 1, outline);
    // Edges: the triangles as lines, with the entity uniforms only (like the outline).
    m_wirePipe.reset();
    m_wireOverPipe.reset();
    if (wireframeSupported()) {
        const QShader wireVs = loadShader(QStringLiteral("wireframe.vert"));
        const QShader wireFs = loadShader(QStringLiteral("wireframe.frag"));
        auto wire = [&](bool over) {
            std::unique_ptr<QRhiGraphicsPipeline> p(m_rhi->newGraphicsPipeline());
            p->setShaderStages({ { QRhiShaderStage::Vertex, wireVs }, { QRhiShaderStage::Fragment, wireFs } });
            p->setVertexInputLayout(meshInputLayout());
            p->setShaderResourceBindings(m_outlineLayoutSrb.get());
            p->setRenderPassDescriptor(m_sceneRp.get());
            p->setPolygonMode(QRhiGraphicsPipeline::Line);
            QRhiGraphicsPipeline::TargetBlend b;
            b.enable = true;
            b.srcColor = QRhiGraphicsPipeline::One; // premultiplied alpha
            b.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
            b.srcAlpha = QRhiGraphicsPipeline::One;
            b.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
            p->setTargetBlends({ b });
            if (over) {
                // Over the surfaces they belong to: depth tested, pulled toward the camera so
                // the lines don't fight with their own triangles. Alone: every edge (see-through).
                p->setDepthTest(true);
                p->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
                p->setDepthBias(-50);
                p->setSlopeScaledDepthBias(-1.0f);
            }
            p->create();
            return p;
        };
        m_wirePipe = wire(false);
        m_wireOverPipe = wire(true);
    }
    m_displayPipe = pipeline(fullscreen, QStringLiteral("display.frag"), m_displaySrb.get(), m_ldrRp.get(), 1);
    m_post.createPipelines(fullscreen);
    m_particles.createPipelines(m_sceneKeepRp.get(), 1);
    m_fluids.createPipelines(m_sceneKeepRp.get(), 1);
    m_distancePipe.reset();
    if (m_distanceSrb)
        m_distancePipe = pipeline(fullscreen, QStringLiteral("scene_distance.frag"), m_distanceSrb.get(), m_distanceRp.get(), 1);
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
    m_particles.release();
    m_fluids.release();
    m_distancePipe.reset();
    m_distanceSrb.reset();
    m_distanceRt.reset();
    m_distanceTex.reset();
    m_distanceRp.reset();
    m_distanceUbuf.reset();
    m_sceneKeepRt.reset();
    m_sceneKeepRp.reset();
    m_materialShaders.clear();
    m_meshes.clear();
    m_entities.clear();
    m_gridPipe.reset();
    m_backgroundPipe.reset();
    m_meshPipe.reset();
    m_outlinePipe.reset();
    m_overlayPipe.reset();
    m_displayPipe.reset();
    m_downPipe.reset();
    m_blurPipe.reset();
    m_compositePipe.reset();
    m_gridSrb.reset();
    m_displaySrb.reset();
    for (auto &s : m_displayPostSrbs)
        s.reset();
    m_downSrb.reset();
    m_compositeSrb.reset();
    m_overlaySrb.reset();
    m_meshLayoutSrb.reset();
    m_shadowLayoutSrb.reset();
    m_outlineLayoutSrb.reset();
    m_backgroundSrb.reset();
    for (auto &s : m_blurSrbs)
        s.reset();
    m_sceneRt.reset();
    m_displayRt.reset();
    m_blurARt.reset();
    m_blurBRt.reset();
    m_sceneRp.reset();
    m_ldrRp.reset();
    m_sceneDepth.reset();
    m_sceneDepthTex.reset();
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
    m_shadows.release();
    m_ibl.release();
    m_post.release();
    m_textures.release();
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

Renderer::GpuSlot Renderer::newSlot()
{
    GpuSlot s;
    s.uniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kEntityUbufSize));
    s.uniforms->create();
    s.textures = fallbackTextures();
    s.srb.reset(m_rhi->newShaderResourceBindings());
    const auto bindings = meshBindings(s.uniforms.get(), s.textures);
    s.srb->setBindings(bindings.cbegin(), bindings.cend());
    s.srb->create();
    return s;
}

void Renderer::bindTextures(GpuSlot &slot, const TextureSet &textures)
{
    if (slot.textures == textures)
        return;
    slot.textures = textures;
    const auto bindings = meshBindings(slot.uniforms.get(), textures);
    slot.srb->setBindings(bindings.cbegin(), bindings.cend());
    slot.srb->create();
    if (slot.shadowSrb) {
        const auto shadow = casterBindings(slot.uniforms.get(), textures);
        slot.shadowSrb->setBindings(shadow.cbegin(), shadow.cend());
        slot.shadowSrb->create();
    }
}

QRhiShaderResourceBindings *Renderer::slotShadowSrb(GpuSlot &slot)
{
    if (!slot.shadowSrb) {
        slot.shadowSrb.reset(m_rhi->newShaderResourceBindings());
        const auto bindings = casterBindings(slot.uniforms.get(), slot.textures);
        slot.shadowSrb->setBindings(bindings.cbegin(), bindings.cend());
        slot.shadowSrb->create();
    }
    return slot.shadowSrb.get();
}

Renderer::TextureSet Renderer::fallbackTextures() const
{
    TextureSet set;
    set.fill(m_textures.fallback(TextureFallback::White));
    return set;
}

Renderer::GpuEntity &Renderer::gpuEntity(EntityId id, int slotCount)
{
    slotCount = qMax(1, slotCount);
    auto it = m_entities.find(id);
    if (it == m_entities.end()) {
        using B = QRhiShaderResourceBinding;
        GpuEntity g;
        g.materialSlots.push_back(newSlot());
        QRhiBuffer *uniforms = g.materialSlots[0].uniforms.get();
        g.outlineSrb.reset(m_rhi->newShaderResourceBindings());
        g.outlineSrb->setBindings({ B::uniformBuffer(0, B::VertexStage | B::FragmentStage, uniforms) });
        g.outlineSrb->create();
        g.shadowSrb.reset(m_rhi->newShaderResourceBindings());
        const auto shadow = m_shadows.casterBindings(uniforms);
        g.shadowSrb->setBindings(shadow.cbegin(), shadow.cend());
        g.shadowSrb->create();
        it = m_entities.emplace(id, std::move(g)).first;
    }
    GpuEntity &g = it->second;
    while (int(g.materialSlots.size()) < slotCount)
        g.materialSlots.push_back(newSlot());
    if (int(g.materialSlots.size()) > slotCount)
        g.materialSlots.resize(size_t(slotCount)); // slot 0 (outline, shadows) stays
    return g;
}

std::unique_ptr<QRhiGraphicsPipeline> Renderer::createMeshPipeline(const QShader &fragment, bool blend, MeshCull cull)
{
    std::unique_ptr<QRhiGraphicsPipeline> p(m_rhi->newGraphicsPipeline());
    p->setShaderStages({ { QRhiShaderStage::Vertex, loadShader(QStringLiteral("mesh.vert")) },
                         { QRhiShaderStage::Fragment, fragment } });
    p->setVertexInputLayout(meshInputLayout());
    p->setShaderResourceBindings(m_meshLayoutSrb.get());
    p->setRenderPassDescriptor(m_sceneRp.get());
    p->setCullMode(cull == CullFront  ? QRhiGraphicsPipeline::Front
                   : cull == CullBack ? QRhiGraphicsPipeline::Back
                                      : QRhiGraphicsPipeline::None); // two-sided, like m_meshPipe
    p->setDepthTest(true);
    p->setDepthWrite(!blend); // see-through surfaces don't hide what's drawn after them
    if (blend) {
        QRhiGraphicsPipeline::TargetBlend b;
        b.enable = true;
        b.srcColor = QRhiGraphicsPipeline::One; // premultiplied alpha (see Surface.alpha in mesh.frag)
        b.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        b.srcAlpha = QRhiGraphicsPipeline::One;
        b.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        p->setTargetBlends({ b });
    }
    if (!p->create())
        return nullptr;
    return p;
}

QShader Renderer::compileFragment(const QString &source, const QString &name, QString *error) const
{
    // Compiled for the backend in use only: much faster than every target.
    QShaderBaker baker;
    QList<QShaderBaker::GeneratedShader> targets;
    switch (m_rhi->backend()) {
    case QRhi::Vulkan:
        targets.append({ QShader::SpirvShader, QShaderVersion(100) });
        break;
    case QRhi::D3D11:
    case QRhi::D3D12:
        targets.append({ QShader::HlslShader, QShaderVersion(50) });
        break;
    case QRhi::Metal:
        targets.append({ QShader::MslShader, QShaderVersion(12) });
        break;
    case QRhi::OpenGLES2:
        // The same GLSL versions as the precompiled shaders (CMakeLists.txt).
        targets.append({ QShader::GlslShader, QShaderVersion(430) });
        targets.append({ QShader::GlslShader, QShaderVersion(310, QShaderVersion::GlslEs) });
        break;
    default:
        break;
    }
    baker.setGeneratedShaders(targets);
    baker.setGeneratedShaderVariants({ QShader::StandardShader });
    baker.setSourceString(source.toUtf8(), QShader::FragmentStage, name);
    QShader shader = baker.bake();
    if (!shader.isValid())
        *error = QStringLiteral("Shader error: ") + baker.errorMessage().trimmed();
    return shader;
}

Renderer::MaterialShader &Renderer::materialShader(const QString &code)
{
    auto it = m_materialShaders.find(code);
    if (it == m_materialShaders.end()) {
        MaterialShader s;
        if (m_meshFragSource.isEmpty()) {
            QFile f(QStringLiteral(":/shadersrc/mesh.frag"));
            if (f.open(QIODevice::ReadOnly))
                m_meshFragSource = QString::fromUtf8(f.readAll());
        }
        const QString source = MaterialCompiler::insertInto(m_meshFragSource, code);
        if (source.isEmpty())
            s.error = QStringLiteral("mesh.frag has no material markers (//@material-begin, //@material-end).");
        else
            s.fragment = compileFragment(source, QStringLiteral("material"), &s.error);
        it = m_materialShaders.emplace(code, std::move(s)).first;
    }
    return it->second;
}

QRhiGraphicsPipeline *Renderer::scenePipeline(MaterialShader &s, bool blend, QString *error, MeshCull cull)
{
    auto &pipeline = blend ? s.blendPipelines[size_t(cull)] : s.pipeline;
    if (s.error.isEmpty() && !pipeline) {
        pipeline = createMeshPipeline(s.fragment, blend, cull);
        if (!pipeline)
            s.error = QStringLiteral("The material's pipeline couldn't be created.");
    }
    *error = s.error;
    return s.error.isEmpty() ? pipeline.get() : nullptr;
}

QRhiGraphicsPipeline *Renderer::shadowPipeline(MaterialShader &s, const QString &code, bool translucent, QString *error)
{
    if (!s.shadowCompiled) {
        s.shadowCompiled = true;
        if (m_shadowMaterialSource.isEmpty()) {
            QFile f(QStringLiteral(":/shadersrc/shadow_material.glsl"));
            if (f.open(QIODevice::ReadOnly))
                m_shadowMaterialSource = QString::fromUtf8(f.readAll());
        }
        const QString source = MaterialCompiler::shadowSource(m_meshFragSource, code, m_shadowMaterialSource);
        if (source.isEmpty() || m_shadowMaterialSource.isEmpty())
            s.shadowError = QStringLiteral("The material's shadow shader can't be built.");
        else
            s.shadowFragment = compileFragment(source, QStringLiteral("material shadow"), &s.shadowError);
    }
    auto &pipeline = translucent ? s.translucentShadowPipeline : s.clipShadowPipeline;
    if (s.shadowError.isEmpty() && !pipeline) {
        pipeline = m_shadows.createMaterialPipeline(s.shadowFragment, m_shadowLayoutSrb.get(), translucent);
        if (!pipeline)
            s.shadowError = QStringLiteral("The material's shadow pipeline couldn't be created.");
    }
    *error = s.shadowError;
    return s.shadowError.isEmpty() ? pipeline.get() : nullptr;
}

bool Renderer::takeMaterialErrorsChanged()
{
    const bool changed = m_particles.takeErrorsChanged() | m_materialErrorsChanged;
    m_materialErrorsChanged = false;
    return changed;
}

QList<QRhiShaderResourceBinding> Renderer::meshBindings(QRhiBuffer *entityUniforms, const TextureSet &textures,
                                                        bool forCasters) const
{
    using B = QRhiShaderResourceBinding;
    const auto fs = B::FragmentStage;
    QList<QRhiShaderResourceBinding> b = { B::uniformBuffer(0, B::VertexStage | fs, entityUniforms),
                                           B::uniformBuffer(1, fs, m_lightsUbuf.get()),
                                           B::uniformBuffer(2, fs, m_envUbuf.get()),
                                           B::sampledTexture(3, fs, m_ibl.irradiance(), m_ibl.irradianceSampler()),
                                           B::sampledTexture(4, fs, m_ibl.prefiltered(), m_ibl.cubeSampler()),
                                           B::sampledTexture(5, fs, m_ibl.brdfLut(), m_ibl.lutSampler()) };
    b.append(m_shadows.meshBindings(forCasters)); // 6..10, 19
    for (int i = 0; i < MaterialCompiler::MaxTextures; ++i) // 11..18
        b.append(B::sampledTexture(MaterialCompiler::FirstTextureBinding + i, fs, textures[size_t(i)], m_textures.sampler()));
    return b;
}

QList<QRhiShaderResourceBinding> Renderer::casterBindings(QRhiBuffer *entityUniforms, const TextureSet &textures) const
{
    QList<QRhiShaderResourceBinding> b = meshBindings(entityUniforms, textures, true);
    b.append(m_shadows.materialViewBinding());
    return b;
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
    const DebugView view = m_view.shading == Shading::Unlit ? DebugView::Unlit : m_view.debug;
    // Data views (base color, normals, depth, shadows): shown without exposure or tonemapping,
    // and on black: no environment, no sky.
    const bool dataView = view != DebugView::None && view != DebugView::DirectOnly;
    const bool showBackground = hasEnvironment && environment.background && m_view.skybox && !dataView;
    // Wireframe without line drawing (OpenGL ES): like Clay.
    const bool wireOnly = m_view.shading == Shading::Wireframe && m_wirePipe;
    const bool clay = m_view.shading == Shading::Clay || (m_view.shading == Shading::Wireframe && !m_wirePipe);
    const bool sceneLights = m_view.shading != Shading::Preview; // Preview: the environment only
    const DebugView debugView = view;
    // Recomputes the maps (before anything samples them) only when the source changed.
    const int iblPasses = m_ibl.update(cb, { environment.image, environment.skyColor, environment.groundColor });

    QRhiResourceUpdateBatch *rub = m_rhi->nextResourceUpdateBatch();
    m_textures.beginFrame(rub); // images decoded since the last frame
    if (m_textures.takeErrorsChanged())
        m_materialErrorsChanged = true; // the UI shows image errors with the material's
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
        v = putLinearColor(v, QColor("#1C1C20"), showBackground || dataView ? 0.0f : 1.0f); // sky top
        v = putLinearColor(v, QColor("#121214"), 1.0f); // sky bottom / horizon
        // Line strengths: 0 hides them (setGridLinesVisible).
        const float lines = m_gridLines ? 1.0f : 0.0f;
        v = putLinearColor(v, QColor("#2E2E34"), lines);         // minor lines
        v = putLinearColor(v, QColor("#46464E"), lines);         // major lines
        v = putLinearColor(v, QColor("#C8434F"), 0.9f * lines);  // X axis
        putLinearColor(v, QColor("#3F6FD8"), 0.9f * lines);      // Z axis (navy-blue accent)
        rub->updateDynamicBuffer(m_gridUbuf.get(), 0, kGridUbufSize, data);
    }

    // --- Depth view (DebugView::Depth): 0 at the camera, 1 at the farthest mesh along the view ---
    float depthFar = 1.0f;
    if (debugView == DebugView::Depth && scene) {
        const QVector3D forward = (camera.target - eye).normalized();
        for (const Entity &e : scene->entities()) {
            if (!e.mesh || e.mesh->indices.empty() || !e.visible)
                continue;
            const QMatrix4x4 model = e.transform.matrix();
            const QVector3D &lo = e.mesh->boundsMin, &hi = e.mesh->boundsMax;
            for (int c = 0; c < 8; ++c) {
                const QVector3D corner = model.map(QVector3D(c & 1 ? hi.x() : lo.x(), c & 2 ? hi.y() : lo.y(),
                                                             c & 4 ? hi.z() : lo.z()));
                depthFar = qMax(depthFar, QVector3D::dotProduct(corner - eye, forward));
            }
        }
    }

    // --- Lights: one buffer for the frame, shared by every mesh ---
    std::vector<ShadowMaps::Light> shadowLights; // every light of the buffer, in order
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
        if (scene && sceneLights) {
            for (const Entity &e : scene->entities()) {
                if (!e.light || !e.light->enabled || count == MaxLights)
                    continue;
                shadowLights.push_back({ count, e.light->type == Light::Point, e.transform.position,
                                         Light::direction(e.transform), e.light->shadows, e.light->size,
                                         e.light->softness });
                put(e.light->type, e.transform.position, Light::direction(e.transform), e.light->color,
                    e.light->intensity);
            }
        }
        // A scene without lights (older files, or all disabled) would be black:
        // light it with a default sun so it can still be edited.
        if (count == 0 && sceneLights) {
            const Light sun;
            const QVector3D direction = QVector3D(-0.45f, -0.8f, -0.35f).normalized();
            shadowLights.push_back({ 0, false, QVector3D(), direction, true, sun.size, sun.softness });
            put(Light::Directional, QVector3D(), direction, Qt::white, 3.0f);
        }
        // y: what mesh.frag writes (DebugView), 0 = the shaded color; z: the depth view's
        // far distance (float bits).
        qint32 farBits = 0;
        std::memcpy(&farBits, &depthFar, sizeof(farBits));
        const qint32 header[4] = { count, qint32(debugView), farBits, 0 };
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
    // Whole meshes (shadow maps, outline), and their submeshes (scene pass, one material each).
    struct Draw
    {
        GpuMesh *mesh;
        QRhiShaderResourceBindings *outlineSrb;
        bool selected;
    };
    struct SurfaceDraw
    {
        GpuMesh *mesh;
        quint32 firstIndex, indexCount;
        QRhiShaderResourceBindings *srb;
        QRhiGraphicsPipeline *pipe; // m_meshPipe, or a node material's
        bool blend;                 // see-through: drawn after the others, far to near
        float distance2;            // squared distance from the camera to the entity's center
    };
    std::vector<Draw> draws;
    std::vector<SurfaceDraw> surfaces;
    std::vector<ShadowMaps::Caster> casters;
    int boundTextures = 0;
    QVector3D boundsMin(1e30f, 1e30f, 1e30f), boundsMax(-1e30f, -1e30f, -1e30f); // of every drawn mesh, world space

    // Node materials used this frame: generated code, pipeline and parameter layout.
    struct FrameMaterial
    {
        CompiledMaterial compiled;
        QRhiGraphicsPipeline *pipe = nullptr;       // scene pass
        // Blend, closed meshes: front faces culled (back faces drawn), back faces culled.
        QRhiGraphicsPipeline *cullFrontPipe = nullptr, *cullBackPipe = nullptr;
        QRhiGraphicsPipeline *shadowPipe = nullptr; // clip / blend; nullptr: drawn like an opaque one
        const NodeGraph *graph = nullptr;
        AlphaMode alphaMode = AlphaMode::Opaque;
        float alphaCutoff = 0.5f;
    };
    QHash<MaterialId, FrameMaterial> frameMaterials;
    QHash<MaterialId, QString> errors;
    auto frameMaterial = [&](MaterialId id) -> const FrameMaterial * {
        const Material *m = scene ? scene->material(id) : nullptr;
        if (!m)
            return nullptr;
        auto it = frameMaterials.find(id);
        if (it == frameMaterials.end()) {
            FrameMaterial fm;
            fm.graph = &m->graph;
            fm.alphaMode = m->alphaMode;
            fm.alphaCutoff = m->alphaCutoff;
            fm.compiled = MaterialCompiler::compile(m->graph, NodeLibrary::instance());
            QString error = fm.compiled.error;
            if (fm.compiled.ok()) {
                MaterialShader &shader = materialShader(fm.compiled.code);
                fm.pipe = scenePipeline(shader, fm.alphaMode == AlphaMode::Blend, &error);
                if (fm.pipe && fm.alphaMode == AlphaMode::Blend) {
                    QString cullError;
                    fm.cullFrontPipe = scenePipeline(shader, true, &cullError, CullFront);
                    fm.cullBackPipe = scenePipeline(shader, true, &cullError, CullBack);
                }
                if (fm.pipe && fm.alphaMode != AlphaMode::Opaque) {
                    QString shadowError;
                    fm.shadowPipe = shadowPipeline(shader, fm.compiled.code, fm.alphaMode == AlphaMode::Blend, &shadowError);
                    if (!shadowError.isEmpty())
                        error = QStringLiteral("Shadows: ") + shadowError; // still drawn, with opaque shadows
                }
            }
            errors.insert(id, error);
            it = frameMaterials.insert(id, std::move(fm));
        }
        return it->pipe ? &it.value() : nullptr;
    };

    {
        const QMatrix4x4 clipViewProj = m_rhi->clipSpaceCorrMatrix() * viewProj;
        std::unordered_map<EntityId, bool> seen;
        if (scene) {
            for (const Entity &e : scene->entities()) {
                if (!e.mesh || e.mesh->indices.empty() || !e.visible)
                    continue;
                GpuMesh &gm = gpuMesh(e.mesh, rub);
                const int slotCount = int(e.mesh->submeshes.size());
                GpuEntity &ge = gpuEntity(e.id, slotCount);
                seen[e.id] = true;

                // The uniforms every slot shares; each adds its material's values.
                const QMatrix4x4 model = e.transform.matrix();
                float common[kEntityUbufSize / 4] = {};
                float *v = putMat4(common, clipViewProj * model);
                v = putMat4(v, model);
                v = putMat4(v, QMatrix4x4(model.normalMatrix()));
                v = putLinearColor(v, clay ? kClayColor : e.displayColor, 1.0f);
                v = putLinearColor(v, kOutlineColor, kOutlineWidth * m_overlayScale);
                v[0] = float(m_pixelSize.width());
                v[1] = float(m_pixelSize.height());
                v[2] = wireOnly ? 1.0f : 0.0f; // wireframe.frag: light lines alone, dark ones over surfaces
                v[3] = m_seconds;              // scene time
                v += 4;
                v[0] = eye.x(); v[1] = eye.y(); v[2] = eye.z();
                v += 4;
                v[0] = clay ? kClayRoughness : e.roughness;
                v[1] = clay ? 0.0f : e.metallic;

                const QVector3D center = model.map((e.mesh->boundsMin + e.mesh->boundsMax) * 0.5f);
                const float distance2 = (center - eye).lengthSquared();
                const bool mirrored = model.determinant() < 0.0;
                // Shadows: the whole mesh in one draw, unless some slot has a clip or blend
                // material; then each slot on its own.
                struct SlotShadow
                {
                    quint32 firstIndex, indexCount;
                    QRhiGraphicsPipeline *pipe; // nullptr: depth only
                    QRhiShaderResourceBindings *srb;
                    bool translucent;
                };
                std::vector<SlotShadow> slotShadows;
                bool slotShadowsNeeded = false;

                const NodeLibrary &library = NodeLibrary::instance();
                for (int slot = 0; slot < slotCount; ++slot) {
                    GpuSlot &gs = ge.materialSlots[size_t(slot)];
                    float data[kEntityUbufSize / 4];
                    std::memcpy(data, common, sizeof(data));
                    TextureSet textures = fallbackTextures();
                    // Node material: the values of its unlinked inputs (one vec4 each), its textures.
                    const MaterialId materialId = e.material(slot);
                    const FrameMaterial *fm = materialId != NoMaterial ? frameMaterial(materialId) : nullptr;
                    if (clay)
                        fm = nullptr; // compiled anyway, so its errors still show
                    if (fm) {
                        v = data + kEntityParamsOffset / 4;
                        for (const MaterialParam &p : fm->compiled.params) {
                            const GraphNode *node = fm->graph->node(p.node);
                            const NodeType *type = node ? library.type(node->type) : nullptr;
                            const PortDesc *port = type ? type->input(p.port) : nullptr;
                            if (port) {
                                const QVariant value = MaterialCompiler::inputValue(*node, *port);
                                if (p.type == PortType::Vec3) {
                                    const QVector3D c = value.value<QVector3D>();
                                    v[0] = p.color ? sRGBToLinear(c.x()) : c.x();
                                    v[1] = p.color ? sRGBToLinear(c.y()) : c.y();
                                    v[2] = p.color ? sRGBToLinear(c.z()) : c.z();
                                } else if (p.type == PortType::Vec2) {
                                    const QVector2D c = value.value<QVector2D>();
                                    v[0] = c.x();
                                    v[1] = c.y();
                                } else {
                                    v[0] = value.toFloat();
                                }
                            }
                            v += 4;
                        }
                        for (size_t i = 0; i < fm->compiled.textures.size(); ++i) {
                            const MaterialTexture &t = fm->compiled.textures[i];
                            const GraphNode *node = fm->graph->node(t.node);
                            const NodeType *type = node ? library.type(node->type) : nullptr;
                            const PortDesc *port = type ? type->input(t.port) : nullptr;
                            if (!port)
                                continue;
                            const TextureValue value = TextureValue::fromVariant(node->values.value(port->name), port->linear);
                            textures[i] = m_textures.texture(value, port->fallback);
                            if (!value.image.isEmpty())
                                ++boundTextures;
                        }
                        // material.zw: alpha cutoff (clip only), alpha mode
                        v = data + kEntityMaterialOffset / 4;
                        v[2] = fm->alphaMode == AlphaMode::Clip ? fm->alphaCutoff : 0.0f;
                        v[3] = float(int(fm->alphaMode));
                    }
                    rub->updateDynamicBuffer(gs.uniforms.get(), 0, kEntityUbufSize, data);
                    bindTextures(gs, textures);

                    const Submesh &sm = e.mesh->submeshes[size_t(slot)];
                    const bool blend = fm && fm->alphaMode == AlphaMode::Blend;
                    if (sm.indexCount > 0) {
                        if (blend && e.mesh->closed && fm->cullFrontPipe && fm->cullBackPipe) {
                            // A closed see-through mesh (a glass cube): its far side first, then
                            // its near side over it. Two-sided in one draw, the faces would blend
                            // in index order, the far ones sometimes over the near ones.
                            // (A mirrored model matrix flips which faces are the back ones.)
                            surfaces.push_back({ &gm, sm.firstIndex, sm.indexCount, gs.srb.get(),
                                                 mirrored ? fm->cullBackPipe : fm->cullFrontPipe, blend, distance2 });
                            surfaces.push_back({ &gm, sm.firstIndex, sm.indexCount, gs.srb.get(),
                                                 mirrored ? fm->cullFrontPipe : fm->cullBackPipe, blend, distance2 });
                        } else {
                            surfaces.push_back({ &gm, sm.firstIndex, sm.indexCount, gs.srb.get(),
                                                 fm ? fm->pipe : m_meshPipe.get(), blend, distance2 });
                        }
                        QRhiGraphicsPipeline *shadowPipe = fm ? fm->shadowPipe : nullptr;
                        slotShadows.push_back({ sm.firstIndex, sm.indexCount, shadowPipe,
                                                shadowPipe ? slotShadowSrb(gs) : nullptr, shadowPipe && blend });
                        slotShadowsNeeded |= shadowPipe != nullptr;
                    }
                }

                if (!slotShadowsNeeded) {
                    casters.push_back({ gm.vertices.get(), gm.indices.get(), 0, gm.indexCount, ge.shadowSrb.get(),
                                        e.mesh->closed, mirrored });
                } else {
                    for (const SlotShadow &s : slotShadows) {
                        if (s.pipe)
                            casters.push_back({ gm.vertices.get(), gm.indices.get(), s.firstIndex, s.indexCount, s.srb,
                                                false, mirrored, s.pipe, s.translucent });
                        else
                            casters.push_back({ gm.vertices.get(), gm.indices.get(), s.firstIndex, s.indexCount,
                                                ge.shadowSrb.get(), e.mesh->closed, mirrored });
                    }
                }
                draws.push_back({ &gm, ge.outlineSrb.get(), e.id == selected });

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

    if (errors != m_materialErrors) {
        m_materialErrors = errors;
        m_materialErrorsChanged = true;
    }

    // --- Shadow maps: views fitted to the meshes, uniforms ---
    {
        const QMatrix4x4 toWorld = camera.view().inverted();
        ShadowMaps::Camera shadowCamera;
        shadowCamera.eye = eye;
        shadowCamera.forward = (camera.target - eye).normalized();
        shadowCamera.right = toWorld.mapVector(QVector3D(1, 0, 0)).normalized();
        shadowCamera.up = toWorld.mapVector(QVector3D(0, 1, 0)).normalized();
        shadowCamera.fovY = camera.fieldOfView;
        shadowCamera.aspect = aspect;
        shadowCamera.nearPlane = camera.nearPlane;
        shadowCamera.farPlane = camera.farPlane;
        shadowCamera.shiftX = camera.shiftX;
        m_shadows.prepare(rub, shadowLights, shadowCamera, boundsMin, boundsMax, !draws.empty());
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

    // --- Post-processing: the scene camera's effects, in Rendered shading ---
    bool post = false;
    if (scene && m_view.post && m_view.shading == Shading::Rendered && m_view.debug == DebugView::None) {
        const Entity *cam = scene->sceneCamera();
        if (cam && cam->postProcess && cam->postProcess->enabled) {
            PostProcessor::Frame frame;
            frame.settings = *cam->postProcess;
            frame.nearPlane = camera.nearPlane;
            frame.farPlane = camera.farPlane;
            frame.fieldOfView = camera.fieldOfView;
            frame.seconds = m_seconds;
            m_post.prepare(rub, frame);
            post = m_post.passCount() > 0;
        }
    }
    const int ldrPasses = post ? m_post.ldrPassCount() : 0;

    // --- Display uniforms: x = exposure (EV), y = tonemapper index, zw free ---
    {
        const float p[4] = { dataView ? 0.0f : m_exposure, dataView ? 0.0f : float(m_tonemapper), 0.0f, 0.0f };
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
    auto drawMesh = [&](QRhiGraphicsPipeline *pipe, QRhiShaderResourceBindings *srb, GpuMesh *mesh,
                        quint32 firstIndex, quint32 indexCount) {
        cb->setGraphicsPipeline(pipe);
        cb->setViewport(fullViewport);
        cb->setShaderResources(srb);
        const QRhiCommandBuffer::VertexInput vin(mesh->vertices.get(), 0);
        cb->setVertexInput(0, 1, &vin, mesh->indices.get(), 0, QRhiCommandBuffer::IndexUInt32);
        cb->drawIndexed(indexCount, 1, firstIndex);
    };
    const QColor black(Qt::black);

    // Stats: grid + display + downsample + 4 blur + composite = 8 fullscreen draws in 8 passes,
    // plus the shadow atlases.
    m_stats = {};
    m_stats.meshes = int(draws.size());
    m_stats.textures = boundTextures;
    m_stats.renderPasses = 8 + m_shadows.passCount() + iblPasses + (post ? m_post.passCount() : 0);
    m_stats.drawCalls = 8 + iblPasses + (overlayCount > 0 ? 1 : 0) + (showBackground ? 1 : 0) + (post ? m_post.passCount() : 0);
    for (const SurfaceDraw &s : surfaces) {
        m_stats.drawCalls += 1;
        m_stats.triangles += s.indexCount / 3;
    }
    for (const Draw &d : draws) {
        if (d.selected) { // the outline draws the mesh again
            m_stats.drawCalls += 1;
            m_stats.triangles += d.mesh->indexCount / 3;
        }
    }
    for (const ShadowMaps::Caster &c : casters) { // once per shadow map tile
        m_stats.drawCalls += m_shadows.viewCount();
        m_stats.triangles += quint64(c.indexCount / 3) * quint64(m_shadows.viewCount());
    }

    // 0. Particles up to the scene time (compute; drawn in the scene pass), then the shadow
    //    atlases (every mesh's depth from each shadowed light), translucency map
    const bool particles = !wireOnly && !dataView;
    m_particles.simulate(cb, particles ? scene : nullptr, m_seconds);
    {
        const QVector4D back = camera.view().row(2); // the view's +Z: from the scene toward the eye
        m_particles.sort(cb, eye, -back.toVector3D().normalized());
    }
    m_particles.prepareDraw(rub, m_rhi->clipSpaceCorrMatrix() * viewProj, camera.view(), eye, m_pixelSize, m_seconds);
    m_stats.emitters = m_particles.emitterCount();
    m_stats.particles = m_particles.particleCount();
    m_stats.particleSteps = m_particles.stepsLastFrame();
    m_stats.drawCalls += m_stats.emitters;

    // Fluids up to the scene time (compute), lit by the main light: the first directional one.
    m_fluids.simulate(cb, particles ? scene : nullptr, m_seconds);
    {
        // The scene's first directional light; with no lights at all, the default sun (as for
        // the meshes); point lights only: no main light.
        QVector3D lightDir = QVector3D(-0.45f, -0.8f, -0.35f).normalized(), lightColor(3.0f, 3.0f, 3.0f);
        bool hasLight = sceneLights;
        bool anyLight = false;
        bool sun = false;
        for (const Entity &e : scene ? scene->entities() : std::vector<Entity>()) {
            if (!e.light || !e.light->enabled)
                continue;
            anyLight = true;
            if (!sun && e.light->type == Light::Directional) {
                lightDir = Light::direction(e.transform);
                float c[4];
                putLinearColor(c, e.light->color, 1.0f);
                lightColor = QVector3D(c[0], c[1], c[2]) * e.light->intensity;
                sun = true;
            }
        }
        if (anyLight && !sun)
            hasLight = false;
        m_fluids.prepareDraw(rub, m_rhi->clipSpaceCorrMatrix() * viewProj, eye, lightDir.normalized(), lightColor, hasLight,
                             m_pixelSize, m_seconds);
    }
    m_stats.fluids = m_fluids.volumeCount();
    m_stats.voxels = m_fluids.voxelCount();
    m_stats.fluidSteps = m_fluids.stepsLastFrame();
    m_stats.drawCalls += m_stats.fluids;
    const bool particlePass = m_stats.emitters > 0 || m_stats.fluids > 0;
    const bool distancePass = particlePass && m_distancePipe;
    if (distancePass) {
        const float planes[4] = { camera.nearPlane, camera.farPlane, 0.0f, 0.0f };
        rub->updateDynamicBuffer(m_distanceUbuf.get(), 0, 16, planes);
        m_stats.drawCalls += 1;
    }
    m_stats.renderPasses += (particlePass ? 1 : 0) + (distancePass ? 1 : 0);
    m_shadows.render(cb, rub, casters);

    // 1. Scene (HDR, linear): environment and grid as background (no depth), opaque meshes,
    //    see-through ones far to near, selection outline
    std::stable_sort(surfaces.begin(), surfaces.end(), [](const SurfaceDraw &a, const SurfaceDraw &b) {
        if (a.blend != b.blend)
            return !a.blend;
        return a.blend && a.distance2 > b.distance2;
    });
    cb->beginPass(m_sceneRt.get(), black, { 1.0f, 0 });
    if (showBackground)
        fullscreen(m_backgroundPipe.get(), m_backgroundSrb.get(), m_pixelSize);
    fullscreen(m_gridPipe.get(), m_gridSrb.get(), m_pixelSize);
    if (!wireOnly) {
        for (const SurfaceDraw &s : surfaces)
            drawMesh(s.pipe, s.srb, s.mesh, s.firstIndex, s.indexCount);
    }
    QRhiGraphicsPipeline *wirePipe = wireOnly ? m_wirePipe.get() : m_view.wireOverlay ? m_wireOverPipe.get() : nullptr;
    if (wirePipe) {
        for (const Draw &d : draws)
            drawMesh(wirePipe, d.outlineSrb, d.mesh, 0, d.mesh->indexCount);
    }
    for (const Draw &d : draws) {
        if (d.selected)
            drawMesh(m_outlinePipe.get(), d.outlineSrb, d.mesh, 0, d.mesh->indexCount);
    }
    cb->endPass();

    //    Particles: the scene's view depth first (soft particles read it), then the scene
    //    again without clearing it, the particles depth tested against the meshes.
    if (particlePass) {
        if (distancePass) {
            cb->beginPass(m_distanceRt.get(), black, { 1.0f, 0 });
            fullscreen(m_distancePipe.get(), m_distanceSrb.get(), m_pixelSize);
            cb->endPass();
        }
        cb->beginPass(m_sceneKeepRt.get(), black, { 1.0f, 0 });
        m_fluids.draw(cb, fullViewport);
        m_particles.draw(cb, fullViewport);
        cb->endPass();
    }

    //    Post: HDR effects (depth of field, bloom) on the scene
    QRhiTexture *hdr = post ? m_post.renderHdr(cb, m_sceneTex.get()) : m_sceneTex.get();
    QRhiShaderResourceBindings *displaySrb = hdr == m_sceneTex.get() ? m_displaySrb.get()
        : hdr == m_post.hdrTexture(0)                                 ? m_displayPostSrbs[0].get()
                                                                      : m_displayPostSrbs[1].get();

    // 2. Display (8 bit, sRGB): scene converted for the screen, the display effects, then the
    //    overlay (gizmo) on top. The display effects ping-pong with the post target so that
    //    the last one (or the display pass itself) writes the display image.
    auto drawOverlay = [&] {
        if (overlayCount == 0)
            return;
        cb->setGraphicsPipeline(m_overlayPipe.get());
        cb->setViewport(fullViewport);
        cb->setShaderResources(m_overlaySrb.get());
        const QRhiCommandBuffer::VertexInput vin(m_overlayVbuf.get(), 0);
        cb->setVertexInput(0, 1, &vin);
        cb->draw(overlayCount);
    };
    // Target of the step that has `remaining` display effects after it.
    auto ldrStep = [&](int remaining) -> std::pair<QRhiTextureRenderTarget *, QRhiTexture *> {
        if (remaining % 2 == 0)
            return { m_displayRt.get(), m_displayTex.get() };
        return { m_post.ldrTarget(), m_post.ldrTexture() };
    };
    auto [displayTarget, displayed] = ldrStep(ldrPasses);
    cb->beginPass(displayTarget, black, { 1.0f, 0 });
    fullscreen(m_displayPipe.get(), displaySrb, m_pixelSize);
    if (ldrPasses == 0)
        drawOverlay();
    cb->endPass();
    for (int i = 0; i < ldrPasses; ++i) {
        const auto [target, texture] = ldrStep(ldrPasses - 1 - i);
        cb->beginPass(target, black, { 1.0f, 0 });
        m_post.drawLdr(cb, i, displayed, m_pixelSize);
        if (i == ldrPasses - 1)
            drawOverlay();
        cb->endPass();
        displayed = texture;
    }

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
