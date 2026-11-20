#pragma once

#include <QList>
#include <QMatrix4x4>
#include <QRectF>
#include <QVector3D>
#include <rhi/qshader.h>
#include <array>
#include <functional>
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
//   directional  DirectionalAtlasSize^2, CascadeCount cascades per light: the camera's view
//                is cut in slices by distance, each slice gets its own orthographic tile
//                (fine near the camera, coarse far away). One light: 2 x 2 tiles of 2048^2;
//                more lights: a row of 4 tiles of 1024^2 per light
//   point        6 x MaxPoint tiles of PointFaceSize^2: one row per light, one cube face
//                per column (+X -X +Y -Y +Z -Z), perspective from the light's position
//
// Each atlas is rendered in one pass, with one viewport per tile, before the scene pass.
// Closed meshes (Mesh::closed) write only their back faces, as seen from the light: the
// depth of the far side of a solid object. Near a contact (a cube on the floor) that depth
// is close to the receiver's, so a blocker search (PCSS) finds a small distance and the
// penumbra shrinks to the contact, instead of measuring from the top of the object. It
// also keeps acne off lit surfaces. Open meshes (a plane) have no far side: both faces.
// mesh.frag reads both atlases twice: through a comparison sampler (filtered shadow tests)
// and as raw depth (for blocker searches, e.g. PCSS). See the shadow block in mesh.frag.
// On OpenGL the comparison mode is state of the texture, not of the sampler, so one texture
// can't be read both ways in the same draw: there each atlas is copied into an R32F texture
// after its pass (depth_copy.frag), and the raw depth is read from the copy.
//
// Node materials whose alpha matters draw with their own pipelines (createMaterialPipeline),
// whose fragment shader runs the material (shadow_material.glsl):
//   clip   into the depth atlases, cut where the material is (two-sided)
//   blend  not into the depth atlases but into the translucency map: one RGBA16F texture at
//          1 / TranslucencyScale of the atlases' resolution, a region for each atlas with the
//          same tiles. Cleared to 1; every translucent caster multiplies rgb by the light it
//          lets through and keeps in alpha the smallest (nearest) linear depth. Fluid volumes
//          draw into it too, through render()'s `moreTranslucent` (FluidSystem::drawShadows()).
//
// The translucency map also holds the front depth, in two more regions at 1 / FrontDepthScale
// (to the right of the translucency regions): the alpha of every texel is the linear depth
// of the caster surface nearest to the light (closed meshes: their front faces, the side
// the depth atlases leave out). The depth atlas minus it is how much of an object the
// light crossed (thickness, for subsurface scattering). Drawn by the built-in casters only:
// clip and blend materials are not in it.
//
//   translucency map (pixels):   x 0..2047             x 2048..3071
//                                 y 2048..3071 point    y 1024..1535 point front depth
//                                 y 0..2047 directional y 0..1023    directional front depth
class ShadowMaps
{
public:
    static constexpr int MaxLights = 8; // must match Renderer::MaxLights and MAX_LIGHTS in mesh.frag
    static constexpr int MaxDirectional = 4;
    static constexpr int MaxPoint = 4;
    static constexpr int CascadeCount = 4;
    static constexpr int DirectionalAtlasSize = 4096;
    static constexpr int DirectionalViews = MaxDirectional * CascadeCount;
    // Split distances blend uniform and logarithmic spacing: 0 = uniform, 1 = logarithmic.
    static constexpr float CascadeSplitBlend = 0.75f;
    static constexpr int PointFaceSize = 512;
    // Extra texels rendered around each cube face (a field of view a bit wider than 90°),
    // so filters near a face's edge still read valid depth inside the tile: filter and
    // search radii up to this many texels never need clamping.
    static constexpr int PointFaceBorder = 24;
    static constexpr int ViewCount = DirectionalViews + 6 * MaxPoint;
    static constexpr int TranslucencyScale = 2; // translucency map: half the atlases' resolution
    static constexpr int FrontDepthScale = 4;   // front-depth regions: a quarter
    static constexpr int TranslucencyBinding = 19;  // shadowTranslucency in mesh.frag
    static constexpr int MaterialViewBinding = 20;  // shadowViewBuf in shadow_material.vert/.glsl

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

