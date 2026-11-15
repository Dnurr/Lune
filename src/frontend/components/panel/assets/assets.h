#pragma once

#include "../panel.h"

class QFileSystemModel;
class QLabel;
class QListView;
class QModelIndex;
class QSortFilterProxyModel;
class QSplitter;
class QTreeView;

// File browser on a root folder, in two linked views:
//   folders   the root folder and its subfolders, as a tree
//   contents  the files and folders inside the folder picked in the tree, as icons
// Only files the editor can use are listed (scenes, meshes, images). Double-clicking a
// folder in the contents opens it; double-clicking a file emits fileActivated().
//
// The root is the open scene's folder, or one picked with the folder button.
class AssetsPanel : public Panel
{
    Q_OBJECT

public:
    explicit AssetsPanel(QWidget *parent = nullptr, Side side = Side::Bottom);

    void setRootPath(const QString &path);
    QString rootPath() const { return m_root; }
    void setCurrentPath(const QString &path); // a folder inside the root
    QString currentPath() const { return m_current; }

    // Splitter sizes and the current folder, for the editor layout.
    QJsonObject viewState() const;
    void restoreViewState(const QJsonObject &state);

signals:
    void fileActivated(const QString &path);
    void rootPathChanged(const QString &path); // picked with the folder button

private:
    void chooseRoot();
    void showFolder(const QModelIndex &treeIndex);
    void updateOrientation();

    QString m_root;
    QString m_current;
    QLabel *m_path = nullptr;
    QSplitter *m_splitter = nullptr;
    QFileSystemModel *m_folders = nullptr;   // directories only, for the tree
    QSortFilterProxyModel *m_rootOnly = nullptr; // keeps only the root among its siblings
    QFileSystemModel *m_files = nullptr;     // files and folders, for the contents
    QTreeView *m_tree = nullptr;
    QListView *m_contents = nullptr;
};
