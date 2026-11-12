#include "transform.h"

#include <QDoubleSpinBox>
#include <QMouseEvent>

TransformPanel::TransformPanel(QWidget *parent)
    : Panel("TRANSFORM", parent)
{
    auto *position = new Vector3("position", this);
    position->setValue(QVector3D(0.0f, 0.0f, 0.0f));
    auto *rotation = new Vector3("rotation", this);
    rotation->setValue(QVector3D(0.0f, 0.0f, 0.0f));
    auto *scale = new Vector3("scale", this);
    scale->setValue(QVector3D(1.0f, 1.0f, 1.0f));
    this->contentLayout()->addWidget(position);
    this->contentLayout()->addWidget(rotation);
    this->contentLayout()->addWidget(scale);

    alignLabels({ position, rotation, scale });
}

// ---------------------------------------------------------------------------
// DragLabel
// ---------------------------------------------------------------------------

DragLabel::DragLabel(const QString &text, QWidget *parent)
    : QLabel(text, parent)
{
    setCursor(Qt::SizeHorCursor);
}

void DragLabel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_lastX = qRound(event->globalPosition().x());
        event->accept();
        return;
    }
    QLabel::mousePressEvent(event);
}

void DragLabel::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging) {
        QLabel::mouseMoveEvent(event);
        return;
    }
    // Global coordinates: stable even when the cursor leaves the label
    // (the label keeps receiving moves while the button is held).
    const int x = qRound(event->globalPosition().x());
    const int dx = x - m_lastX;
    m_lastX = x;
    if (dx != 0)
        emit dragged(dx, event->modifiers());
    event->accept();
}

void DragLabel::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragging && event->button() == Qt::LeftButton) {
        m_dragging = false;
        event->accept();
        return;
    }
    QLabel::mouseReleaseEvent(event);
}

double dragFactor(Qt::KeyboardModifiers modifiers)
{
    if (modifiers & Qt::ShiftModifier)
        return 0.1;
    if (modifiers & Qt::ControlModifier)
        return 10.0;
    return 1.0;
}

// ---------------------------------------------------------------------------
// Coordinate
// ---------------------------------------------------------------------------

Coordinate::Coordinate(const QString &label, CoordColour colour, QWidget *parent) : QFrame(parent)
{
    QColor c;

    switch (colour)
    {
    case CoordColour::Red:
        c.setRgb(255, 0, 0);
        break;

    case CoordColour::Green:
        c.setRgb(0, 255, 0);
        break;

    case CoordColour::Blue:
        c.setRgb(0, 0, 255);
        break;
    }

    // The spin box sits inside the coloured frame: no border/background of its own.
    this->setStyleSheet(
        QString("Coordinate { border: 2px solid rgb(%1, %2, %3); border-radius: 7px }"
                "Coordinate QDoubleSpinBox { border: none; background: transparent; padding: 0px; }")
            .arg(c.red())
            .arg(c.green())
            .arg(c.blue())
    );

    this->setContentsMargins(0, 0, 0, 0);

    auto *layout = new QHBoxLayout(this);
    auto *l = new DragLabel(label, this);
    l->setAlignment(Qt::AlignVCenter);

    m_spin = new QDoubleSpinBox(this);
    m_spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_spin->setRange(-1e6, 1e6);
    m_spin->setDecimals(3);
    m_spin->setSingleStep(0.1);         // mouse wheel / arrow keys
    m_spin->setKeyboardTracking(false); // typing: update on Enter / focus out, not on every key
    m_spin->setMinimumWidth(24);        // let it shrink: the default width fits "-1000000.000"
    m_spin->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    layout->addWidget(l);
    layout->addWidget(m_spin, 1);
    layout->setSpacing(2);
    layout->setContentsMargins(3, 0, 3, 3);

    connect(m_spin, &QDoubleSpinBox::valueChanged, this, &Coordinate::valueChanged);
    connect(l, &DragLabel::dragged, this, [this](int dx, Qt::KeyboardModifiers mods) {
        setValue(value() + dx * m_dragStep * dragFactor(mods));
    });
}

double Coordinate::value() const
{
    return m_spin->value();
}

void Coordinate::setValue(double value)
{
    m_spin->setValue(value); // emits valueChanged only if it actually changed
}

// ---------------------------------------------------------------------------
// Vector3
// ---------------------------------------------------------------------------

Vector3::Vector3(const QString &label, QWidget *parent) : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    auto *coords = new QHBoxLayout(); // no parent: it's added to `layout` below

    m_x = new Coordinate("x:", CoordColour::Red, this);
    m_y = new Coordinate("y:", CoordColour::Green, this);
    m_z = new Coordinate("z:", CoordColour::Blue, this);

    coords->addWidget(m_x);
    coords->addWidget(m_y);
    coords->addWidget(m_z);

    coords->setSpacing(3);

    // Dragging on the vector's own label moves all three coordinates together.
    auto *l = new DragLabel(label, this);
    l->setAlignment(Qt::AlignVCenter);
    m_label = l;

    layout->addWidget(l);
    layout->addLayout(coords);
    layout->setSpacing(10);
    layout->setStretch(1, 1);
    layout->setContentsMargins(3, 0, 3, 3);

    for (Coordinate *c : { m_x, m_y, m_z }) {
        connect(c, &Coordinate::valueChanged, this, [this] {
            if (!m_batch)
                emit valueChanged(value());
        });
    }

    connect(l, &DragLabel::dragged, this, [this](int dx, Qt::KeyboardModifiers mods) {
        const float delta = float(dx * m_dragStep * dragFactor(mods));
        setValue(value() + QVector3D(delta, delta, delta));
    });
}

QVector3D Vector3::value() const
{
    return QVector3D(float(m_x->value()), float(m_y->value()), float(m_z->value()));
}

void Vector3::setValue(const QVector3D &v)
{
    const QVector3D before = value();

    m_batch = true;
    m_x->setValue(v.x());
    m_y->setValue(v.y());
    m_z->setValue(v.z());
    m_batch = false;

    if (value() != before)
        emit valueChanged(value());
}

void Vector3::setDragStep(double step)
{
    m_dragStep = step;
    for (Coordinate *c : { m_x, m_y, m_z })
        c->setDragStep(step);
}

int Vector3::labelWidthHint() const
{
    return m_label->sizeHint().width();
}

void Vector3::setLabelWidth(int width)
{
    m_label->setFixedWidth(width);
}

void alignLabels(const QList<Vector3 *> &vectors)
{
    int w = 0;
    for (const Vector3 *v : vectors)
        w = qMax(w, v->labelWidthHint());
    for (Vector3 *v : vectors)
        v->setLabelWidth(w);
}
