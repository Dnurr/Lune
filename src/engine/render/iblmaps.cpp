#include "iblmaps.h"

#include <QFile>
#include <QFloat16>
#include <rhi/qrhi.h>

#include <cmath>

namespace
{
constexpr float kPi = 3.14159265358979f;

QShader loadShader(const QString &name)
{
    QFile f(QStringLiteral(":/shaders/") + name + QStringLiteral(".qsb"));
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("IblMaps: missing shader %s", qPrintable(name));
        return {};
    }
    return QShader::fromSerialized(f.readAll());
}

float sRGBToLinear(float c)
{
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
} // namespace

IblMaps::IblMaps() = default;

IblMaps::~IblMaps() = default;

HdrImage IblMaps::gradient(const QColor &sky, const QColor &ground, int width, int height)
{
    const float skyLin[3] = { sRGBToLinear(float(sky.redF())), sRGBToLinear(float(sky.greenF())),
                              sRGBToLinear(float(sky.blueF())) };
    const float groundLin[3] = { sRGBToLinear(float(ground.redF())), sRGBToLinear(float(ground.greenF())),
                                 sRGBToLinear(float(ground.blueF())) };
    HdrImage image;
    image.width = width;
    image.height = height;
    image.pixels.resize(size_t(width) * size_t(height) * 3);
    for (int y = 0; y < height; ++y) {
        // Row 0 looks straight up: the direction's height is cos(polar angle).
        const float up = std::cos((y + 0.5f) / float(height) * kPi);
        const float t = up * 0.5f + 0.5f;
        for (int x = 0; x < width; ++x) {
            float *p = image.pixels.data() + (size_t(y) * size_t(width) + size_t(x)) * 3;
            for (int c = 0; c < 3; ++c)
                p[c] = groundLin[c] + (skyLin[c] - groundLin[c]) * t;
        }
    }
    return image;
}

HdrImage IblMaps::sourceImage(const IblSource &source, QString *error)
{
    if (!source.image.isEmpty()) {
        HdrImage image = HdrIO::load(source.image, error);
        if (!image.isNull())
            return downscaled(std::move(image), MaxImageWidth);
    }
    return gradient(source.sky, source.ground);
}

// ---------------------------------------------------------------------------
// Resources
// ---------------------------------------------------------------------------

std::unique_ptr<QRhiTextureRenderTarget> IblMaps::cubeTarget(QRhiTexture *cube, int face, int level)
{
    QRhiColorAttachment attachment(cube);
    attachment.setLayer(face);
    attachment.setLevel(level);
    std::unique_ptr<QRhiTextureRenderTarget> rt(m_rhi->newTextureRenderTarget(QRhiTextureRenderTargetDescription(attachment)));
    rt->setRenderPassDescriptor(m_rp.get());
    rt->create();
    return rt;
}

