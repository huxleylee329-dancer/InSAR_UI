#include "treeview.h"
#include "icon_source.h"
#include "icon_utils.h"
#include <qmessagebox.h>
#include "NodeUtils.h"
#include <QMenu>
#include <QMenuBar>  
#include <QStatusBar> 
#include <QFileDialog>
#include<QDir>
#include<QFile>
#include<QFileInfo>
#include<QSet>
#include<FormatConversion.h>
#include "tinyxml.h"
#include <QStyledItemDelegate>
#include <QPainter>
#include <QItemSelectionModel>
#include <QtConcurrent/QtConcurrent>
#include <QFuture>
#include <QFutureWatcher>

// Icons now use SVG currentColor - automatically follows widget color property
// No manual tinting needed - theme colors are set via stylesheet

static bool isDarkTheme(const QWidget *w)
{
    if (!w) return false;
    QColor bg = w->palette().color(w->backgroundRole());
    return bg.lightness() < 128;
}



class TreeViewDelegate : public QStyledItemDelegate
{
public:
    explicit TreeViewDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        // 如果是被选中的非叶子节点（有子节点的目录或工程）
        if ((opt.state & QStyle::State_Selected) && index.model()->hasChildren(index)) {
            bool isDark = isDarkTheme(opt.widget);
            QColor bgColor = isDark ? QColor(60, 60, 60) : QColor(230, 230, 230);

            // 1. 手动填充背景色，避开 QSS 的覆盖
            painter->save();
            painter->fillRect(opt.rect, bgColor);
            painter->restore();

            // 2. 清除 State_Selected 标志，防止基类样式表绘制蓝色背景和白色文字
            opt.state &= ~QStyle::State_Selected;
        }

        QStyledItemDelegate::paint(painter, opt, index);
    }
};

#ifdef _DEBUG
#pragma comment(lib, "Utils_d.lib")
#pragma comment(lib, "FormatConversion_d.lib")
#else
#pragma comment(lib, "Utils.lib")
#pragma comment(lib, "FormatConversion.lib")
#endif
TreeView::TreeView(QWidget* parent) : QTreeView(parent)
{
    this->num_pro =0;
    mTreeProcess = NULL;
    type = 2;
     model = new QStandardItemModel(0, 2);
     //modelSelection = new QItemSelectionModel(model);
     //this->setSelectionModel(modelSelection);
     this->setModel(model);
     //this->setSelectionBehavior(QAbstractItemView::SelectItems);
     //this->setSelectionMode(QAbstractItemView::ExtendedSelection);
     this->setColumnHidden(1, true);
     this->setContextMenuPolicy(Qt::CustomContextMenu);
     connect(this, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(slotCustomContextMenu(const QPoint&)));
     this->setItemDelegate(new TreeViewDelegate(this));
}

void TreeView::drawBranches(QPainter *painter, const QRect &rect, const QModelIndex &index) const
{
    QItemSelectionModel *selModel = selectionModel();
    bool isSelected = selModel && selModel->isSelected(index);

    if (isSelected && model->hasChildren(index)) {
        bool isDark = isDarkTheme(this);
        QColor bgColor = isDark ? QColor(60, 60, 60) : QColor(230, 230, 230);

        // 计算折叠箭头所在的最右侧缩进区域（宽度为 indentation()）
        int indent = indentation();
        int foldStart = rect.x() + qMax(0, rect.width() - indent);
        int foldWidth = qMin(rect.width(), indent);
        QRect foldRect(foldStart, rect.y(), foldWidth, rect.height());

        painter->save();
        painter->fillRect(foldRect, bgColor);
        painter->restore();

        // 临时阻断信号并取消选择，迫使基类 drawBranches 在常规状态下渲染折叠图标（无蓝色背景覆盖）
        selModel->blockSignals(true);
        selModel->select(index, QItemSelectionModel::Deselect);

        QTreeView::drawBranches(painter, rect, index);

        // 绘制完成后重新选择以恢复原状态
        selModel->select(index, QItemSelectionModel::Select);
        selModel->blockSignals(false);
    } else {
        QTreeView::drawBranches(painter, rect, index);
    }
}

