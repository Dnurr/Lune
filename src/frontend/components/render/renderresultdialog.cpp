#include "renderresultdialog.h"

#include <QBoxLayout>
#include <QDesktopServices>
#include <QDir>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QUrl>

RenderResultDialog::RenderResultDialog(const std::vector<QImage> &images, const QStringList &titles,
                                       const QStringList &paths, QWidget *parent)
    : QDialog(parent)
    , m_images(images)
    , m_paths(paths)
{
    setWindowTitle("Render");
    setAttribute(Qt::WA_DeleteOnClose);
    resize(1100, 700);

    auto *layout = new QVBoxLayout(this);
    auto *row = new QHBoxLayout;
    m_list = new QListWidget(this);
    m_list->addItems(titles);
    m_list->setFixedWidth(200);
    m_list->setVisible(images.size() > 1);
    row->addWidget(m_list);
    m_image = new QLabel(this);
    m_image->setAlignment(Qt::AlignCenter);
    m_image->setMinimumSize(320, 200);
    m_image->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    row->addWidget(m_image, 1);
    layout->addLayout(row, 1);

    m_path = new QLabel(this);
    m_path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_path);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    QPushButton *open = buttons->addButton("Open Folder", QDialogButtonBox::ActionRole);
    connect(open, &QPushButton::clicked, this, [this] {
        if (m_current >= 0)
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_paths.value(m_current)).absolutePath()));
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    layout->addWidget(buttons);

    connect(m_list, &QListWidget::currentRowChanged, this, &RenderResultDialog::showImage);
    m_list->setCurrentRow(0);
    if (m_current < 0)
        showImage(0);
}

void RenderResultDialog::showImage(int index)
{
    if (index < 0 || index >= int(m_images.size()))
        return;
    m_current = index;
    const QImage &image = m_images[size_t(index)];
    m_path->setText(QStringLiteral("%1   (%2 x %3)").arg(QDir::toNativeSeparators(m_paths.value(index))).arg(image.width()).arg(image.height()));
    updatePixmap();
}

void RenderResultDialog::updatePixmap()
{
    if (m_current < 0)
        return;
    const QImage &image = m_images[size_t(m_current)];
    const QSize area = m_image->size() * m_image->devicePixelRatioF();
    // Fitted when larger than the area, otherwise at its own size.
    const bool larger = image.width() > area.width() || image.height() > area.height();
    QPixmap pixmap = QPixmap::fromImage(larger ? image.scaled(area, Qt::KeepAspectRatio, Qt::SmoothTransformation) : image);
    pixmap.setDevicePixelRatio(m_image->devicePixelRatioF());
    m_image->setPixmap(pixmap);
}

void RenderResultDialog::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    updatePixmap();
}
