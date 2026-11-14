#pragma once

#include <QList>
#include <QMatrix4x4>
#include <QRectF>
#include <QVector3D>
#include <array>
#include <memory>
#include <vector>

class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class QRhiGraphicsPipeline;
class QRhiRenderPassDescriptor;
class QRhiResourceUpdateBatch;
class QRhiSampler;
class QRhiShaderResourceBinding;
class QRhiShaderResourceBindings;
class QRhiTexture;
class QRhiTextureRenderTarget;

// Shadow maps of every light that casts shadows, in two depth atlases:
//
//   directional  DirectionalAtlasSize^2: one DirectionalSize^2 tile per light (2 x 2),
//                orthographic, covering the bounding sphere of every visible mesh
//   point        6 x MaxPoint tiles of PointFaceSize^2: one row per light, one cube face
//                per column (+X -X +Y -Y +Z -Z), perspective from the light's position
//
// Each atlas is rendered in one pass, with one viewport per tile, before the scene pass.
// mesh.frag reads both atlases twice: through a comparison sampler (filtered shadow tests)
// and as raw depth (for blocker searches, e.g. PCSS). See the shadow block in mesh.frag.
class ShadowMaps
{
public:
    static constexpr int MaxLights = 8; // must match Renderer::MaxLights and MAX_LIGHTS in mesh.frag
    static constexpr int MaxDirectional = 4;
    static constexpr int MaxPoint = 4;
    static constexpr int DirectionalSize = 2048;
    static constexpr int DirectionalAtlasSize = 2 * DirectionalSize;
    static constexpr int PointFaceSize = 512;
    // Extra texels rendered around each cube face (a field of view a bit wider than 90°),
    // so filters near a face's edge still read valid depth inside the tile: filter and
    // search radii up to this many texels never need clamping.
    static constexpr int PointFaceBorder = 24;
    static constexpr int ViewCount = MaxDirectional + 6 * MaxPoint;

    // One light of the frame, at index `lightIndex` in mesh.frag's lights[].
    struct Light
    {
        int lightIndex = 0;
        bool point = false;
        QVector3D position;  // point lights
        QVector3D direction; // directional lights: where the light shines
        bool shadows = true;
        float size = 0.0f;
        float softness = 1.0f;
    };

    // A mesh drawn into the shadow maps: its buffers and its binding set (casterBindings()).
    struct Caster
    {
        QRhiBuffer *vertices = nullptr;
        QRhiBuffer *indices = nullptr;
        quint32 indexCount = 0;
        QRhiShaderResourceBindings *srb = nullptr;
    };

    ShadowMaps();
    ~ShadowMaps();

    void initialize(QRhi *rhi);
    void release();

    // Bindings 6..10 of every mesh SRB (the shadow block and both atlases, twice).
    QList<QRhiShaderResourceBinding> meshBindings() const;
    // Bindings of a caster's SRB in the shadow passes: its entity uniforms (binding 0, only
    // `model` is read) and the view matrix (binding 1, dynamic offset per tile).
    QList<QRhiShaderResourceBinding> casterBindings(QRhiBuffer *entityUniforms) const;

    // Picks the shadowed lights (up to MaxDirectional + MaxPoint, in order), fits their
    // views to the meshes' world bounds and writes the uniforms. Lights beyond the limits,
    // or with `shadows` off, light without shadows.
    void prepare(QRhiResourceUpdateBatch *rub, const std::vector<Light> &lights, const QVector3D &boundsMin,
                 const QVector3D &boundsMax, bool hasMeshes);
    // Records both atlas passes (always cleared, so they're valid to sample). `rub` goes
    // with the first pass.
    void render(QRhiCommandBuffer *cb, QRhiResourceUpdateBatch *rub, const std::vector<Caster> &casters);

    int passCount() const { return 2; }
    int viewCount() const { return int(m_views.size()); } // tiles drawn in the last prepare()

private:
    struct View
    {
        bool point = false;
        int tile = 0;           // directional: slot; point: slot * 6 + face
        QMatrix4x4 clip;        // world -> clip space, corrected for the backend
        QRectF viewport;        // in the atlas, QRhi convention (pixels, origin bottom-left)
    };

    void createPipeline();
    QRectF tileViewport(bool point, int tile) const;
    QMatrix4x4 clipToTile(const QRectF &viewport, const QSize &atlas) const;

    QRhi *m_rhi = nullptr;
    std::unique_ptr<QRhiTexture> m_directional, m_point;
    std::unique_ptr<QRhiTextureRenderTarget> m_directionalRt, m_pointRt;
    std::unique_ptr<QRhiRenderPassDescriptor> m_rp; // depth only, shared by both atlases
    std::unique_ptr<QRhiSampler> m_compareSampler, m_depthSampler;
    std::unique_ptr<QRhiBuffer> m_uniforms; // the shadow block of mesh.frag
    std::unique_ptr<QRhiBuffer> m_viewUbuf; // one aligned view matrix per tile (dynamic offsets)
    std::unique_ptr<QRhiBuffer> m_layoutUbuf;
    std::unique_ptr<QRhiShaderResourceBindings> m_layoutSrb;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
    quint32 m_viewStride = 0;
    std::vector<View> m_views;
};
