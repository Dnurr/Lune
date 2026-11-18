#pragma once

#include <QPointF>
#include <QRectF>
#include <QWidget>
#include <optional>
#include <vector>

#include "engine/scene/components.h"
#include "engine/scene/entity.h"
#include "frontend/components/panel/panel.h"

class QListWidget;
class QUndoStack;
class Scene;
class Selection;

// One number of an animated property, drawn as one curve: a track (index into the entity's
// animation) and which of its numbers (Animation::channel()).
struct CurveChannel
{
    int track = 0;
    int index = 0;
    int component = 0, property = 0; // components() indices of the track's property
    PropertyType type = PropertyType::Float;
    QString name;
    QColor color;
    bool visible = true;
};

// The curves of the selected entity's animation, value over frame.
//
// Mouse: click a key to select it (Shift: add / remove), drag keys to move them (in time by
// whole frames, and in value), drag a selected key's handle to set its tangent, drag on empty
// space to select keys in a box, middle drag to pan, wheel to zoom (Ctrl: time only, Shift:
// values only), drag on the frame numbers at the top to change the current frame.
// Keys: Delete removes the selected keys, Home / F frames every key. Right click: interpolation,
// automatic tangents, delete, frame all. Every edit is one undoable SetAnimationCommand.
class CurveView : public QWidget
{
    Q_OBJECT

public:
    CurveView(Scene *scene, QUndoStack *undo, QWidget *parent = nullptr);

    // The entity and channels to show (the panel's list decides which are visible).
    void setChannels(EntityId entity, std::vector<CurveChannel> channels);
    void setChannelVisible(int channel, bool visible);
    void frameAll();

    QSize sizeHint() const override { return { 600, 200 }; }

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    struct KeyRef
    {
        int track = 0, key = 0, index = 0; // the key of one channel
        bool operator==(const KeyRef &) const = default;
    };
    enum class Drag
    {
        None,
        Keys,
        Handle,
        Box,
        Pan,
        Scrub,
    };

    const Entity *entity() const;
    // Value / frame <-> widget pixels (the area under the frame numbers).
    QRectF plotRect() const;
    double xOf(double frame) const;
    double yOf(double value) const;
    double frameAt(double x) const;
    double valueAt(double y) const;

    QPointF keyPos(const Entity &e, const KeyRef &k) const;
    // The two handle ends of a selected key (left, right), in pixels; nullopt where it has none.
    std::optional<QPointF> handlePos(const Entity &e, const KeyRef &k, int side) const;
    std::optional<KeyRef> keyUnder(const QPointF &pos) const;
    bool handleUnder(const QPointF &pos, KeyRef *key, int *side) const;
    const CurveChannel *channelOf(const KeyRef &k) const;
    bool isSelected(const KeyRef &k) const;
    void dropInvalidSelection();

    // Animation of the entity at the start of the drag, with the selected keys moved / the
    // handle set; previewed on the scene while dragging, pushed as one command on release.
    std::vector<AnimationTrack> movedKeys(double frames, double value, std::vector<KeyRef> *selection) const;
    std::vector<AnimationTrack> withHandle(const QPointF &pos) const;
    void preview(const std::vector<AnimationTrack> &animation);
    void commit(const QString &text);
    void edit(const std::vector<AnimationTrack> &animation, const QString &text); // one command, now

    void deleteSelected();
    void setInterpolation(Interpolation interpolation);
    void setAutoTangents();

    Scene *m_scene;
    QUndoStack *m_undo;
    EntityId m_entity = InvalidEntity;
    std::vector<CurveChannel> m_channels;
    std::vector<KeyRef> m_selection;

    double m_f0 = 0.0, m_f1 = 100.0; // frames across the plot
    double m_v0 = 0.0, m_v1 = 1.0;   // values from bottom to top

    Drag m_drag = Drag::None;
    QPointF m_pressPos;
    QPointF m_lastPos;
    Entity m_dragStart;              // the entity when the drag started
    std::vector<KeyRef> m_dragSelection; // the selection when the drag started
    KeyRef m_handleKey;
    int m_handleSide = 1;
    bool m_dragChanged = false;
    QRectF m_box;
};

// Bottom panel with the curve editor: the channels of the selected entity's animated
// properties (checkable, colored like their curves) next to the CurveView.
class CurvesPanel : public Panel
{
    Q_OBJECT

public:
    CurvesPanel(Scene *scene, Selection *selection, QUndoStack *undo, QWidget *parent);

private:
    void rebuild(bool frame); // the channels of the selected entity; frame: fit the view to them

    Scene *m_scene;
    Selection *m_selection;
    QListWidget *m_list = nullptr;
    CurveView *m_view = nullptr;
    EntityId m_shown = InvalidEntity;
    QStringList m_shownTracks; // "component/property" of the channels listed
};
