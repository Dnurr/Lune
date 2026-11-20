#include "batchrender.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QtGlobal>
#include <cmath>
#include <cstdio>

#include "appsettings.h"
#include "displaysettings.h"
#include "engine/io/sceneio.h"
#include "engine/render/imagerender.h"
#include "engine/scene/animation.h"
#include "engine/scene/scene.h"
#include "rendersettings.h"

namespace
{
void print(FILE *to, const QString &text)
{
    std::fputs(qPrintable(text + QLatin1Char('\n')), to);
    std::fflush(to);
}
} // namespace

int renderFromCommandLine(const QStringList &args)
{
    if (args.isEmpty() || args.size() > 2) {
        print(stderr, QStringLiteral("Usage: Lune --render scene.lune [image.png]"));
        return 2;
    }
    const QString scenePath = QFileInfo(args.at(0)).absoluteFilePath();

    Scene scene;
    QJsonObject state;
    QStringList warnings;
    QString error;
    if (!SceneIO::load(scene, scenePath, &state, &warnings, &error)) {
        print(stderr, error);
        return 1;
    }
    for (const QString &w : warnings)
        print(stderr, QStringLiteral("Warning: ") + w);

    // The frame saved with the scene, as the editor opens it.
    const int frame = int(std::lround(scene.timeline().clamp(state["timeline"].toObject()["frame"].toDouble(scene.timeline().start))));
    scene.setFrame(frame);
    Animation::apply(scene);

    const RenderSettings settings = RenderSettings::fromJson(state["render"].toObject());
    const QJsonObject display = state["display"].toObject();
    const QJsonObject shading = state["shading"].toObject();
    ImageRenderRequest request;
    request.size = settings.imageSize(QSize(1920, 1080));
    request.exposure = float(display["exposure"].toDouble(0.0));
    request.tonemapper = qMax(0, DisplaySettings::tonemapperIds().indexOf(display["tonemapper"].toString()));
    request.skybox = shading["skybox"].toBool(true);
    request.post = shading["post"].toBool(true);
    request.seconds = float(scene.timeline().seconds(frame));
    request.views.push_back(imagePasses().front().view); // the final image

    // From the scene camera; without one, from the viewport camera saved with the scene.
    OrbitCamera camera;
    if (const Entity *cam = scene.sceneCamera()) {
        camera = OrbitCamera::fromSceneCamera(cam->transform, *cam->camera);
    } else {
        const QJsonObject c = state["camera"].toObject();
        const QJsonArray t = c["target"].toArray();
        if (t.size() == 3)
            camera.target = QVector3D(float(t[0].toDouble()), float(t[1].toDouble()), float(t[2].toDouble()));
        camera.yaw = float(c["yaw"].toDouble(camera.yaw));
        camera.pitch = qBound(-89.0f, float(c["pitch"].toDouble(camera.pitch)), 89.0f);
        camera.distance = qBound(0.5f, float(c["distance"].toDouble(camera.distance)), 500.0f);
    }

    QString out;
    if (args.size() == 2) {
        out = QFileInfo(args.at(1)).absoluteFilePath();
    } else {
        const QString folder = settings.outputFolder(scenePath);
        QDir().mkpath(folder);
        out = QDir(folder).filePath(RenderSettings::fileName(QFileInfo(scenePath).completeBaseName(), frame));
    }

    const std::vector<QImage> images = renderImages(AppSettings::backend(), nullptr, scene, camera, request, &error);
    if (images.size() != 1) {
        print(stderr, error.isEmpty() ? QStringLiteral("Rendering failed.") : error);
        return 1;
    }
    if (!images.front().save(out)) {
        print(stderr, QStringLiteral("Can't write ") + QDir::toNativeSeparators(out));
        return 1;
    }
    print(stdout, QDir::toNativeSeparators(out));
    return 0;
}
