#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

class Scene;

// The frame the editor shows, and playing it back.
//
// The frame always lies within the scene's Timeline (it follows range changes). Playing
// runs in real time at the timeline's frame rate: frames are dropped when the viewport
// can't keep up, so the duration is always right, and at the end it loops to the start.
class Playback : public QObject
{
    Q_OBJECT

public:
    explicit Playback(Scene *scene, QObject *parent = nullptr);

    double frame() const { return m_frame; }
    void setFrame(double frame); // limited to the timeline
    // Scene time of the frame shown: Timeline::seconds(frame()).
    float seconds() const;

    bool isPlaying() const { return m_timer.isActive(); }
    void play();
    void pause();
    void togglePlaying();

    void step(int frames); // pauses; limited to the range
    void goToStart();
    void goToEnd();

signals:
    void frameChanged(double frame);
    void playingChanged(bool playing);

private:
    void tick();

    Scene *m_scene;
    double m_frame = 1.0;
    QTimer m_timer;
    QElapsedTimer m_clock;     // since play()
    double m_playFrom = 1.0;   // frame shown when play() was called
};
