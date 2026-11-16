#pragma once

#include <QDialog>
#include <QImage>
#include <QStringList>
#include <vector>

class QLabel;
class QListWidget;

// The images of a render, after they're saved: the list of passes on the left (only with
// more than one image), the selected image fitted on the right, its file below, and a
// button that opens the folder. Not modal: the editor stays usable.
class RenderResultDialog : public QDialog
{
    Q_OBJECT

public:
    RenderResultDialog(const std::vector<QImage> &images, const QStringList &titles, const QStringList &paths,
                       QWidget *parent = nullptr);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void showImage(int index);
    void updatePixmap();

    std::vector<QImage> m_images;
    QStringList m_paths;
    int m_current = -1;
    QListWidget *m_list = nullptr;
    QLabel *m_image = nullptr;
    QLabel *m_path = nullptr;
};
