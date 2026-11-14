#include "shadowmaps.h"

#include "engine/scene/mesh.h"

#include <QFile>
#include <rhi/qrhi.h>

#include <cmath>
#include <cstring>

namespace
{
// Shadow block of mesh.frag (binding 6), std140. Offsets in floats.
constexpr int kLightShadow = 0;                                                     // vec4[MaxLights]
constexpr int kDirMatrix = kLightShadow + 4 * ShadowMaps::MaxLights;                // mat4[DirectionalViews]
constexpr int kDirTile = kDirMatrix + 16 * ShadowMaps::DirectionalViews;            // vec4[DirectionalViews]
constexpr int kDirRange = kDirTile + 4 * ShadowMaps::DirectionalViews;              // vec4[DirectionalViews]
constexpr int kPointMatrix = kDirRange + 4 * ShadowMaps::DirectionalViews;          // mat4[MaxPoint * 6]
constexpr int kPointTile = kPointMatrix + 16 * 6 * ShadowMaps::MaxPoint;            // vec4[MaxPoint * 6]
constexpr int kPointRange = kPointTile + 4 * 6 * ShadowMaps::MaxPoint;              // vec4[MaxPoint]
constexpr int kAtlas = kPointRange + 4 * ShadowMaps::MaxPoint;                      // vec4
constexpr int kCascadeSplits = kAtlas + 4;                                          // vec4
constexpr int kCameraForward = kCascadeSplits + 4;                                  // vec4
constexpr int kUniformFloats = kCameraForward + 4;
constexpr int kUniformSize = kUniformFloats * 4;

constexpr int kLayoutUbufSize = 512; // at least the entity block of mesh.vert
// Hardware depth bias of the shadow passes, growing with the surface's slope as seen from
// the light: removes most shadow acne on its own. The shader can add its own on top.
constexpr float kSlopeBias = 1.5f;
constexpr float kPointNear = 0.05f;

QShader loadShader(const QString &name)
{
    QFile f(QStringLiteral(":/shaders/") + name + QStringLiteral(".qsb"));
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("ShadowMaps: missing shader %s", qPrintable(name));
        return {};
    }
    return QShader::fromSerialized(f.readAll());
}

void putMat4(float *dst, const QMatrix4x4 &m)
{
    std::memcpy(dst, m.constData(), 64); // column-major, as std140 expects
}

void putVec4(float *dst, float x, float y, float z, float w)
{
    dst[0] = x;
    dst[1] = y;
    dst[2] = z;
    dst[3] = w;
}
} // namespace

ShadowMaps::ShadowMaps() = default;

ShadowMaps::~ShadowMaps() = default;

void ShadowMaps::initialize(QRhi *rhi)
{
    release();
    m_rhi = rhi;

    // The most precise depth format available.
    QRhiTexture::Format format = QRhiTexture::D16;
    for (QRhiTexture::Format f : { QRhiTexture::D32F, QRhiTexture::D24 }) {
        if (m_rhi->isTextureFormatSupported(f)) {
            format = f;
            break;
        }
    }
    auto atlas = [&](const QSize &size, std::unique_ptr<QRhiTexture> &tex, std::unique_ptr<QRhiTextureRenderTarget> &rt) {
        tex.reset(m_rhi->newTexture(format, size, 1, QRhiTexture::RenderTarget));
        tex->create();
        QRhiTextureRenderTargetDescription desc;
        desc.setDepthTexture(tex.get());
        rt.reset(m_rhi->newTextureRenderTarget(desc));
        if (!m_rp)
            m_rp.reset(rt->newCompatibleRenderPassDescriptor()); // same format: valid for both
        rt->setRenderPassDescriptor(m_rp.get());
        rt->create();
    };
    atlas(QSize(DirectionalAtlasSize, DirectionalAtlasSize), m_directional, m_directionalRt);
    atlas(QSize(6 * PointFaceSize, MaxPoint * PointFaceSize), m_point, m_pointRt);

    // texture(map, vec3(uv, depth)) = 1 where depth <= the stored depth, blended across 4 texels.
    m_compareSampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                             QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_compareSampler->setTextureCompareOp(QRhiSampler::LessOrEqual);
    m_compareSampler->create();
    // Raw depth, one texel at a time.
    m_depthSampler.reset(m_rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None,
                                           QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_depthSampler->create();

    auto ubuf = [this](quint32 size) {
        std::unique_ptr<QRhiBuffer> b(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, size));
        b->create();
        return b;
    };
    m_uniforms = ubuf(kUniformSize);
    m_viewStride = m_rhi->ubufAligned(64);
    m_viewUbuf = ubuf(m_viewStride * ViewCount);
    m_layoutUbuf = ubuf(kLayoutUbufSize);

    m_layoutSrb.reset(m_rhi->newShaderResourceBindings());
    const auto b = casterBindings(m_layoutUbuf.get());
    m_layoutSrb->setBindings(b.cbegin(), b.cend());
    m_layoutSrb->create();

    createPipeline();
}