    // The camera the cascades are fitted to.
    struct Camera
    {
        QVector3D eye;
        QVector3D forward, right, up; // unit vectors
        float fovY = 50.0f;           // vertical, degrees
        float aspect = 1.0f;
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
        float shiftX = 0.0f; // OrbitCamera::shiftX
    };

    // A mesh (or a range of its triangles: a submesh) drawn into the shadow maps.
    //   pipeline null:  depth only, with the built-in pipelines; `srb` from casterBindings()
    //   pipeline set:   a node material's (createMaterialPipeline, `translucent` as created
    //                   with); `srb` has the mesh bindings for casters (meshBindings(true))
    //                   plus materialViewBinding()
    struct Caster
    {
        QRhiBuffer *vertices = nullptr;
        QRhiBuffer *indices = nullptr;
        quint32 firstIndex = 0;
        quint32 indexCount = 0;
        QRhiShaderResourceBindings *srb = nullptr;
        bool closed = false;   // Mesh::closed: back faces only (built-in pipelines)
        bool mirrored = false; // the model matrix flips the winding (negative determinant)
        QRhiGraphicsPipeline *pipeline = nullptr;
        bool translucent = false; // into the translucency map instead of the depth atlases
    };

    ShadowMaps();
    ~ShadowMaps();

    void initialize(QRhi *rhi);
    void release();

    // Bindings 6..10 and 19 of every mesh SRB (the shadow block, both atlases twice, the
    // translucency map). `forCasters`: for the SRBs of material casters, which draw into
    // the atlases: 1x1 stand-ins instead of the atlases and the map (the material shader has
    // mesh.frag's declarations, but must not read what it's drawing into).
    QList<QRhiShaderResourceBinding> meshBindings(bool forCasters = false) const;
    // Bindings of a caster's SRB in the shadow passes: its entity uniforms (binding 0, only
    // `model` is read) and the view matrix (binding 1, dynamic offset per tile).
    QList<QRhiShaderResourceBinding> casterBindings(QRhiBuffer *entityUniforms) const;
    // The view of material casters (MaterialViewBinding, dynamic offset per tile).
    QRhiShaderResourceBinding materialViewBinding() const;
    // A pipeline for a material's shadow fragment shader (mesh.frag's declarations + the
    // material + shadow_material.glsl). `layout`: an SRB laid out like the casters'.
    // nullptr if it can't be created.
    std::unique_ptr<QRhiGraphicsPipeline> createMaterialPipeline(const QShader &fragment,
                                                                 QRhiShaderResourceBindings *layout,
                                                                 bool translucent) const;

    // A tile of the last prepare(), for drawing other translucent casters (fluid volumes) into
    // the translucency map: a pipeline made for translucencyRenderPass(), the viewport set to
    // `viewport`, blending like a blend material (rgb multiplied, a = min of linear depths).
    struct TranslucentView
    {
        QMatrix4x4 clip;    // world -> the light's clip space, corrected for the backend
        QRectF viewport;    // in the translucency map (QRhi convention)
        bool point = false;
        float nearPlane = 0.0f, farPlane = 0.0f; // point lights (linear depth = distance along the face's axis / far)
        QVector3D light;    // directional: where it shines (unit); point: its position
    };
    std::vector<TranslucentView> translucentViews() const;
    QRhiRenderPassDescriptor *translucencyRenderPass() const { return m_translucencyRp.get(); }

