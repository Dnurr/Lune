#pragma once

#include <QObject>
#include <QStringList>

// How the viewport turns the HDR scene into the image on screen: exposure and tonemapping
// operator. Editor state (a viewing choice, not scene content): not undoable, saved in the
// "editor" section of the scene file.
//
// The renderer receives them in the display pass uniforms (display.frag):
//   params.x = exposure in EV (stops): the scene is multiplied by 2^EV
//   params.y = tonemapper index (Tonemapper below, as a float)
class DisplaySettings : public QObject
{
    Q_OBJECT

public:
    // Order = index passed to the shader. Add new operators at the end.
    enum Tonemapper
    {
        None = 0,
        Reinhard = 1,
        Aces = 2,
        AgX = 3,
    };

    static constexpr double MinExposure = -10.0;
    static constexpr double MaxExposure = 10.0;

    explicit DisplaySettings(QObject *parent = nullptr);

    double exposure() const { return m_exposure; }
    void setExposure(double ev); // clamped to [MinExposure, MaxExposure]

    int tonemapper() const { return m_tonemapper; }
    void setTonemapper(int tonemapper); // out-of-range values are ignored

    // Display names ("None", "Reinhard", ...) and short ids for files ("none", "reinhard", ...),
    // indexed by Tonemapper.
    static QStringList tonemapperNames();
    static QStringList tonemapperIds();

    void reset(); // exposure 0, no tonemapping

signals:
    void changed();

private:
    double m_exposure = 0.0;
    int m_tonemapper = None;
};
