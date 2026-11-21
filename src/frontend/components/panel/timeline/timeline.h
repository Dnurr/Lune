#pragma once

#include <QWidget>

#include "frontend/components/panel/panel.h"

class Playback;
class QDoubleSpinBox;
class QSpinBox;
class QToolButton;
class QUndoStack;
class Scene;
class Selection;

// The frame ruler of the Timeline panel: the scene's whole range from edge to edge, with
// numbered ticks, the keyframes of the selected entity (diamonds, amber on the current frame)
// and the current frame. Press or drag on it to scrub.
class TimelineRuler : public QWidget
{
    Q_OBJECT

public:
    TimelineRuler(Scene *scene, Playback *playback, Selection *selection, QWidget *parent = nullptr);

    QSize sizeHint() const override { return { 400, 36 }; }
    QSize minimumSizeHint() const override { return { 120, 30 }; }

    // Frame under x (widget coordinates), rounded, within the range; and back.
    int frameAt(int x) const;
    double xOf(double frame) const;

    // Frames shown without simulating (the fluid cache), as ranges: a band along the bottom.
    void setCachedFrames(const QList<QPair<int, int>> &ranges);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    Scene *m_scene;
    Playback *m_playback;
    Selection *m_selection;
    bool m_scrubbing = false;
    QList<QPair<int, int>> m_cached;
};

// Footer panel under everything, as wide as the window: playback buttons, the current
// frame, the ruler, and the scene's frame range and rate (undoable edits).
class TimelinePanel : public Panel
{
    Q_OBJECT

public:
    TimelinePanel(Scene *scene, Playback *playback, Selection *selection, QUndoStack *undo, QWidget *parent);

    // The fluid cache's frames, on the ruler (TimelineRuler::setCachedFrames()).
    void setCachedFrames(const QList<QPair<int, int>> &ranges);

private:
    void syncTimeline(); // fields from the scene's Timeline
    void syncFrame();    // frame field and ruler from the playback
    void syncPlaying();
    void editTimeline(int start, int end, double fps);

    Scene *m_scene;
    Playback *m_playback;
    QUndoStack *m_undo;
    TimelineRuler *m_ruler = nullptr;
    QToolButton *m_play = nullptr;
    QSpinBox *m_frame = nullptr;
    QSpinBox *m_start = nullptr;
    QSpinBox *m_end = nullptr;
    QDoubleSpinBox *m_fps = nullptr;
};
