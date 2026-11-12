#pragma once

#include "../panel.h"
#include "engine/scene/transform.h"

class Vector3;

// Shows and edits a Transform. It doesn't touch the scene itself:
//   setTransform()  -> display a value (no signal)
//   edited()        -> the user changed something; the editor turns it into an undoable command
class TransformPanel : public Panel
{
    Q_OBJECT

public:
    explicit TransformPanel(QWidget *parent = nullptr);

    void setTransform(const Transform &transform);
    const Transform &transform() const { return m_value; }

signals:
    void edited(const Transform &transform, TransformField field);

private:
    void onEdited(TransformField field);

    Vector3 *m_position = nullptr;
    Vector3 *m_rotation = nullptr;
    Vector3 *m_scale = nullptr;
    Transform m_value;
    bool m_updating = false; // true inside setTransform(): ignore the widgets' own signals
};
