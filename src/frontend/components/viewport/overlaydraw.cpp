#include "overlaydraw.h"

#include <QtMath>
#include <cmath>

namespace OverlayDraw
{
void triangle(std::vector<OverlayVertex> &out, const QPointF &a, const QPointF &b, const QPointF &c, const QColor &col)
{
    for (const QPointF &p : { a, b, c })
        out.push_back({ float(p.x()), float(p.y()), float(col.redF()), float(col.greenF()), float(col.blueF()),
                        float(col.alphaF()) });
}

void quad(std::vector<OverlayVertex> &out, const QPointF &a, const QPointF &b, const QPointF &c, const QPointF &d,
          const QColor &col)
{
    triangle(out, a, b, c, col);
    triangle(out, a, c, d, col);
}

void segment(std::vector<OverlayVertex> &out, const QPointF &p, const QPointF &q, float width, const QColor &col)
{
    const QPointF d = q - p;
    const double len = std::hypot(d.x(), d.y());
    if (len < 1e-6)
        return;
    const QPointF n(-d.y() / len * width * 0.5, d.x() / len * width * 0.5);
    quad(out, p + n, q + n, q - n, p - n, col);
}

void square(std::vector<OverlayVertex> &out, const QPointF &c, float half, const QColor &col)
{
    quad(out, c + QPointF(-half, -half), c + QPointF(half, -half), c + QPointF(half, half), c + QPointF(-half, half), col);
}

namespace
{
QPointF onCircle(const QPointF &c, double radius, int i, int segments)
{
    const double a = 2.0 * M_PI * i / segments;
    return c + QPointF(std::cos(a) * radius, std::sin(a) * radius);
}
} // namespace

void disc(std::vector<OverlayVertex> &out, const QPointF &c, float radius, const QColor &col, int segments)
{
    for (int i = 0; i < segments; ++i)
        triangle(out, c, onCircle(c, radius, i, segments), onCircle(c, radius, i + 1, segments), col);
}

void ring(std::vector<OverlayVertex> &out, const QPointF &c, float radius, float width, const QColor &col, int segments)
{
    const double inner = radius - width * 0.5;
    const double outer = radius + width * 0.5;
    for (int i = 0; i < segments; ++i)
        quad(out, onCircle(c, inner, i, segments), onCircle(c, outer, i, segments), onCircle(c, outer, i + 1, segments),
             onCircle(c, inner, i + 1, segments), col);
}

double distanceToSegment(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const double len2 = QPointF::dotProduct(ab, ab);
    const double t = len2 > 0 ? qBound(0.0, QPointF::dotProduct(p - a, ab) / len2, 1.0) : 0.0;
    const QPointF closest = a + ab * t;
    return std::hypot(p.x() - closest.x(), p.y() - closest.y());
}
} // namespace OverlayDraw