void ShadowMaps::createPipeline()
{
    QRhiVertexInputLayout input; // positions only
    input.setBindings({ { quint32(sizeof(MeshVertex)) } });
    input.setAttributes({ { 0, 0, QRhiVertexInputAttribute::Float3, 0 } });

    m_pipeline.reset(m_rhi->newGraphicsPipeline());
    m_pipeline->setShaderStages({ { QRhiShaderStage::Vertex, loadShader(QStringLiteral("shadow.vert")) },
                                  { QRhiShaderStage::Fragment, loadShader(QStringLiteral("shadow.frag")) } });
    m_pipeline->setVertexInputLayout(input);
    m_pipeline->setShaderResourceBindings(m_layoutSrb.get());
    m_pipeline->setRenderPassDescriptor(m_rp.get());
    m_pipeline->setCullMode(QRhiGraphicsPipeline::None); // two-sided, like the meshes
    m_pipeline->setDepthTest(true);
    m_pipeline->setDepthWrite(true);
    m_pipeline->setSlopeScaledDepthBias(kSlopeBias);
    m_pipeline->create();
}

void ShadowMaps::release()
{
    m_pipeline.reset();
    m_layoutSrb.reset();
    m_layoutUbuf.reset();
    m_viewUbuf.reset();
    m_uniforms.reset();
    m_compareSampler.reset();
    m_depthSampler.reset();
    m_directionalRt.reset();
    m_pointRt.reset();
    m_rp.reset();
    m_directional.reset();
    m_point.reset();
    m_views.clear();
    m_rhi = nullptr;
}

QList<QRhiShaderResourceBinding> ShadowMaps::meshBindings() const
{
    using B = QRhiShaderResourceBinding;
    const auto fs = B::FragmentStage;
    return { B::uniformBuffer(6, fs, m_uniforms.get()),
             B::sampledTexture(7, fs, m_directional.get(), m_compareSampler.get()),
             B::sampledTexture(8, fs, m_directional.get(), m_depthSampler.get()),
             B::sampledTexture(9, fs, m_point.get(), m_compareSampler.get()),
             B::sampledTexture(10, fs, m_point.get(), m_depthSampler.get()) };
}

QList<QRhiShaderResourceBinding> ShadowMaps::casterBindings(QRhiBuffer *entityUniforms) const
{
    using B = QRhiShaderResourceBinding;
    return { B::uniformBuffer(0, B::VertexStage, entityUniforms),
             B::uniformBufferWithDynamicOffset(1, B::VertexStage, m_viewUbuf.get(), 64) };
}

QRectF ShadowMaps::tileViewport(bool point, int tile, int directionalCount) const
{
    if (point) {
        const int face = tile % 6, slot = tile / 6;
        return QRectF(face * PointFaceSize, slot * PointFaceSize, PointFaceSize, PointFaceSize);
    }
    const int slot = tile / CascadeCount, cascade = tile % CascadeCount;
    if (directionalCount <= 1) { // one light: 2 x 2 tiles, half the atlas each way
        const int size = DirectionalAtlasSize / 2;
        return QRectF((cascade % 2) * size, (cascade / 2) * size, size, size);
    }
    const int size = DirectionalAtlasSize / CascadeCount; // a row per light
    return QRectF(cascade * size, slot * size, size, size);
}