void IblMaps::initialize(QRhi *rhi)
{
    release();
    m_rhi = rhi;

    if (!m_rhi->isTextureFormatSupported(QRhiTexture::RGBA16F, QRhiTexture::RenderTarget))
        qWarning("IblMaps: RGBA16F render targets are not supported, environment lighting will be wrong");

    m_cubeSampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::Linear,
                                          QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_cubeSampler->create();
    m_lutSampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                         QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_lutSampler->create();
    // Equirectangular images wrap around horizontally.
    m_equirectSampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                              QRhiSampler::Repeat, QRhiSampler::ClampToEdge));
    m_equirectSampler->create();

    using T = QRhiTexture;
    auto texture = [this](int size, T::Flags flags) {
        std::unique_ptr<QRhiTexture> t(m_rhi->newTexture(T::RGBA16F, QSize(size, size), 1, flags | T::RenderTarget));
        t->create();
        return t;
    };
    m_environment = texture(EnvironmentSize, T::CubeMap | T::MipMapped | T::UsedWithGenerateMips);
    m_irradiance = texture(IrradianceSize, T::CubeMap);
    m_prefiltered = texture(PrefilterSize, T::CubeMap | T::MipMapped);
    m_lut = texture(LutSize, {});
    // Until the first source is set: a 1x1 placeholder, so the conversion SRBs can exist.
    m_equirect.reset(m_rhi->newTexture(T::RGBA16F, QSize(1, 1)));
    m_equirect->create();

    // Every pass renders RGBA16F color only: one render pass descriptor for all of them.
    m_lutPass.target.reset(m_rhi->newTextureRenderTarget({ QRhiColorAttachment(m_lut.get()) }));
    m_rp.reset(m_lutPass.target->newCompatibleRenderPassDescriptor());
    m_lutPass.target->setRenderPassDescriptor(m_rp.get());
    m_lutPass.target->create();

    using B = QRhiShaderResourceBinding;
    const auto stages = B::VertexStage | B::FragmentStage;
    auto initPass = [&](Pass &pass, QRhiTexture *cube, int face, int level, QRhiTexture *input, QRhiSampler *sampler) {
        pass.uniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 16));
        pass.uniforms->create();
        pass.target = cubeTarget(cube, face, level);
        pass.srb.reset(m_rhi->newShaderResourceBindings());
        pass.srb->setBindings({ B::uniformBuffer(0, stages, pass.uniforms.get()),
                                B::sampledTexture(1, B::FragmentStage, input, sampler) });
        pass.srb->create();
    };
    for (int face = 0; face < 6; ++face) {
        initPass(m_convertPasses[size_t(face)], m_environment.get(), face, 0, m_equirect.get(), m_equirectSampler.get());
        initPass(m_irradiancePasses[size_t(face)], m_irradiance.get(), face, 0, m_environment.get(), m_cubeSampler.get());
        for (int level = 0; level < PrefilterLevels; ++level) {
            initPass(m_prefilterPasses[size_t(level * 6 + face)], m_prefiltered.get(), face, level, m_environment.get(),
                     m_cubeSampler.get());
        }
    }
    m_lutPass.srb.reset(m_rhi->newShaderResourceBindings()); // the LUT needs no input
    m_lutPass.srb->create();

    const bool flip = m_rhi->isYUpInNDC() != m_rhi->isYUpInFramebuffer();
    const QShader cube = loadShader(QStringLiteral("cube.vert"));
    const QShader fullscreen = loadShader(flip ? QStringLiteral("fullscreen_flipped.vert") : QStringLiteral("fullscreen.vert"));
    auto pipeline = [this](const QShader &vert, const QString &frag, QRhiShaderResourceBindings *srb) {
        std::unique_ptr<QRhiGraphicsPipeline> p(m_rhi->newGraphicsPipeline());
        p->setShaderStages({ { QRhiShaderStage::Vertex, vert }, { QRhiShaderStage::Fragment, loadShader(frag) } });
        p->setShaderResourceBindings(srb);
        p->setRenderPassDescriptor(m_rp.get());
        p->create();
        return p;
    };
    m_convertPipe = pipeline(cube, QStringLiteral("equirect_to_cube.frag"), m_convertPasses[0].srb.get());
    m_irradiancePipe = pipeline(cube, QStringLiteral("irradiance.frag"), m_irradiancePasses[0].srb.get());
    m_prefilterPipe = pipeline(cube, QStringLiteral("prefilter.frag"), m_prefilterPasses[0].srb.get());
    m_lutPipe = pipeline(fullscreen, QStringLiteral("brdf_lut.frag"), m_lutPass.srb.get());
}

void IblMaps::release()
{
    m_convertPipe.reset();
    m_irradiancePipe.reset();
    m_prefilterPipe.reset();
    m_lutPipe.reset();
    auto releasePass = [](Pass &pass) {
        pass.srb.reset();
        pass.target.reset();
        pass.uniforms.reset();
    };
    for (Pass &p : m_convertPasses)
        releasePass(p);
    for (Pass &p : m_irradiancePasses)
        releasePass(p);
    for (Pass &p : m_prefilterPasses)
        releasePass(p);
    releasePass(m_lutPass);
    m_rp.reset();
    m_equirect.reset();
    m_environment.reset();
    m_irradiance.reset();
    m_prefiltered.reset();
    m_lut.reset();
    m_cubeSampler.reset();
    m_lutSampler.reset();
    m_equirectSampler.reset();
    m_rhi = nullptr;
    m_lutReady = false;
    m_hasSource = false;
    m_uniformsReady = false;
}

