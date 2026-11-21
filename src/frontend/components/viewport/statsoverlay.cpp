#include "statsoverlay.h"

#include <QEvent>
#include <QLocale>

#include "engine/render/renderer.h"

namespace
{
constexpr qint64 kWindowMs = 500; // the text is refreshed (and averages reset) twice a second
constexpr int kTopMargin = 56; // below the camera bar
} // namespace

StatsOverlay::StatsOverlay(QWidget *viewport)
    : QLabel(viewport)
{
    setObjectName("StatsOverlay");
    setProperty("glass", true); // the viewport blurs the scene behind it
    setProperty("glassRadius", 10);
    setAttribute(Qt::WA_TransparentForMouseEvents); // clicks reach the viewport
    setTextFormat(Qt::PlainText);
    setText(QStringLiteral("measuring..."));
    viewport->installEventFilter(this);
    hide();
}

void StatsOverlay::frameRendered(double cpuMs, const FrameStats &stats, const char *backend, const QSize &pixelSize,
                                 int entities)
{
    if (!m_window.isValid())
        m_window.start();
    ++m_frames;
    m_cpuTotal += cpuMs;
    m_cpuMax = qMax(m_cpuMax, cpuMs);

    const qint64 elapsed = m_window.elapsed();
    if (elapsed < kWindowMs)
        return;

    const double fps = m_frames * 1000.0 / double(elapsed);
    const double frameMs = double(elapsed) / m_frames;
    const QLocale c = QLocale::c();
    setText(QStringLiteral("%1  %2 x %3\n"
                           "%4 fps   %5 ms/frame\n"
                           "CPU %6 ms  (max %7)\n"
                           "%8 draws  %9 passes  %10 tris\n"
                           "%11 meshes  %12 entities  %13 textures%14")
                .arg(QString::fromLatin1(backend))
                .arg(pixelSize.width())
                .arg(pixelSize.height())
                .arg(fps, 0, 'f', 1)
                .arg(frameMs, 0, 'f', 2)
                .arg(m_cpuTotal / m_frames, 0, 'f', 2)
                .arg(m_cpuMax, 0, 'f', 2)
                .arg(stats.drawCalls)
                .arg(stats.renderPasses)
                .arg(c.toString(qulonglong(stats.triangles)))
                .arg(stats.meshes)
                .arg(entities)
                .arg(stats.textures)
                .arg((stats.emitters == 0 ? QString()
                                          : QStringLiteral("\n%1 particles  %2 emitters  %3 steps")
                                                .arg(c.toString(qulonglong(stats.particles)))
                                                .arg(stats.emitters)
                                                .arg(stats.particleSteps))
                     + (stats.fluids == 0 ? QString()
                                          : QStringLiteral("\n%1 voxels  %2 fluids  %3 steps  cache %4 MB")
                                                .arg(c.toString(qulonglong(stats.voxels)))
                                                .arg(stats.fluids)
                                                .arg(stats.fluidSteps)
                                                .arg(double(stats.fluidCache) / (1024.0 * 1024.0), 0, 'f', 0))));
    reposition();

    m_window.restart();
    m_frames = 0;
    m_cpuTotal = 0.0;
    m_cpuMax = 0.0;
}

void StatsOverlay::showEvent(QShowEvent *event)
{
    // Start a fresh window: the time spent hidden isn't a frame.
    m_window.invalidate();
    m_frames = 0;
    m_cpuTotal = 0.0;
    m_cpuMax = 0.0;
    reposition();
    QLabel::showEvent(event);
}

void StatsOverlay::reposition()
{
    adjustSize();
    const QWidget *p = parentWidget();
    move((p->width() - width()) / 2, kTopMargin);
}

bool StatsOverlay::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize && isVisible())
        reposition();
    return QLabel::eventFilter(watched, event);
}