// Clip space (backend-corrected) -> (u, v, depth) in the atlas, for a tile drawn with
// `viewport` (QRhi convention: origin bottom-left).
QMatrix4x4 ShadowMaps::clipToTile(const QRectF &viewport, const QSize &atlas) const
{
    // Clip space -> texture coordinates of the whole target: x, y from -1..1 to 0..1 (v flipped
    // where texture rows and clip-space y disagree), depth to 0..1 where the backend's clip
    // depth is -1..1 (OpenGL).
    const bool zeroToOne = m_rhi->isClipDepthZeroToOne();
    const float vScale = m_rhi->isYUpInFramebuffer() == m_rhi->isYUpInNDC() ? 0.5f : -0.5f;
    QMatrix4x4 toTexture;
    toTexture.translate(0.5f, 0.5f, zeroToOne ? 0.0f : 0.5f);
    toTexture.scale(0.5f, vScale, zeroToOne ? 1.0f : 0.5f);

    // The viewport's rows in texture coordinates: from the bottom where framebuffers are
    // y-up (OpenGL), from the top elsewhere.
    const float w = float(atlas.width()), h = float(atlas.height());
    const float u0 = float(viewport.x()) / w, u1 = float(viewport.x() + viewport.width()) / w;
    float v0 = float(viewport.y()) / h, v1 = float(viewport.y() + viewport.height()) / h;
    if (!m_rhi->isYUpInFramebuffer()) {
        const float top = 1.0f - v1;
        v1 = 1.0f - v0;
        v0 = top;
    }
    QMatrix4x4 toTile;
    toTile.translate(u0, v0, 0.0f);
    toTile.scale(u1 - u0, v1 - v0, 1.0f);
    return toTile * toTexture;
}

