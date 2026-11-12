#pragma once

#include <QElapsedTimer>
#include <QLabel>

struct FrameStats;

// Small glass panel at the top of the viewport with frame statistics:
// backend and resolution, FPS and frame time, CPU time spent recording the frame,
// draw calls / passes / triangles, meshes and entities.
//
// While it's visible the viewport renders continuously (otherwise it only redraws on
// changes and FPS would mean nothing); FPS is capped by vsync.
// GPU time isn't shown: QRhiWidget doesn't let us enable GPU timestamps on its QRhi.
// Mouse events pass through it.
class StatsOverlay : public QLabel
{
    Q_OBJECT

public:
    explicit StatsOverlay(QWidget *viewport);

    // Called by the viewport after every rendered frame.
    void frameRendered(double cpuMs, const FrameStats &stats, const char *backend, const QSize &pixelSize,
                       int entities);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void reposition();

    QElapsedTimer m_window; // current averaging window
    int m_frames = 0;
    double m_cpuTotal = 0.0;
    double m_cpuMax = 0.0;
};
