#pragma once

#include <QSize>
#include <array>
#include <map>
#include <memory>
#include <tuple>
#include <vector>
#include <rhi/qrhi.h>

#include "engine/scene/entity.h"

// Post-processing chain of the Renderer: the effects of the scene camera's PostProcess
// component, as fullscreen passes around the display pass (tonemapping).
//
//   scene (HDR) -> depth of field -> bloom -> [display pass] -> chromatic aberration
//               -> vignette -> film grain -> FXAA -> overlay, glass blur, screen
//
// Effects that are off are skipped (no pass, no cost). The order is fixed.
//
// HDR effects ping-pong between two full-size HDR targets (hdrTexture(0/1)); bloom also has
// a pyramid of BloomLevels half-size steps:
//   down[0] = bloom_down(scene)         half size
//   down[i] = bloom_down(down[i-1])     1/2^(i+1) size
//   up[i]   = bloom_up(up[i+1] or down[last], down[i])    back up, level by level
//   result  = bloom_composite(scene, up[0])               full size
// Display effects ping-pong between the renderer's display image and ldrTexture(), the
// Renderer arranges it so the last one writes the display image.
//
// Every effect shader (shaders/post_*.frag, written by the author: they start as plain
// copies) has the same bindings:
//   0  PostBuf uniforms: inputSize, outputSize, camera (near, far, depth mode, seconds),
//      params0, params1 (the effect's settings, see prepare())
//   1  texture a (the image to process), linear filtering, clamped
//   2  texture b (a second image: bloom's pyramid), linear, clamped
//   3  the scene's depth (raw depth buffer values), nearest; a 1x1 stand-in where the
//      backend can't sample depth (camera.z = -1)
class PostProcessor
{
public:
    static constexpr int BloomLevels = 6;

    struct Frame
    {
        PostProcess settings;
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
        float fieldOfView = 50.0f; // vertical, degrees (depth of field's focal length)
        float seconds = 0.0f;      // animated effects (film grain); 0 in image renders
    };

    void initialize(QRhi *rhi);
    // Targets for a new size. `depth`: the scene's depth texture, or null (not sampleable).
    // `ldrRp`: the render pass of the Renderer's display targets.
    void resize(const QSize &pixelSize, QRhiTexture::Format hdrFormat, QRhiTexture *depth, QRhiRenderPassDescriptor *ldrRp);
    void createPipelines(const QShader &fullscreen);
    void release();

    // Once per frame, before the passes: what runs this frame and its uniforms.
    void prepare(QRhiResourceUpdateBatch *rub, const Frame &frame);
    // Passes this frame (for the stats).
    int passCount() const;

    // HDR effects on `scene`. Returns the texture holding the result: `scene` itself when
    // none is on, otherwise hdrTexture(0) or (1).
    QRhiTexture *renderHdr(QRhiCommandBuffer *cb, QRhiTexture *scene);
    QRhiTexture *hdrTexture(int index) const { return m_hdr[size_t(index)].texture.get(); }

    // Display effects this frame, and the extra display-referred target they ping-pong with.
    int ldrPassCount() const { return int(m_ldrPasses.size()); }
    QRhiTexture *ldrTexture() const { return m_ldr.texture.get(); }
    QRhiTextureRenderTarget *ldrTarget() const { return m_ldr.target.get(); }
    // Draws display effect `index` reading `input`, inside a pass the caller began on a
    // display-format target of the given size.
    void drawLdr(QRhiCommandBuffer *cb, int index, QRhiTexture *input, const QSize &size);

private:
    enum Pass // one uniform buffer each
    {
        DepthOfField,
        BloomDown,                                // + level
        BloomUp = BloomDown + BloomLevels,        // + level (the last level has none)
        BloomComposite = BloomUp + BloomLevels - 1,
        ChromaticAberration,
        Vignette,
        FilmGrain,
        Fxaa,
        PassCount
    };
    struct Target
    {
        std::unique_ptr<QRhiTexture> texture;
        std::unique_ptr<QRhiTextureRenderTarget> target;
    };

    Target newTarget(const QSize &size, QRhiTexture::Format format, bool hdr);
    // Bindings for one pass: its uniforms, textures a and b (null: a stand-in) and the depth.
    QRhiShaderResourceBindings *bindings(Pass pass, QRhiTexture *a, QRhiTexture *b);
    void fullscreen(QRhiCommandBuffer *cb, QRhiGraphicsPipeline *pipe, QRhiShaderResourceBindings *srb, const QSize &size);
    void writeUniforms(QRhiResourceUpdateBatch *rub, Pass pass, const QSize &input, const QSize &output, const float params0[4],
                       const float params1[4] = nullptr);

    QRhi *m_rhi = nullptr;
    QSize m_pixelSize;
    QRhiTexture *m_depth = nullptr; // not owned
    std::unique_ptr<QRhiTexture> m_dummy; // 1x1 stand-in for unused textures and the depth
    std::unique_ptr<QRhiSampler> m_linear, m_nearest;
    std::array<std::unique_ptr<QRhiBuffer>, PassCount> m_ubufs;
    std::array<Target, 2> m_hdr;
    std::array<Target, BloomLevels> m_bloomDown;
    std::array<Target, BloomLevels - 1> m_bloomUp;
    Target m_ldr;
    std::unique_ptr<QRhiRenderPassDescriptor> m_hdrRp;
    QRhiRenderPassDescriptor *m_ldrRp = nullptr; // the Renderer's
    std::unique_ptr<QRhiShaderResourceBindings> m_layout; // layout every pipeline is built with
    std::array<std::unique_ptr<QRhiGraphicsPipeline>, PassCount> m_pipelines; // bloom: one per kind, at its first level
    // Bindings by (pass, a, b), made when first needed; cleared when the targets change.
    std::map<std::tuple<int, QRhiTexture *, QRhiTexture *>, std::unique_ptr<QRhiShaderResourceBindings>> m_srbs;

    QRhiResourceUpdateBatch *m_pendingUpload = nullptr; // the stand-in's pixel, sent with the first frame

    // This frame
    float m_camera[4] = {}; // PostBuf.camera
    bool m_dof = false, m_bloom = false;
    std::vector<Pass> m_ldrPasses;
};