void TreeView::init_tree()
{
    model->setHeaderData(0, Qt::Horizontal, tr("workspace"));
    model->setHeaderData(1, Qt::Horizontal, tr("Path"));
    type = 1;
}

void TreeView::NewProject(QString name, QString save_path)
{
    
    QStandardItem* item0 = new QStandardItem(name + ".insar");
    item0->setIcon(QIcon(PROJECT_ICON));
    item0->setStatusTip(NOT_IN_PROCESS);
    QStandardItem* item1_path = new QStandardItem(save_path);
    model->setRowCount(model->rowCount() + 1);
    model->setItem(model->rowCount()-1, 0, item0);
    model->setItem(model->rowCount() - 1, 1, item1_path);
    XMLFile xml;
    QFileInfo info = QFileInfo(save_path);
    QString path = info.absoluteFilePath();
    xml.XMLFile_creat_new_project(path.toStdString().c_str(),QString("%1.insar").arg(name).toStdString().c_str(), "1.0");

}

void TreeView::updateTreeIcons(const QString &theme)
{
    m_cachedTheme = theme;
    if (!model) return;
    bool isDark = theme == "dark";
    QColor iconColor = themeIconColor(isDark);
    QColor selectedColor = isDark ? QColor(0, 95, 172) : QColor(255, 255, 255);

    std::function<void(QStandardItem*)> updateItem = [&](QStandardItem *item) {
        if (!item) return;
        QString statusTip = item->statusTip();
        bool isProject = !statusTip.isEmpty();
        bool hasChildren = item->hasChildren();

        if (isProject) {
            item->setIcon(createColoredIcon(PROJECT_ICON, iconColor, selectedColor));
        } else if (hasChildren) {
            item->setIcon(createColoredIcon(FOLDER_ICON, iconColor, selectedColor));
        } else {
            item->setIcon(createColoredIcon(IMAGEDATA_ICON, iconColor, selectedColor));
        }

        for (int c = 0; c < item->rowCount(); ++c) {
            updateItem(item->child(c, 0));
        }
    };

    for (int row = 0; row < model->rowCount(); ++row) {
        updateItem(model->item(row, 0));
    }
}


void TreeView::mouseDoubleClickEvent(QMouseEvent * event)
{
    QModelIndex index = currentIndex();
    QStandardItem* item = model->itemFromIndex(index);
    if (item && !item->hasChildren()) {
        if (event->button() == Qt::LeftButton) {
            emit sendindex(index);
        }
    } else {
        if (event->button() == Qt::LeftButton) {
            setExpanded(index, !isExpanded(index));
            event->accept();
        }
    }
}

void TreeView::slotCustomContextMenu(const QPoint& point) //槽函数定义
{
    if (type == 1)
    {
        QModelIndex index = this->indexAt(point);  // 获取鼠标位置下的项
        QStandardItem* item = model->itemFromIndex(index);

        // 检查项是否有效
        if (!item || !index.isValid())
            return;

        QModelIndex parentIndex = index.parent();

        if (!parentIndex.isValid())
        {
            QMenu* menu = new QMenu(this);
            QAction* unload = new QAction(QStringLiteral("卸载工程"));
            QAction* cleanOrphaned = new QAction(QStringLiteral("清除孤立文件"));
            menu->addAction(unload);
            menu->addAction(cleanOrphaned);
            connect(unload, SIGNAL(triggered()), this, SLOT(Unload()));
            connect(cleanOrphaned, SIGNAL(triggered()), this, SLOT(CleanOrphanedFiles()));
            menu->exec(this->mapToGlobal(point));
        }
        else if (!parentIndex.parent().isValid())
        {
            QMenu* menu = new QMenu(this);
            QAction* node_delete = new QAction(QStringLiteral("删除节点"));
            node_delete->setIcon(QIcon(EXPORT_ICON));
            menu->addAction(node_delete);
            connect(node_delete, &QAction::triggered, this, &TreeView::DeleteNode);
            menu->exec(this->mapToGlobal(point));
        }
        else
        {
            QMenu* menu = new QMenu(this);
            QAction* image_saveas = new QAction(QStringLiteral("另存为"));
            QAction* image_delete = new QAction(QStringLiteral("删除"));
            image_saveas->setIcon(QIcon(EXPORT_ICON));
            image_delete->setIcon(QIcon(EXPORT_ICON));
            menu->addAction(image_saveas);
            menu->addAction(image_delete);
            connect(image_saveas, &QAction::triggered, this, &TreeView::Import);
            connect(image_delete, &QAction::triggered, this, &TreeView::Delete);
            menu->exec(this->mapToGlobal(point));
        }

    }
}

