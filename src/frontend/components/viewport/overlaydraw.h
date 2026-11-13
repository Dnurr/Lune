#pragma once

#include <QColor>
#include <QPointF>
#include <vector>

#include "engine/render/renderer.h"

// Small helpers that build 2D overlay triangles (widget pixels, display colors), shared by
// the gizmo and the light icons. The renderer draws them on top of the scene.
namespace OverlayDraw
{
void triangle(std::vector<OverlayVertex> &out, const QPointF &a, const QPointF &b, const QPointF &c, const QColor &col);
void quad(std::vector<OverlayVertex> &out, const QPointF &a, const QPointF &b, const QPointF &c, const QPointF &d,
          const QColor &col);
// Thick line from p to q.
void segment(std::vector<OverlayVertex> &out, const QPointF &p, const QPointF &q, float width, const QColor &col);
// Axis-aligned square of half side `half`.
void square(std::vector<OverlayVertex> &out, const QPointF &c, float half, const QColor &col);
// Filled circle.
void disc(std::vector<OverlayVertex> &out, const QPointF &c, float radius, const QColor &col, int segments = 20);
// Circle outline of the given line width.
void ring(std::vector<OverlayVertex> &out, const QPointF &c, float radius, float width, const QColor &col,
          int segments = 24);

// Distance from p to the segment ab.
double distanceToSegment(const QPointF &p, const QPointF &a, const QPointF &b);
} // namespace OverlayDraw