void IblMaps::setSourceImage(const HdrImage &image, QRhiResourceUpdateBatch *rub)
{
    // Half floats are plenty for radiance and halve the upload.
    QByteArray data(image.width * image.height * 4 * int(sizeof(qfloat16)), Qt::Uninitialized);
    auto *dst = reinterpret_cast<qfloat16 *>(data.data());
    for (size_t i = 0; i < size_t(image.width) * size_t(image.height); ++i) {
        // qfloat16 saturates to infinity above 65504: clamp so a hot sun stays finite.
        for (int c = 0; c < 3; ++c)
            dst[i * 4 + size_t(c)] = qfloat16(std::min(image.pixels[i * 3 + size_t(c)], 65000.0f));
        dst[i * 4 + 3] = qfloat16(1.0f);
    }
    m_equirect.reset(m_rhi->newTexture(QRhiTexture::RGBA16F, QSize(image.width, image.height)));
    m_equirect->create();
    rub->uploadTexture(m_equirect.get(), QRhiTextureUploadDescription(
                                             QRhiTextureUploadEntry(0, 0, QRhiTextureSubresourceUploadDescription(data))));

    // The conversion passes read the new texture.
    using B = QRhiShaderResourceBinding;
    for (Pass &pass : m_convertPasses) {
        pass.srb->setBindings({ B::uniformBuffer(0, B::VertexStage | B::FragmentStage, pass.uniforms.get()),
                                B::sampledTexture(1, B::FragmentStage, m_equirect.get(), m_equirectSampler.get()) });
        pass.srb->create();
    }
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

int IblMaps::update(QRhiCommandBuffer *cb, const IblSource &source)
{
    const bool sourceChanged = !m_hasSource || !(source == m_source);
    if (m_lutReady && !sourceChanged)
        return 0;

    const float flip = (m_rhi->isYUpInNDC() != m_rhi->isYUpInFramebuffer()) ? 1.0f : 0.0f;
    QRhiResourceUpdateBatch *rub = m_rhi->nextResourceUpdateBatch();
    // cube.vert parameters: x = face, y = flip V, z = roughness, w = size of the input in pixels.
    auto setUniforms = [rub](Pass &pass, int face, float flipV, float roughness, float inputSize) {
        const float u[4] = { float(face), flipV, roughness, inputSize };
        rub->updateDynamicBuffer(pass.uniforms.get(), 0, 16, u);
    };
    if (!m_uniformsReady) {
        for (int face = 0; face < 6; ++face) {
            setUniforms(m_irradiancePasses[size_t(face)], face, flip, 0.0f, float(EnvironmentSize));
            for (int level = 0; level < PrefilterLevels; ++level) {
                setUniforms(m_prefilterPasses[size_t(level * 6 + face)], face, flip,
                            float(level) / float(PrefilterLevels - 1), float(EnvironmentSize));
            }
        }
        m_uniformsReady = true;
    }

    int passes = 0;
    const QColor black(Qt::black);
    auto draw = [&](QRhiGraphicsPipeline *pipe, const Pass &pass) {
        const QSize size = pass.target->pixelSize();
        cb->beginPass(pass.target.get(), black, { 1.0f, 0 }, rub);
        rub = nullptr; // a batch is consumed by the pass it's given to
        cb->setGraphicsPipeline(pipe);
        cb->setViewport({ 0, 0, float(size.width()), float(size.height()) });
        cb->setShaderResources(pass.srb.get());
        cb->draw(3);
        cb->endPass();
        ++passes;
    };

    if (!m_lutReady) {
        draw(m_lutPipe.get(), m_lutPass);
        m_lutReady = true;
    }

    if (sourceChanged) {
        QString error;
        const HdrImage image = sourceImage(source, &error);
        if (!error.isEmpty())
            qWarning("IblMaps: %s (using the gradient)", qPrintable(error));
        m_source = source;
        m_hasSource = true;

        if (!rub)
            rub = m_rhi->nextResourceUpdateBatch();
        setSourceImage(image, rub);
        for (int face = 0; face < 6; ++face)
            setUniforms(m_convertPasses[size_t(face)], face, flip, 0.0f, float(image.width));

        for (const Pass &pass : m_convertPasses)
            draw(m_convertPipe.get(), pass);
        // Mips of the environment, for filtered sampling in the convolutions.
        rub = m_rhi->nextResourceUpdateBatch();
        rub->generateMips(m_environment.get());
        for (const Pass &pass : m_irradiancePasses)
            draw(m_irradiancePipe.get(), pass);
        for (const Pass &pass : m_prefilterPasses)
            draw(m_prefilterPipe.get(), pass);
    }

    if (rub)
        rub->release();
    return passes;
}