void TreeView::Delete()
{
    QModelIndex imageIndex = this->currentIndex();

    if (!imageIndex.isValid() ||
        !imageIndex.parent().isValid() ||
        !imageIndex.parent().parent().isValid())
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("请选择具体图像数据删除！"));
        return;
    }

   
    if (this->currentIndex().isValid())
    {
        
        QString Project_path = model->itemFromIndex(this->currentIndex().parent().parent().sibling(0, 1))->text();
        QString Project_name = model->itemFromIndex(this->currentIndex().parent().parent())->text();
        QString DataNode_name = model->itemFromIndex(this->currentIndex().parent())->text();
        QModelIndex NameIndex = this->currentIndex();
        QModelIndex PathIndex = NameIndex.sibling(0, 1);
        QString path = model->itemFromIndex(PathIndex)->text();
        QString name = model->itemFromIndex(NameIndex)->text();
        XMLFile xml;
        xml.XMLFile_load((Project_path+"/"+ Project_name).toStdString().c_str());
        xml.XMLFile_remove_node(DataNode_name.toStdString().c_str(), name.toStdString().c_str(), path.toStdString().c_str());
        xml.XMLFile_save((Project_path + "/" + Project_name).toStdString().c_str());
        if (QFile::exists(path))
        {
            QFile::remove(path);
        }

        model->removeRow(this->currentIndex().row(), this->currentIndex().parent());
        emit update();
    }
    else
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("该数据正在处理中，无法删除！"));
    }
}

void TreeView::Unload()
{
    if (this->currentIndex().isValid())
    {
        model->removeRow(model->itemFromIndex(this->currentIndex())->row());
        emit update();
    }
    else
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("该工程正在处理中，无法卸载！"));
    }
}

