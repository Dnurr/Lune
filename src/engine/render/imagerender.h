#pragma once

#include <QImage>
#include <QSize>
#include <QString>
#include <rhi/qrhi.h>
#include <vector>

#include "camera.h"
#include "renderer.h"

class QWindow;
class Scene;

// Offline image rendering (the editor's Render menu): the scene drawn by its own Renderer,
// on a QRhi created for the job and destroyed after it, so the result never depends on the
// viewport's size, overlays or panels, and the viewport's GPU state is left alone.
//
// Each image is a separate offscreen frame (uniform buffers are rewritten per frame, which
// one frame with several passes couldn't do on every backend). Frames are repeated until
// the material textures have loaded, then read back: 8-bit, after exposure and tonemapping,
// like the viewport shows them.

// One of the images an image render can produce: `id` names its file (scene_frame0001_<id>.png).
struct ImagePass
{
    QString id;
    QString title;
    ViewSettings view;
};

// Every pass Render Passes can write, in order. The first is the final image (Render Image).
const std::vector<ImagePass> &imagePasses();
const ImagePass *imagePass(const QString &id);

struct ImageRenderRequest
{
    QSize size;              // pixels
    float exposure = 0.0f;   // EV
    int tonemapper = 0;      // DisplaySettings
    bool skybox = true;      // applied to every view
    bool post = true;        // applied to every view (only Rendered ones have effects)
    float seconds = 0.0f;    // scene time (Renderer::setTime)
    std::vector<ViewSettings> views; // one image each
};

// Renders `request.views` of `scene` seen by `camera` (its lens shift ignored: the image is
// centered). `backend`: usually the viewport's (QRhi::backend()). `window`: where a Vulkan
// instance can be found (the viewport's window); may be null. Blocks until done. Returns one
// image per view, or none and `error` set.
std::vector<QImage> renderImages(QRhi::Implementation backend, QWindow *window, const Scene &scene, OrbitCamera camera,
                                 const ImageRenderRequest &request, QString *error);
