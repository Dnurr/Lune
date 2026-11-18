#include "playback.h"

#include <cmath>

#include "engine/scene/scene.h"

Playback::Playback(Scene *scene, QObject *parent)
    : QObject(parent)
    , m_scene(scene)
{
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &Playback::tick);
    connect(m_scene, &Scene::frameChanged, this, &Playback::frameChanged);
    // A new rate: keep the playing speed right.
    connect(m_scene, &Scene::timelineChanged, this, [this] {
        if (isPlaying()) {
            m_playFrom = frame();
            m_clock.restart();
            m_timer.setInterval(qMax(1, int(500.0 / m_scene->timeline().fps)));
        }
    });
}

double Playback::frame() const
{
    return m_scene->frame();
}

void Playback::setFrame(double frame)
{
    m_scene->setFrame(frame);
}

float Playback::seconds() const
{
    return float(m_scene->timeline().seconds(frame()));
}

void Playback::play()
{
    if (isPlaying())
        return;
    const Timeline &t = m_scene->timeline();
    if (frame() >= t.end)
        setFrame(t.start); // at the end: play from the start
    m_playFrom = std::floor(frame());
    m_clock.start();
    // Twice per frame, so a frame is never shown much later than its time.
    m_timer.start(qMax(1, int(500.0 / t.fps)));
    emit playingChanged(true);
}

void Playback::pause()
{
    if (!isPlaying())
        return;
    m_timer.stop();
    emit playingChanged(false);
}

void Playback::togglePlaying()
{
    if (isPlaying())
        pause();
    else
        play();
}

void Playback::step(int frames)
{
    pause();
    setFrame(std::round(frame()) + frames);
}

void Playback::goToStart()
{
    setFrame(m_scene->timeline().start);
}

void Playback::goToEnd()
{
    setFrame(m_scene->timeline().end);
}

void Playback::tick()
{
    const Timeline &t = m_scene->timeline();
    const double elapsed = m_clock.nsecsElapsed() * 1e-9;
    double f = std::floor(m_playFrom + elapsed * t.fps);
    if (f > t.end) // loop
        f = t.start + std::fmod(f - t.start, double(t.frameCount()));
    setFrame(f);
}