void TreeView::CleanOrphanedFiles()
{
    QModelIndex projIndex = this->currentIndex();
    if (!projIndex.isValid()) {
        projIndex = model->index(0, 0);
        if (!projIndex.isValid()) {
            QMessageBox::warning(nullptr, QStringLiteral("提示"), QStringLiteral("没有打开的工程。"));
            return;
        }
    } else {
        while (projIndex.parent().isValid()) {
            projIndex = projIndex.parent();
        }
    }
    
    QStandardItem* projItem = model->itemFromIndex(projIndex);
    if (!projItem) return;
    
    QString projectName = projItem->text();
    QModelIndex pathIndex = projIndex.sibling(projIndex.row(), 1);
    QStandardItem* pathItem = model->itemFromIndex(pathIndex);
    if (!pathItem) return;
    QString projectPath = pathItem->text();
    
    // Get all active node folder names from the project tree
    QStringList activeNodeNames;
    QSet<QString> activeFiles;
    QSet<QString> activeNodeItemKeys;
    for (int i = 0; i < projItem->rowCount(); ++i) {
        QStandardItem* nodeItem = projItem->child(i, 0);
        if (nodeItem) {
            QString nodeName = nodeItem->text();
            activeNodeNames.append(nodeName);
            for (int j = 0; j < nodeItem->rowCount(); ++j) {
                QStandardItem* nameItem = nodeItem->child(j, 0);
                QStandardItem* pathItem = nodeItem->child(j, 1);
                if (nameItem) {
                    activeNodeItemKeys.insert(nodeName + "/" + nameItem->text());
                }
                if (pathItem && !pathItem->text().isEmpty()) {
                    activeFiles.insert(QDir::cleanPath(pathItem->text()));
                }
            }
        }
    }
    
    QDir rootDir(projectPath);
    if (!rootDir.exists()) {
        return;
    }
    
    QStringList allDirs = rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    QStringList orphanedDirs;
    QStringList orphanedFiles;
    
    for (const QString& dirName : allDirs) {
        if (dirName == "temp" || dirName == ".temp" || dirName == "logs") {
            continue;
        }
        if (!activeNodeNames.contains(dirName)) {
            orphanedDirs.append(dirName);
        } else {
            QDir activeDir(projectPath + "/" + dirName);
            QStringList filesInDir = activeDir.entryList(QDir::Files | QDir::NoDotAndDotDot);
            for (const QString& fileName : filesInDir) {
                QString absFilePath = QDir::cleanPath(activeDir.absoluteFilePath(fileName));
                if (!activeFiles.contains(absFilePath)) {
                    // Check if it is a preview JPG whose corresponding tree item is active
                    if (absFilePath.endsWith(".jpg", Qt::CaseInsensitive)) {
                        QString baseJpgName = QFileInfo(absFilePath).baseName();
                        QString itemKey = dirName + "/" + baseJpgName;
                        if (activeNodeItemKeys.contains(itemKey)) {
                            continue; // Valid preview file, keep it!
                        }
                    }
                    orphanedFiles.append(absFilePath);
                }
            }
        }
    }
    
    if (orphanedDirs.isEmpty() && orphanedFiles.isEmpty()) {
        QMessageBox::information(nullptr, QStringLiteral("提示"), QStringLiteral("未发现孤立文件夹或孤立文件。"));
        return;
    }
    
    QString msg = QStringLiteral("发现以下孤立项：\n");
    if (!orphanedDirs.isEmpty()) {
        msg += QStringLiteral("【孤立文件夹】\n");
        for (const QString& dirName : orphanedDirs) {
            msg += "- " + dirName + "\n";
        }
    }
    if (!orphanedFiles.isEmpty()) {
        msg += QStringLiteral("【孤立文件】\n");
        for (const QString& filePath : orphanedFiles) {
            msg += "- " + QFileInfo(filePath).fileName() + QStringLiteral(" (位于节点 ") + QFileInfo(QFileInfo(filePath).path()).fileName() + ")\n";
        }
    }
    msg += QStringLiteral("\n是否确认删除它们？(此操作不可逆)");
    
    auto reply = QMessageBox::question(nullptr, QStringLiteral("确认删除"), msg, QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        for (const QString& dirName : orphanedDirs) {
            QDir dir(projectPath + "/" + dirName);
            dir.removeRecursively();
        }
        for (const QString& filePath : orphanedFiles) {
            QFile::remove(filePath);
        }
        QMessageBox::information(nullptr, QStringLiteral("完成"), QStringLiteral("孤立项已清理完毕。"));
    }
}

void TreeView::Import()
{
    if (this->currentIndex().isValid())
    {
        QModelIndex NameIndex = this->currentIndex();
        QModelIndex PathIndex = NameIndex.sibling(0, 1);
        QString path = model->itemFromIndex(PathIndex)->text();
        QString name = model->itemFromIndex(NameIndex)->text();
        QString Imagerank = model->itemFromIndex(NameIndex)->toolTip();
        if (!path.isEmpty())
        {
            QString dirname = QFileDialog::getSaveFileName(this,
                QStringLiteral("图像另存为"),
                "/",
                "*.jpg");
            if (!dirname.isEmpty())
            {
                if (mTreeProcess) {
                    mTreeProcess->close();
                    mTreeProcess->deleteLater();
                }
                mTreeProcess = new QProgressDialog("Saving Image...", nullptr, 0, 0, this);
                mTreeProcess->setFixedSize(450, 100);
                mTreeProcess->setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
                mTreeProcess->setWindowTitle(QStringLiteral("保存进度"));
                mTreeProcess->setValue(0);
                mTreeProcess->show();

                QFutureWatcher<bool>* watcher = new QFutureWatcher<bool>(this);
                watcher->setProperty("dirname", dirname);
                watcher->setProperty("progress", QVariant::fromValue(static_cast<void*>(mTreeProcess)));
                connect(watcher, &QFutureWatcher<bool>::finished, this, &TreeView::onSaveImageFinished);

                QFuture<bool> future = QtConcurrent::run(NodeUtils::generateJpgPreviewFromH5, path, dirname, Imagerank);
                watcher->setFuture(future);
            }
        }
    }
}

