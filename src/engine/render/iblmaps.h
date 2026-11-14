#pragma once

#include <QColor>
#include <QString>
#include <array>
#include <memory>
#include <vector>

#include "engine/io/hdrimage.h"

class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class QRhiGraphicsPipeline;
class QRhiRenderPassDescriptor;
class QRhiResourceUpdateBatch;
class QRhiSampler;
class QRhiShaderResourceBindings;
class QRhiTexture;
class QRhiTextureRenderTarget;

// What the environment maps are computed from: an equirectangular HDR image or,
// without one (or when it can't be read), a sky/ground gradient.
struct IblSource
{
    QString image;     // absolute path of an .hdr file, empty = gradient
    QColor sky, ground; // gradient colors (sRGB)

    bool operator==(const IblSource &) const = default;
};

// GPU maps for image-based lighting, recomputed on the GPU when the source changes:
//
//   environment  cubemap, EnvironmentSize, full mip chain: the source as a cube
//                (equirect_to_cube.frag), mips generated for filtered sampling
//   irradiance   cubemap, IrradianceSize: diffuse lighting per normal direction (irradiance.frag)
//   prefiltered  cubemap, PrefilterSize, PrefilterLevels mips: specular reflection,
//                roughness = level / (PrefilterLevels - 1) (prefilter.frag)
//   brdfLut      2D, LutSize: split-sum BRDF terms per (NdotV, roughness) (brdf_lut.frag),
//                computed once since it doesn't depend on the environment
//
// Every cube pass draws a fullscreen triangle per face (and per mip) with cube.vert,
// which gives the fragment shader the world direction of each texel.
class IblMaps
{
public:
    static constexpr int EnvironmentSize = 512;
    static constexpr int IrradianceSize = 32;
    static constexpr int PrefilterSize = 128;
    static constexpr int PrefilterLevels = 5; // 128 .. 8 pixels
    static constexpr int LutSize = 256;
    static constexpr int MaxImageWidth = 4096; // larger images are downscaled when loaded

    IblMaps();
    ~IblMaps();

    void initialize(QRhi *rhi);
    void release();

    // Recomputes the maps if `source` differs from the last one (and the LUT the first time),
    // recording the passes on `cb`; call it before any pass that samples the maps.
    // Returns the number of render passes recorded (0 when nothing changed).
    int update(QRhiCommandBuffer *cb, const IblSource &source);

    QRhiTexture *environment() const { return m_environment.get(); }
    QRhiTexture *irradiance() const { return m_irradiance.get(); }
    QRhiTexture *prefiltered() const { return m_prefiltered.get(); }
    QRhiTexture *brdfLut() const { return m_lut.get(); }
    QRhiSampler *cubeSampler() const { return m_cubeSampler.get(); } // trilinear, clamped
    QRhiSampler *lutSampler() const { return m_lutSampler.get(); }   // bilinear, clamped

    // The equirectangular image used for the source: the file, or the gradient when
    // there's no file. `error` is set when the file couldn't be read (the gradient is used).
    static HdrImage sourceImage(const IblSource &source, QString *error = nullptr);
    // Equirectangular sky/ground gradient (linear radiance), blended on the direction's height.
    static HdrImage gradient(const QColor &sky, const QColor &ground, int width = 64, int height = 32);

private:
    // One fullscreen pass into one face (or the LUT) of one mip level.
    struct Pass
    {
        std::unique_ptr<QRhiBuffer> uniforms; // cube.vert: face, flip, roughness, source size
        std::unique_ptr<QRhiTextureRenderTarget> target;
        std::unique_ptr<QRhiShaderResourceBindings> srb;
    };

    void setSourceImage(const HdrImage &image, QRhiResourceUpdateBatch *rub);
    std::unique_ptr<QRhiTextureRenderTarget> cubeTarget(QRhiTexture *cube, int face, int level);

    QRhi *m_rhi = nullptr;
    bool m_lutReady = false;
    bool m_hasSource = false;
    bool m_uniformsReady = false;
    IblSource m_source;

    std::unique_ptr<QRhiSampler> m_cubeSampler, m_lutSampler, m_equirectSampler;
    std::unique_ptr<QRhiTexture> m_equirect; // the source image, RGBA16F
    std::unique_ptr<QRhiTexture> m_environment, m_irradiance, m_prefiltered, m_lut;
    std::unique_ptr<QRhiRenderPassDescriptor> m_rp; // RGBA16F color only: every pass

    std::array<Pass, 6> m_convertPasses;                    // equirect -> environment, per face
    std::array<Pass, 6> m_irradiancePasses;                 // per face
    std::array<Pass, 6 * PrefilterLevels> m_prefilterPasses; // per level, per face
    Pass m_lutPass;

    std::unique_ptr<QRhiGraphicsPipeline> m_convertPipe, m_irradiancePipe, m_prefilterPipe, m_lutPipe;
};
