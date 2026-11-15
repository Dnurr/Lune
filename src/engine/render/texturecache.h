#pragma once

#include <QImage>
#include <QString>

#include <array>
#include <future>
#include <map>
#include <memory>
#include <utility>

#include "engine/material/nodelibrary.h"

class QRhi;
class QThreadPool;
class QRhiResourceUpdateBatch;
class QRhiSampler;
class QRhiTexture;

// The images used by node materials, as GPU textures: one per (image reference, linear),
// shared by every material using it.
//
// Images are decoded by a few worker threads (loadImage()), a few at a time: a scene with
// dozens of 4K images would otherwise hold all of them decoded at once (gigabytes) before
// the first is uploaded. Until an image is ready, and if it fails,
// texture() returns the input's fallback (1x1 white, black, gray or flat normal), so a
// material is drawn right away and its textures pop in. Textures are RGBA8 with a full mip
// chain, generated on the GPU; color images use an sRGB format, so sampling converts them
// to linear. A texture no material used for a while is released.
class TextureCache
{
public:
    TextureCache();
    ~TextureCache();

    void initialize(QRhi *rhi);
    void release();

    // Once per frame, before texture(): uploads the images decoded since the last frame,
    // forgets textures unused for a while.
    void beginFrame(QRhiResourceUpdateBatch *rub);

    // The texture to bind for `value` (starts loading it the first time).
    QRhiTexture *texture(const TextureValue &value, TextureFallback fallback);
    QRhiTexture *fallback(TextureFallback fallback) const { return m_fallbacks[size_t(fallback)].get(); }
    // Linear filtering with mipmaps, repeat.
    QRhiSampler *sampler() const { return m_sampler.get(); }

    // Some image is still being decoded: another frame will be needed to show it.
    bool loading() const;
    // Why `image` couldn't be loaded; empty if it's fine, loading, or unknown.
    QString error(const QString &image) const;
    // True once after a frame in which an image failed to load.
    bool takeErrorsChanged();

private:
    struct Entry
    {
        std::future<std::pair<QImage, QString>> pending; // image, error
        std::unique_ptr<QRhiTexture> texture;
        QString error;
        int unusedFrames = 0;
    };
    using Key = std::pair<QString, bool>; // image reference, linear

    QRhi *m_rhi = nullptr;
    std::map<Key, Entry> m_entries;
    std::array<std::unique_ptr<QRhiTexture>, 4> m_fallbacks; // by TextureFallback
    bool m_fallbacksUploaded = false;
    bool m_errorsChanged = false;
    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QThreadPool> m_pool; // decodes images
};
