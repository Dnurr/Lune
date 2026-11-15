#include "texturecache.h"

#include "engine/io/imageloader.h"

#include <rhi/qrhi.h>

#include <chrono>

namespace
{
// Frames a texture may go unused before it's released (a few seconds at 60 fps): picking
// an image, then another, then the first again doesn't reload it.
constexpr int kKeepUnusedFrames = 600;
} // namespace

TextureCache::TextureCache() = default;

TextureCache::~TextureCache()
{
    release();
}

void TextureCache::initialize(QRhi *rhi)
{
    release();
    m_rhi = rhi;
    m_sampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::Linear,
                                      QRhiSampler::Repeat, QRhiSampler::Repeat));
    m_sampler->create();
    for (auto &t : m_fallbacks) {
        t.reset(m_rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
        t->create();
    }
    m_fallbacksUploaded = false;
}

void TextureCache::release()
{
    for (auto &[key, e] : m_entries) {
        if (e.pending.valid())
            e.pending.wait(); // the worker must not outlive the cache
    }
    m_entries.clear();
    for (auto &t : m_fallbacks)
        t.reset();
    m_sampler.reset();
    m_rhi = nullptr;
}

void TextureCache::beginFrame(QRhiResourceUpdateBatch *rub)
{
    if (!m_rhi)
        return;
    if (!m_fallbacksUploaded) {
        // White, black, gray (linear 0.5), flat tangent-space normal.
        const QRgb colors[4] = { qRgba(255, 255, 255, 255), qRgba(0, 0, 0, 255), qRgba(128, 128, 128, 255),
                                 qRgba(128, 128, 255, 255) };
        for (size_t i = 0; i < m_fallbacks.size(); ++i) {
            QImage pixel(1, 1, QImage::Format_RGBA8888);
            pixel.setPixel(0, 0, colors[i]);
            rub->uploadTexture(m_fallbacks[i].get(), pixel);
        }
        m_fallbacksUploaded = true;
    }

    for (auto it = m_entries.begin(); it != m_entries.end();) {
        Entry &e = it->second;
        if (e.pending.valid() && e.pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto [image, error] = e.pending.get();
            if (image.isNull()) {
                e.error = error.isEmpty() ? QStringLiteral("Can't read the image") : error;
                m_errorsChanged = true;
                qWarning("Texture: %s", qPrintable(e.error));
            } else {
                QRhiTexture::Flags flags = QRhiTexture::MipMapped | QRhiTexture::UsedWithGenerateMips;
                if (!it->first.second)
                    flags |= QRhiTexture::sRGB; // color: sampled as linear
                e.texture.reset(m_rhi->newTexture(QRhiTexture::RGBA8, image.size(), 1, flags));
                if (e.texture->create()) {
                    rub->uploadTexture(e.texture.get(), image);
                    rub->generateMips(e.texture.get());
                } else {
                    e.texture.reset();
                    e.error = QStringLiteral("The texture couldn't be created (%1 x %2)").arg(image.width()).arg(image.height());
                }
            }
        }
        // Count frames without use (texture() resets it); release the long unused ones.
        if (!e.pending.valid() && ++e.unusedFrames > kKeepUnusedFrames)
            it = m_entries.erase(it);
        else
            ++it;
    }
}

QRhiTexture *TextureCache::texture(const TextureValue &value, TextureFallback fallback)
{
    if (value.image.isEmpty() || !m_rhi)
        return this->fallback(fallback);
    const Key key(value.image, value.linear);
    auto it = m_entries.find(key);
    if (it == m_entries.end()) {
        Entry e;
        const QString image = value.image;
        e.pending = std::async(std::launch::async, [image] {
            QString error;
            QImage decoded = loadImage(image, &error);
            return std::make_pair(std::move(decoded), error);
        });
        it = m_entries.emplace(key, std::move(e)).first;
    }
    it->second.unusedFrames = 0;
    return it->second.texture ? it->second.texture.get() : this->fallback(fallback);
}

bool TextureCache::takeErrorsChanged()
{
    const bool changed = m_errorsChanged;
    m_errorsChanged = false;
    return changed;
}

bool TextureCache::loading() const
{
    for (const auto &[key, e] : m_entries)
        if (e.pending.valid())
            return true;
    return false;
}

QString TextureCache::error(const QString &image) const
{
    for (const auto &[key, e] : m_entries)
        if (key.first == image && !e.error.isEmpty())
            return e.error;
    return QString();
}
