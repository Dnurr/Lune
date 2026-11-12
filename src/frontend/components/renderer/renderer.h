#pragma once

#include <QRhiWidget>
#include <QPoint>
#include <QVector3D>
#include <array>
#include <functional>
#include <memory>

class QRhiBuffer;
class QRhiGraphicsPipeline;
class QRhiRenderPassDescriptor;
class QRhiSampler;
class QRhiShaderResourceBindings;
class QRhiTexture;
class QRhiTextureRenderTarget;

// Backend-agnostic viewport (Direct3D 11/12, Vulkan, Metal, OpenGL).
// Choose the backend with setApi(QRhiWidget::Api::...) before the widget is shown;
// otherwise the platform default is used (Direct3D 11 on Windows).
//
// Frame:
//   1. scene  -> world grid on y = 0 (offscreen texture)
//   2. blur   -> scene downsampled to 1/4 and blurred (Gaussian, 2 iterations)
//   3. final  -> scene, with the blurred copy inside the "glass" rectangles
//
// Glass rectangles: every direct child widget with the dynamic property "glass" = true
// (the floating Panels set it) gets the blurred scene behind it, with rounded corners
// ("glassRadius" property, default 12). Moving/resizing/showing/hiding them redraws.
//
// Camera (orbit around a target): right drag = orbit, middle drag = pan, wheel = zoom.
class Viewport : public QRhiWidget
{
public:
    explicit Viewport(QWidget *parent = nullptr);
    ~Viewport() override;

    void resize(int x, int y);
    void update(std::function<void()> f);

protected:
    void initialize(QRhiCommandBuffer *cb) override;
    void render(QRhiCommandBuffer *cb) override;
    void releaseResources() override;

    void childEvent(QChildEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void createSizeDependentResources();
    void createPipelines();
    QVector3D cameraEye() const;

    QRhi *m_rhi = nullptr;
    QSize m_pixelSize;

    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiBuffer> m_gridUbuf;
    std::unique_ptr<QRhiBuffer> m_downUbuf;
    std::array<std::unique_ptr<QRhiBuffer>, 4> m_blurUbufs; // H, V, H, V
    std::unique_ptr<QRhiBuffer> m_compositeUbuf;

    // Offscreen targets: full-size scene, two 1/4-size ping-pong textures for the blur.
    std::unique_ptr<QRhiTexture> m_sceneTex, m_blurA, m_blurB;
    std::unique_ptr<QRhiTextureRenderTarget> m_sceneRt, m_blurARt, m_blurBRt;
    std::unique_ptr<QRhiRenderPassDescriptor> m_offscreenRp; // shared by all offscreen targets

    std::unique_ptr<QRhiShaderResourceBindings> m_gridSrb, m_downSrb, m_compositeSrb;
    std::array<std::unique_ptr<QRhiShaderResourceBindings>, 4> m_blurSrbs;

    std::unique_ptr<QRhiGraphicsPipeline> m_gridPipe, m_downPipe, m_blurPipe, m_compositePipe;

    // Orbit camera
    QVector3D m_target { 0.0f, 0.0f, 0.0f };
    float m_yaw = 45.0f;    // degrees around Y
    float m_pitch = 30.0f;  // degrees above the ground
    float m_distance = 15.0f;
    Qt::MouseButton m_dragButton = Qt::NoButton;
    QPoint m_lastMouse;
};
