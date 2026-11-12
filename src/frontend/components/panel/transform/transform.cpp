#include "transform.h"

#include <QBoxLayout>

#include "frontend/components/widgets/vector3.h"

TransformPanel::TransformPanel(QWidget *parent)
    : Panel("TRANSFORM", parent)
{
    m_position = new Vector3("position", this);
    m_rotation = new Vector3("rotation", this);
    m_scale = new Vector3("scale", this);

    // Value change per pixel dragged on a label (Shift = x0.1, Ctrl = x10).
    m_position->setDragStep(0.02); // world units
    m_rotation->setDragStep(0.5);  // degrees
    m_scale->setDragStep(0.01);

    this->contentLayout()->addWidget(m_position);
    this->contentLayout()->addWidget(m_rotation);
    this->contentLayout()->addWidget(m_scale);

    alignLabels({ m_position, m_rotation, m_scale });

    connect(m_position, &Vector3::valueChanged, this, [this] { onEdited(TransformField::Position); });
    connect(m_rotation, &Vector3::valueChanged, this, [this] { onEdited(TransformField::Rotation); });
    connect(m_scale, &Vector3::valueChanged, this, [this] { onEdited(TransformField::Scale); });

    setTransform(Transform {});
}

void TransformPanel::setTransform(const Transform &transform)
{
    m_value = transform;
    m_updating = true;
    m_position->setValue(transform.position);
    m_rotation->setValue(transform.rotation);
    m_scale->setValue(transform.scale);
    m_updating = false;
}

void TransformPanel::onEdited(TransformField field)
{
    if (m_updating)
        return;
    m_value.position = m_position->value();
    m_value.rotation = m_rotation->value();
    m_value.scale = m_scale->value();
    emit edited(m_value, field);
}
