#pragma once

#include <QtGlobal>

// The scene's time: the frame range that plays and its frame rate. The frame being shown is
// the editor's (Timeline panel), not the scene's: two views of one scene could show two.
//
// Frames are numbered like in DCC tools (the range starts at 1 by default) and can be
// fractional between two frames. Time 0 is the start frame: seconds(start) == 0, so
// effects driven by time look the same wherever the range begins.
struct Timeline
{
    int start = 1;
    int end = 250;
    double fps = 24.0;

    static constexpr double MinFps = 1.0;
    static constexpr double MaxFps = 240.0;

    double seconds(double frame) const { return (frame - start) / fps; }
    int frameCount() const { return end - start + 1; }
    // `frame` limited to the range.
    double clamp(double frame) const { return qBound(double(start), frame, double(end)); }
    // Start <= end, fps within its limits.
    Timeline valid() const
    {
        Timeline t = *this;
        t.end = qMax(t.start, t.end);
        t.fps = qBound(MinFps, t.fps, MaxFps);
        return t;
    }

    bool operator==(const Timeline &) const = default;
};
