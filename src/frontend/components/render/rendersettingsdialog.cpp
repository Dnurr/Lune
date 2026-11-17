#include "rendersettingsdialog.h"

#include <QBoxLayout>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>

#include "engine/render/imagerender.h"

RenderSettingsDialog::RenderSettingsDialog(const RenderSettings &settings, const QSize &viewportPixels,
                                           const QString &scenePath, QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
{
    setWindowTitle("Render Settings");
    auto *layout = new QVBoxLayout(this);

    // Image
    auto *image = new QGroupBox("Image", this);
    auto *form = new QFormLayout(image);
    m_viewportSize = new QCheckBox(QStringLiteral("Viewport size (%1 x %2)").arg(viewportPixels.width()).arg(viewportPixels.height()),
                                   image);
    m_viewportSize->setChecked(settings.viewportSize);
    form->addRow(m_viewportSize);
    auto spin = [image](int value) {
        auto *s = new QSpinBox(image);
        s->setRange(RenderSettings::MinSize, RenderSettings::MaxSize);
        s->setSuffix(" px");
        s->setValue(value);
        return s;
    };
    m_width = spin(settings.width);
    m_height = spin(settings.height);
    auto *size = new QHBoxLayout;
    size->addWidget(m_width);
    size->addWidget(new QLabel("x", image));
    size->addWidget(m_height);
    form->addRow("Size", size);
    m_skybox = new QCheckBox("Environment as background", image);
    m_skybox->setToolTip("Off: the plain sky behind the scene, even when the environment has a background");
    m_skybox->setChecked(settings.skybox);
    form->addRow(m_skybox);
    layout->addWidget(image);

    // Output
    auto *output = new QGroupBox("Output", this);
    auto *outputRow = new QHBoxLayout(output);
    m_folder = new QLineEdit(settings.folder, output);
    RenderSettings defaults = settings;
    defaults.folder.clear();
    m_folder->setPlaceholderText(defaults.outputFolder(scenePath));
    m_folder->setToolTip("Folder of the images: empty for \"renders\" next to the scene; relative to the scene's folder");
    auto *browse = new QPushButton("Browse...", output);
    connect(browse, &QPushButton::clicked, this, [this, scenePath] {
        const QString dir = QFileDialog::getExistingDirectory(this, "Render Folder", this->settings().outputFolder(scenePath));
        if (!dir.isEmpty())
            m_folder->setText(dir);
    });
    outputRow->addWidget(m_folder, 1);
    outputRow->addWidget(browse);
    layout->addWidget(output);

    // Passes (F6)
    auto *passes = new QGroupBox("Render Passes (F6)", this);
    auto *grid = new QGridLayout(passes);
    int i = 0;
    for (const ImagePass &p : imagePasses()) {
        auto *box = new QCheckBox(p.title, passes);
        box->setChecked(settings.passes.contains(p.id));
        if (p.view.debug != DebugView::None)
            box->setToolTip(p.view.debug == DebugView::DirectOnly
                                ? "The scene lights only: no environment light, no emission"
                                : "Data, not an image: written without exposure or tonemapping");
        grid->addWidget(box, i / 2, i % 2);
        m_passes.insert(p.id, box);
        ++i;
    }
    layout->addWidget(passes);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_viewportSize, &QCheckBox::toggled, this, &RenderSettingsDialog::updateEnabled);
    updateEnabled();
}

void RenderSettingsDialog::updateEnabled()
{
    m_width->setEnabled(!m_viewportSize->isChecked());
    m_height->setEnabled(!m_viewportSize->isChecked());
}

RenderSettings RenderSettingsDialog::settings() const
{
    RenderSettings s = m_settings;
    s.viewportSize = m_viewportSize->isChecked();
    s.width = m_width->value();
    s.height = m_height->value();
    s.folder = m_folder->text().trimmed();
    s.skybox = m_skybox->isChecked();
    s.passes.clear();
    for (const ImagePass &p : imagePasses()) // in the passes' order
        if (m_passes.value(p.id)->isChecked())
            s.passes.append(p.id);
    return s;
}
