#pragma once

#include <QObject>

class DisplaySettings;
class QUndoStack;

// Makes the viewport display settings (exposure, tonemapper) undoable.
//
// It watches DisplaySettings and records every change on the undo stack after it happened,
// so whatever edits the settings (the Display menu, the View menu, the palette, scripts later)
// needs no undo code. A change made by undo/redo itself is not recorded again.
//
// Recorded changes also make the document "modified", so closing or opening another scene
// asks to save them (the settings are saved in the scene file).
//
// Consecutive exposure changes close together (scrubbing the label, holding the spin box
// arrows) merge into one undo step, like property edits in the Inspector.
class DisplayHistory : public QObject
{
    Q_OBJECT

public:
    struct State
    {
        double exposure = 0.0;
        int tonemapper = 0;
        bool operator==(const State &) const = default;
    };

    DisplayHistory(DisplaySettings *settings, QUndoStack *undo, QObject *parent = nullptr);

    // Used by the undo command: applies `state` without recording it.
    void apply(const State &state);

private:
    void onChanged();
    State current() const;

    DisplaySettings *m_settings;
    QUndoStack *m_undo;
    State m_last;          // state after the last recorded (or applied) change
    bool m_applying = false;
};
