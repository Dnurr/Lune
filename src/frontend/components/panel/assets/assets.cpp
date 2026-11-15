#include "assets.h"

#include <QBoxLayout>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QListView>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStandardPaths>
#include <QToolButton>
#include <QTreeView>

namespace
{
// Files the editor can use: scenes, meshes, environment and texture images.
const QStringList kAssetFilters = { "*.lune", "*.obj", "*.gltf", "*.glb", "*.hdr", "*.png", "*.jpg", "*.jpeg", "*.tga" };

QString clean(const QString &path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

bool isInside(const QString &path, const QString &root)
{
    return path == root || path.startsWith(root.endsWith('/') ? root : root + '/');
}

// The tree shows the root folder as its only top-level item: among the root's siblings
// (children of its parent) only the root itself is kept.
class RootOnlyProxy : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void setRoot(const QString &root)
    {
        m_root = root;
        m_parent = QFileInfo(root).dir().absolutePath();
        if (QDir(root).isRoot())
            m_parent.clear(); // a drive: its parent is "My Computer"
        invalidateFilter();
    }
    QString parentPath() const { return m_parent; }

protected:
    bool filterAcceptsRow(int row, const QModelIndex &sourceParent) const override
    {
        const auto *model = static_cast<const QFileSystemModel *>(sourceModel());
        if (clean(model->filePath(sourceParent)) != m_parent)
            return true; // not a sibling of the root
        return clean(model->filePath(model->index(row, 0, sourceParent))) == m_root;
    }

private:
    QString m_root, m_parent;
};
} // namespace

AssetsPanel::AssetsPanel(QWidget *parent, Side side)
    : Panel("Assets", parent, side)
{
    setContentFills(true);

    auto *body = new QWidget(this);
    auto *column = new QVBoxLayout(body);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(8);

    // Where we are, and the button to browse somewhere else.
    auto *bar = new QHBoxLayout;
    bar->setSpacing(6);
    m_path = new QLabel(body);
    m_path->setObjectName("PropertyValue");
    m_path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *choose = new QToolButton(body);
    choose->setText(QStringLiteral("Folder..."));
    choose->setToolTip(QStringLiteral("Choose the folder the assets come from"));
    connect(choose, &QToolButton::clicked, this, &AssetsPanel::chooseRoot);
    bar->addWidget(m_path, 1);
    bar->addWidget(choose);
    column->addLayout(bar);

    m_splitter = new QSplitter(body);
    m_splitter->setChildrenCollapsible(false);
    column->addWidget(m_splitter, 1);

    // Folders (tree)
    m_folders = new QFileSystemModel(this);
    m_folders->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Drives);
    m_folders->setRootPath(QString());
    m_rootOnly = new RootOnlyProxy(this);
    m_rootOnly->setSourceModel(m_folders);
    m_tree = new QTreeView(m_splitter);
    m_tree->setObjectName("AssetFolders");
    m_tree->setModel(m_rootOnly);
    m_tree->setHeaderHidden(true);
    for (int c = 1; c < m_folders->columnCount(); ++c)
        m_tree->hideColumn(c);
    m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    connect(m_tree->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &current) { showFolder(current); });

    // Contents (icons)
    m_files = new QFileSystemModel(this);
    m_files->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    m_files->setNameFilters(kAssetFilters);
    m_files->setNameFilterDisables(false); // hide the other files instead of greying them out
    m_contents = new QListView(m_splitter);
    m_contents->setObjectName("AssetContents");
    m_contents->setModel(m_files);
    m_contents->setViewMode(QListView::IconMode);
    m_contents->setResizeMode(QListView::Adjust);
    m_contents->setMovement(QListView::Static);
    m_contents->setIconSize(QSize(40, 40));
    m_contents->setGridSize(QSize(96, 84));
    m_contents->setUniformItemSizes(true);
    m_contents->setWordWrap(true);
    m_contents->setTextElideMode(Qt::ElideMiddle);
    m_contents->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_contents, &QListView::activated, this, [this](const QModelIndex &index) {
        const QString path = clean(m_files->filePath(index));
        if (m_files->isDir(index))
            setCurrentPath(path);
        else
            emit fileActivated(path);
    });

    m_splitter->addWidget(m_tree);
    m_splitter->addWidget(m_contents);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 3);

    contentLayout()->addWidget(body, 1);
    updateOrientation();
    connect(this, &Panel::sideChanged, this, &AssetsPanel::updateOrientation);

    setRootPath(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
}

