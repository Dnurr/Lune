#include "playback.h"

#include <cmath>

#include "engine/scene/scene.h"

Playback::Playback(Scene *scene, QObject *parent)
    : QObject(parent)
    , m_scene(scene)
    , m_frame(scene->timeline().start)
{
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &Playback::tick);
    // A new range or rate: stay inside it, and keep the playing speed right.
    connect(m_scene, &Scene::timelineChanged, this, [this] {
        setFrame(m_frame);
        if (isPlaying()) {
            m_playFrom = m_frame;
            m_clock.restart();
            m_timer.setInterval(qMax(1, int(500.0 / m_scene->timeline().fps)));
        }
    });
}

void Playback::setFrame(double frame)
{
    const double f = m_scene->timeline().clamp(frame);
    if (f == m_frame)
        return;
    m_frame = f;
    emit frameChanged(m_frame);
}

float Playback::seconds() const
{
    return float(m_scene->timeline().seconds(m_frame));
}

void Playback::play()
{
    if (isPlaying())
        return;
    const Timeline &t = m_scene->timeline();
    if (m_frame >= t.end)
        setFrame(t.start); // at the end: play from the start
    m_playFrom = std::floor(m_frame);
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
    setFrame(std::round(m_frame) + frames);
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