void TreeView::onSaveImageFinished()
{
    QFutureWatcher<bool>* watcher = static_cast<QFutureWatcher<bool>*>(sender());
    if (!watcher) return;
    
    bool success = watcher->result();
    QString dirname = watcher->property("dirname").toString();
    QProgressDialog* progress = static_cast<QProgressDialog*>(watcher->property("progress").value<void*>());
    
    if (progress) {
        progress->setValue(100);
        progress->deleteLater();
        if (mTreeProcess == progress) {
            mTreeProcess = nullptr;
        }
    }
    watcher->deleteLater();
    
    if (!success) {
        QFile::remove(dirname);
        QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("图像保存失败！"));
    } else {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("图像保存成功！"));
    }
}


static bool RemoveDataNodeFromProjectXml(const QString& projectFile, const QString& dataNodeName)
{
    QByteArray xmlPath = QFile::encodeName(projectFile);
    TiXmlDocument doc(xmlPath.constData());

    if (!doc.LoadFile())
        return false;

    TiXmlElement* root = doc.RootElement();
    if (!root)
        return false;

    TiXmlElement* node = root->FirstChildElement("DataNode");
    while (node)
    {
        TiXmlElement* next = node->NextSiblingElement("DataNode");
        const char* name = node->Attribute("name");

        if (name && dataNodeName == QString::fromUtf8(name))
        {
            root->RemoveChild(node);
            return doc.SaveFile();
        }

        node = next;
    }

    return true;
}
void TreeView::DeleteNode()
{
    QModelIndex nodeIndex = this->currentIndex();

    if (!nodeIndex.isValid() || !nodeIndex.parent().isValid() || nodeIndex.parent().parent().isValid())
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("该节点无法删除！"));
        return;
    }

    QStandardItem* node = model->itemFromIndex(nodeIndex);
    if (!node)
    {
        QMessageBox::warning(NULL, "Warning!", QStringLiteral("该节点无法删除！"));
        return;
    }

    QString Project_path = model->itemFromIndex(nodeIndex.parent().sibling(0, 1))->text();
    QString Project_name = model->itemFromIndex(nodeIndex.parent())->text();
    QString DataNode_name = node->text();
    QString projectFile = Project_path + "/" + Project_name;

    RemoveDataNodeFromProjectXml(projectFile, DataNode_name);

    QDir dir(Project_path + "/" + DataNode_name);
    if (dir.exists())
    {
        dir.removeRecursively();
    }

    model->removeRow(nodeIndex.row(), nodeIndex.parent());
    emit update();
}

void TreeView::rowsInserted(const QModelIndex &parent, int start, int end)
{
    QTreeView::rowsInserted(parent, start, end);

    // Only colorize newly inserted items, not the entire tree
    if (m_cachedTheme.isEmpty()) {
        QSettings settings("Config.ini", QSettings::IniFormat);
        m_cachedTheme = settings.value("Appearance/Theme", "light").toString();
    }
    bool isDark = m_cachedTheme == "dark";
    QColor iconColor = themeIconColor(isDark);
    QColor selectedColor = isDark ? QColor(0, 95, 172) : QColor(255, 255, 255);

    QStandardItem* parentItem = model->itemFromIndex(parent);
    if (!parentItem) return;

    for (int row = start; row <= end; ++row) {
        QStandardItem* item = parentItem->child(row, 0);
        if (!item) continue;

        QString statusTip = item->statusTip();
        bool isProject = !statusTip.isEmpty();
        bool hasChildren = item->hasChildren();

        if (isProject) {
            item->setIcon(createColoredIcon(PROJECT_ICON, iconColor, selectedColor));
        } else if (hasChildren) {
            item->setIcon(createColoredIcon(FOLDER_ICON, iconColor, selectedColor));
        } else {
            item->setIcon(createColoredIcon(IMAGEDATA_ICON, iconColor, selectedColor));
        }
    }
}