    // Picks the shadowed lights (up to MaxDirectional + MaxPoint, in order), fits their
    // views (directional: cascades of the camera's view, up to the farthest mesh; point:
    // the meshes' bounds) and writes the uniforms. Lights beyond the limits, or with
    // `shadows` off, light without shadows.
    void prepare(QRhiResourceUpdateBatch *rub, const std::vector<Light> &lights, const Camera &camera,
                 const QVector3D &boundsMin, const QVector3D &boundsMax, bool hasMeshes);
    // Records both atlas passes and the translucency pass, with the front depth (always
    // cleared, so they're valid to sample). `rub` goes with the first pass. `moreTranslucent`,
    // if set, records more translucent casters at the end of the translucency pass (see
    // translucentViews()).
    void render(QRhiCommandBuffer *cb, QRhiResourceUpdateBatch *rub, const std::vector<Caster> &casters,
                const std::function<void(QRhiCommandBuffer *)> &moreTranslucent = {});

    int passCount() const { return m_rawCopy ? 5 : 3; }
    int viewCount() const { return int(m_views.size()); } // tiles drawn in the last prepare()

private:
    struct View
    {
        bool point = false;
        int tile = 0;           // directional: slot * CascadeCount + cascade; point: slot * 6 + face
        QMatrix4x4 clip;        // world -> clip space, corrected for the backend
        QRectF viewport;        // in the atlas, QRhi convention (pixels, origin bottom-left)
        QRectF translucencyViewport; // the same tile in the translucency map
        QRectF frontDepthViewport;   // and in its front-depth region
        float nearPlane = 0.0f, farPlane = 0.0f; // point lights
        QVector3D light;             // directional: where it shines (unit); point: its position
    };

    enum Cull { CullNone, CullFront, CullBack, CullModes };
    static Cull cullFor(const Caster &c);
    void createPipelines();
    QRectF tileViewport(bool point, int tile, int directionalCount) const;
    QMatrix4x4 clipToTile(const QRectF &viewport, const QSize &atlas) const;
    // Where an atlas's region starts in the translucency map (pixels, QRhi convention):
    // its translucency, or its front depth.
    QPointF regionOrigin(bool point, bool front) const;
    // Atlas uv -> translucency map uv of that region: xy scale, zw offset.
    void regionMapping(bool point, bool front, float *dst) const;

    QRhi *m_rhi = nullptr;
    std::unique_ptr<QRhiTexture> m_directional, m_point;
    std::unique_ptr<QRhiTextureRenderTarget> m_directionalRt, m_pointRt;
    std::unique_ptr<QRhiRenderPassDescriptor> m_rp; // depth only, shared by both atlases
    std::unique_ptr<QRhiTexture> m_translucency;
    std::unique_ptr<QRhiTextureRenderTarget> m_translucencyRt;
    std::unique_ptr<QRhiRenderPassDescriptor> m_translucencyRp;
    std::unique_ptr<QRhiSampler> m_translucencySampler;
    std::unique_ptr<QRhiTexture> m_dummyDepth, m_dummyColor; // meshBindings(true)
    // OpenGL: R32F copies of the atlases, for the raw depth reads (see the top of the file).
    struct RawCopy
    {
        std::unique_ptr<QRhiTexture> texture;
        std::unique_ptr<QRhiTextureRenderTarget> target;
        std::unique_ptr<QRhiShaderResourceBindings> srb;
    };
    bool m_rawCopy = false;
    RawCopy m_directionalRaw, m_pointRaw;
    std::unique_ptr<QRhiRenderPassDescriptor> m_rawRp;
    std::unique_ptr<QRhiGraphicsPipeline> m_rawPipeline;
    QShader m_materialVs; // shadow_material.vert
    std::unique_ptr<QRhiSampler> m_compareSampler, m_depthSampler;
    std::unique_ptr<QRhiBuffer> m_uniforms; // the shadow block of mesh.frag
    std::unique_ptr<QRhiBuffer> m_viewUbuf; // per tile, aligned (dynamic offsets): view matrix + vec4 (point, near, far)
    std::unique_ptr<QRhiBuffer> m_layoutUbuf;
    std::unique_ptr<QRhiShaderResourceBindings> m_layoutSrb;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipelines[CullModes];
    std::unique_ptr<QRhiGraphicsPipeline> m_frontPipelines[CullModes]; // indexed by cullFor()
    quint32 m_viewStride = 0;
    std::vector<View> m_views;
};
