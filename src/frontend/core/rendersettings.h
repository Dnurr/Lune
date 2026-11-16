#pragma once

#include <QJsonObject>
#include <QSize>
#include <QString>
#include <QStringList>

// Settings of the Render menu (Render > Render Settings), saved per scene in the "editor"
// section of the scene file ("render").
//
//   F4  Render Image    the final image (the "rendered" pass) at the current frame
//   F5  Render Video    not yet
//   F6  Render Passes   one image per pass listed in `passes` (ids of imagePasses())
//
// Files: <folder>/<scene>_frame0001.png, and <scene>_frame0001_<pass>.png for F6. The
// folder defaults to "renders" next to the scene file (Pictures/Lune for an unsaved scene).
struct RenderSettings
{
    bool viewportSize = true; // the viewport's size in pixels; false: width x height
    int width = 1920;
    int height = 1080;
    QString folder;           // empty: the default folder
    bool skybox = true;       // the environment as background, when it has one
    QStringList passes = defaultPasses();
    int frame = 1;            // the current frame (no animation yet)

    static constexpr int MinSize = 16;
    static constexpr int MaxSize = 16384;

    static QStringList defaultPasses();

    QJsonObject toJson() const;
    // Missing or invalid values (older files, unknown passes): defaults, sizes clamped.
    static RenderSettings fromJson(const QJsonObject &json);

    // Where the images go, for a scene saved at `scenePath` (empty: never saved).
    QString outputFolder(const QString &scenePath) const;
    // "<base>_frame0001.png", or "<base>_frame0001_<passId>.png".
    static QString fileName(const QString &base, int frame, const QString &passId = QString());
    // The size to render at, given the viewport's size in pixels.
    QSize imageSize(const QSize &viewportPixels) const;
};