void ShadowMaps::prepare(QRhiResourceUpdateBatch *rub, const std::vector<Light> &lights, const Camera &camera,
                         const QVector3D &boundsMin, const QVector3D &boundsMax, bool hasMeshes)
{
    m_views.clear();
    float data[kUniformFloats] = {};
    for (int i = 0; i < MaxLights; ++i)
        data[kLightShadow + 4 * i] = -1.0f; // no shadows

    const QSize directionalAtlas = m_directional->pixelSize();
    const QSize pointAtlas = m_point->pixelSize();
    putVec4(data + kAtlas, 1.0f / directionalAtlas.width(), 1.0f / directionalAtlas.height(),
            1.0f / pointAtlas.width(), 1.0f / pointAtlas.height());

    const QMatrix4x4 corr = m_rhi->clipSpaceCorrMatrix();
    QVector3D corners[8]; // of the meshes' bounds
    for (int c = 0; c < 8; ++c)
        corners[c] = QVector3D(c & 1 ? boundsMax.x() : boundsMin.x(), c & 2 ? boundsMax.y() : boundsMin.y(),
                               c & 4 ? boundsMax.z() : boundsMin.z());

    // Cascade splits: view distances along the camera's forward axis, from the near plane to
    // the farthest mesh (no shadows are needed past it).
    float shadowFar = camera.nearPlane * 10.0f;
    for (const QVector3D &c : corners)
        shadowFar = qMax(shadowFar, QVector3D::dotProduct(c - camera.eye, camera.forward));
    shadowFar = qMin(shadowFar * 1.01f, camera.farPlane);
    float splits[CascadeCount + 1];
    splits[0] = camera.nearPlane;
    for (int i = 1; i <= CascadeCount; ++i) {
        const float t = float(i) / CascadeCount;
        const float logSplit = camera.nearPlane * std::pow(shadowFar / camera.nearPlane, t);
        const float uniformSplit = camera.nearPlane + (shadowFar - camera.nearPlane) * t;
        splits[i] = CascadeSplitBlend * logSplit + (1.0f - CascadeSplitBlend) * uniformSplit;
    }
    putVec4(data + kCascadeSplits, splits[1], splits[2], splits[3], splits[4]);
    putVec4(data + kCameraForward, camera.forward.x(), camera.forward.y(), camera.forward.z(), 0.0f);

    // Bounding sphere of each slice of the camera's view. Its size doesn't change when the
    // camera turns, which (with texel snapping below) keeps shadows from shimmering.
    const float tanY = std::tan(qDegreesToRadians(camera.fovY) * 0.5f), tanX = tanY * camera.aspect;
    QVector3D sliceCenter[CascadeCount];
    float sliceRadius[CascadeCount];
    for (int i = 0; i < CascadeCount; ++i) {
        QVector3D pts[8];
        QVector3D sum;
        for (int k = 0; k < 8; ++k) {
            const float d = splits[i + (k & 1)];
            pts[k] = camera.eye + camera.forward * d + camera.right * (d * tanX * (k & 2 ? 1.0f : -1.0f))
                + camera.up * (d * tanY * (k & 4 ? 1.0f : -1.0f));
            sum += pts[k];
        }
        sliceCenter[i] = sum / 8.0f;
        float r = 0.0f;
        for (const QVector3D &q : pts)
            r = qMax(r, (q - sliceCenter[i]).length());
        sliceRadius[i] = std::ceil(r * 16.0f) / 16.0f; // rounded up: stable while the camera moves slightly
    }

    int directionalTotal = 0; // how many directional lights get shadows (decides the tile size)
    for (const Light &light : lights)
        if (hasMeshes && light.shadows && !light.point && light.lightIndex >= 0 && light.lightIndex < MaxLights)
            ++directionalTotal;
    directionalTotal = qMin(directionalTotal, MaxDirectional);

    int directionalCount = 0, pointCount = 0;
    for (const Light &light : lights) {
        if (!hasMeshes || !light.shadows || light.lightIndex < 0 || light.lightIndex >= MaxLights)
            continue;
        float *ls = data + kLightShadow + 4 * light.lightIndex;

        if (!light.point) {
            if (directionalCount == MaxDirectional)
                continue;
            const int slot = directionalCount++;
            const QVector3D dir = light.direction.normalized();
            const QVector3D up = qAbs(dir.y()) > 0.99f ? QVector3D(1, 0, 0) : QVector3D(0, 1, 0);
            QMatrix4x4 rotation; // world -> light space (rotation only), looking along `dir`
            rotation.lookAt(QVector3D(), dir, up);
            for (int cascade = 0; cascade < CascadeCount; ++cascade) {
                const float radius = sliceRadius[cascade];
                const int tile = slot * CascadeCount + cascade;
                const QRectF viewport = tileViewport(false, tile, directionalTotal);
                const float texelWorld = 2.0f * radius / float(viewport.width());

                // Slice center in light space, snapped to whole texels: when the camera moves,
                // the shadow map moves by whole texels and edges don't crawl.
                QVector3D c = rotation.map(sliceCenter[cascade]);
                c.setX(std::floor(c.x() / texelWorld) * texelWorld);
                c.setY(std::floor(c.y() / texelWorld) * texelWorld);
                // Depth: from the mesh nearest to the light (every caster, even outside the
                // slice) to the far side of the slice. Light space looks down -Z.
                float nearDist = -c.z() - radius, farDist = -c.z() + radius;
                for (const QVector3D &corner : corners)
                    nearDist = qMin(nearDist, -rotation.map(corner).z());
                nearDist -= 0.01f;

                QMatrix4x4 projection;
                projection.ortho(c.x() - radius, c.x() + radius, c.y() - radius, c.y() + radius, nearDist, farDist);

                View v;
                v.tile = tile;
                v.clip = corr * projection * rotation;
                v.viewport = viewport;
                const QMatrix4x4 toTile = clipToTile(v.viewport, directionalAtlas);
                putMat4(data + kDirMatrix + 16 * tile, toTile * v.clip);
                const QVector3D ta = toTile.map(QVector3D(-1, -1, 0)), tb = toTile.map(QVector3D(1, 1, 0));
                putVec4(data + kDirTile + 4 * tile, qMin(ta.x(), tb.x()), qMin(ta.y(), tb.y()), qMax(ta.x(), tb.x()),
                        qMax(ta.y(), tb.y()));
                putVec4(data + kDirRange + 4 * tile, farDist - nearDist, texelWorld, 0.0f, 0.0f);
                m_views.push_back(v);
            }
            putVec4(ls, float(slot), light.size, light.softness, 0.0f);
        } else {
            if (pointCount == MaxPoint)
                continue;
            const int slot = pointCount++;
            // Far plane: the farthest corner of the meshes' bounds.
            float far = kPointNear * 10.0f;
            for (const QVector3D &corner : corners)
                far = qMax(far, (corner - light.position).length() * 1.01f);
            // A little wider than 90°: PointFaceBorder extra texels on each side.
            const float tanHalf = float(PointFaceSize) / float(PointFaceSize - 2 * PointFaceBorder);
            const float fov = 2.0f * qRadiansToDegrees(std::atan(tanHalf));
            QMatrix4x4 projection;
            projection.perspective(fov, 1.0f, kPointNear, far);

            static const QVector3D dirs[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
            static const QVector3D ups[6] = { { 0, 1, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 1, 0 }, { 0, 1, 0 } };
            for (int face = 0; face < 6; ++face) {
                QMatrix4x4 view;
                view.lookAt(light.position, light.position + dirs[face], ups[face]);
                View v;
                v.point = true;
                v.tile = slot * 6 + face;
                v.clip = corr * projection * view;
                v.viewport = tileViewport(true, v.tile, directionalTotal);
                const QMatrix4x4 toTile = clipToTile(v.viewport, pointAtlas);
                putMat4(data + kPointMatrix + 16 * v.tile, toTile * v.clip);
                const QVector3D a = toTile.map(QVector3D(-1, -1, 0)), b = toTile.map(QVector3D(1, 1, 0));
                putVec4(data + kPointTile + 4 * v.tile, qMin(a.x(), b.x()), qMin(a.y(), b.y()), qMax(a.x(), b.x()),
                        qMax(a.y(), b.y()));
                m_views.push_back(v);
            }
            putVec4(data + kPointRange + 4 * slot, kPointNear, far, tanHalf, float(PointFaceSize));
            putVec4(ls, float(slot), light.size, light.softness, 0.0f);
        }
    }
    rub->updateDynamicBuffer(m_uniforms.get(), 0, kUniformSize, data);

    for (const View &v : m_views) {
        const int index = v.point ? DirectionalViews + v.tile : v.tile;
        float m[16];
        putMat4(m, v.clip);
        rub->updateDynamicBuffer(m_viewUbuf.get(), quint32(index) * m_viewStride, 64, m);
    }
}

void ShadowMaps::render(QRhiCommandBuffer *cb, QRhiResourceUpdateBatch *rub, const std::vector<Caster> &casters)
{
    const QColor black(Qt::black);
    for (bool point : { false, true }) {
        cb->beginPass(point ? m_pointRt.get() : m_directionalRt.get(), black, { 1.0f, 0 }, point ? nullptr : rub);
        bool bound = false;
        for (const View &v : m_views) {
            if (v.point != point)
                continue;
            if (!bound) {
                cb->setGraphicsPipeline(m_pipeline.get());
                bound = true;
            }
            cb->setViewport({ float(v.viewport.x()), float(v.viewport.y()), float(v.viewport.width()),
                              float(v.viewport.height()) });
            const quint32 offset = quint32(point ? DirectionalViews + v.tile : v.tile) * m_viewStride;
            const QRhiCommandBuffer::DynamicOffset dynamicOffset(1, offset);
            for (const Caster &c : casters) {
                cb->setShaderResources(c.srb, 1, &dynamicOffset);
                const QRhiCommandBuffer::VertexInput vin(c.vertices, 0);
                cb->setVertexInput(0, 1, &vin, c.indices, 0, QRhiCommandBuffer::IndexUInt32);
                cb->drawIndexed(c.indexCount);
            }
        }
        cb->endPass();
    }
}
