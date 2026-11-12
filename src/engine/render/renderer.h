#pragma once

#include <QList>
#include <QRectF>
#include <QSize>
#include <array>
#include <memory>

class OrbitCamera;
class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class QRhiGraphicsPipeline;
class QRhiRenderPassDescriptor;
class QRhiRenderTarget;
class QRhiSampler;
class QRhiShaderResourceBindings;
class QRhiTexture;
class QRhiTextureRenderTarget;

// Scene renderer, independent from any widget: it only needs a QRhi and a render target
// (an editor viewport today, an offline render target tomorrow).
//
// Frame:
//   1. scene  -> world grid on y = 0 (offscreen texture)
//   2. blur   -> scene downsampled to 1/4 and blurred (Gaussian, 2 iterations)
//   3. final  -> scene, with the blurred copy inside the "glass" rectangles
//
// Usage:
//   initialize(rhi, target)  on the first frame and whenever the target/QRhi changes
//   render(cb, target, cam)  every frame
//   release()                when the QRhi goes away
class Renderer
{
public:
    static constexpr int MaxGlassRects = 16; // must match MAX_RECTS in composite.frag

    Renderer();
    ~Renderer();

    void initialize(QRhi *rhi, QRhiRenderTarget *target);
    void render(QRhiCommandBuffer *cb, QRhiRenderTarget *target, const OrbitCamera &camera);
    void release();

    // Areas (in target pixels, origin top-left) that show the blurred scene,
    // with rounded corners. Used for the editor's floating panels.
    void setGlassRects(const QList<QRectF> &rects, float cornerRadius);

private:
    void createSizeDependentResources();
    void createPipelines(QRhiRenderTarget *target);

    QRhi *m_rhi = nullptr;
    QSize m_pixelSize;

    QList<QRectF> m_glassRects;
    float m_glassRadius = 0.0f;

    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiBuffer> m_gridUbuf;
    std::unique_ptr<QRhiBuffer> m_downUbuf;
    std::array<std::unique_ptr<QRhiBuffer>, 4> m_blurUbufs; // H, V, H, V
    std::unique_ptr<QRhiBuffer> m_compositeUbuf;

    // Offscreen targets: full-size scene, two 1/4-size ping-pong textures for the blur.
    std::unique_ptr<QRhiTexture> m_sceneTex, m_blurA, m_blurB;
    std::unique_ptr<QRhiTextureRenderTarget> m_sceneRt, m_blurARt, m_blurBRt;
    std::unique_ptr<QRhiRenderPassDescriptor> m_offscreenRp; // shared by all offscreen targets

    std::unique_ptr<QRhiShaderResourceBindings> m_gridSrb, m_downSrb, m_compositeSrb;
    std::array<std::unique_ptr<QRhiShaderResourceBindings>, 4> m_blurSrbs;

    std::unique_ptr<QRhiGraphicsPipeline> m_gridPipe, m_downPipe, m_blurPipe, m_compositePipe;
};