void AssetsPanel::updateOrientation()
{
    // Side by side at the bottom, tree above the contents on the left / right.
    m_splitter->setOrientation(side() == Side::Bottom ? Qt::Horizontal : Qt::Vertical);
}

void AssetsPanel::setRootPath(const QString &path)
{
    const QString root = clean(QFileInfo(path).absoluteFilePath());
    if (root.isEmpty() || !QFileInfo(root).isDir() || root == m_root)
        return;
    m_root = root;
    static_cast<RootOnlyProxy *>(m_rootOnly)->setRoot(root);
    const QString parent = static_cast<RootOnlyProxy *>(m_rootOnly)->parentPath();
    m_tree->setRootIndex(m_rootOnly->mapFromSource(m_folders->index(parent)));
    m_tree->expand(m_rootOnly->mapFromSource(m_folders->index(root)));
    m_current.clear();
    setCurrentPath(root);
}

void AssetsPanel::setCurrentPath(const QString &path)
{
    QString folder = clean(QFileInfo(path).absoluteFilePath());
    if (!QFileInfo(folder).isDir() || !isInside(folder, m_root))
        folder = m_root;
    if (folder == m_current)
        return;
    m_current = folder;

    m_files->setRootPath(folder);
    m_contents->setRootIndex(m_files->index(folder));

    const QModelIndex treeIndex = m_rootOnly->mapFromSource(m_folders->index(folder));
    if (treeIndex.isValid() && m_tree->currentIndex() != treeIndex) {
        m_tree->setCurrentIndex(treeIndex);
        m_tree->scrollTo(treeIndex); // also expands the parents
    }

    // "Root / Sub / Folder"
    const QString relative = QDir(m_root).relativeFilePath(folder);
    QString name = QFileInfo(m_root).fileName();
    if (name.isEmpty())
        name = m_root; // a drive
    QString text = name;
    if (relative != QLatin1String("."))
        text += QStringLiteral("  /  ") + relative.split('/').join(QStringLiteral("  /  "));
    m_path->setText(text);
    m_path->setToolTip(QDir::toNativeSeparators(folder));
}

void AssetsPanel::showFolder(const QModelIndex &treeIndex)
{
    if (treeIndex.isValid())
        setCurrentPath(m_folders->filePath(m_rootOnly->mapToSource(treeIndex)));
}

void AssetsPanel::chooseRoot()
{
    const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("Assets Folder"), m_root);
    if (path.isEmpty())
        return;
    setRootPath(path);
    emit rootPathChanged(m_root);
}

QJsonObject AssetsPanel::viewState() const
{
    QJsonArray sizes;
    for (int s : m_splitter->sizes())
        sizes.append(s);
    QJsonObject state;
    state["splitter"] = sizes;
    state["folder"] = QDir(m_root).relativeFilePath(m_current);
    return state;
}

void AssetsPanel::restoreViewState(const QJsonObject &state)
{
    const QJsonArray sizes = state["splitter"].toArray();
    if (sizes.size() == 2 && sizes[0].toInt() > 0 && sizes[1].toInt() > 0)
        m_splitter->setSizes({ sizes[0].toInt(), sizes[1].toInt() });
    const QString folder = state["folder"].toString();
    if (!folder.isEmpty())
        setCurrentPath(QDir(m_root).filePath(folder));
}
