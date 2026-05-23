#include "treeview.h"
#include"icon_source.h"
#include "icon_utils.h"
#include<qmessagebox.h>
#include <QMenu>
#include <QMenuBar>  
#include <QStatusBar> 
#include <QFileDialog>
#include<QDebug>
#include<QDir>
#include<QFile>
#include<QFileInfo>
#include<QSet>
#include<FormatConversion.h>

// Icons now use SVG currentColor - automatically follows widget color property
// No manual tinting needed - theme colors are set via stylesheet

static bool isDarkTheme(QWidget *w)
{
    if (!w) return false;
    QColor bg = w->palette().color(w->backgroundRole());
    return bg.lightness() < 128;
}
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
    thread = NULL;
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
}

void TreeView::init_tree()
{
    model->setHeaderData(0, Qt::Horizontal, tr("workspace"));
    model->setHeaderData(1, Qt::Horizontal, tr("Path"));
    type = 1;
}

void TreeView::init_mould()
{
    model->setHeaderData(0, Qt::Horizontal, QString("template"));
    QStandardItem* InSAR = new QStandardItem("InSAR");
    InSAR->setIcon(QIcon(TEMPLATE_FOLDER));
    QStandardItem* DInSAR = new QStandardItem("DInSAR");
    DInSAR->setIcon(QIcon(TEMPLATE_FOLDER));
    model->appendRow(InSAR);
    model->appendRow(DInSAR);
    QStandardItem* ToDEM = new QStandardItem("DEM");
    ToDEM->setIcon(QIcon(TEMPLATE_TOOL));
    ToDEM->setToolTip("DEM");
    InSAR->appendRow(ToDEM);
    type = 2;
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
    if (!model) return;
    QColor iconColor = themeIconColor(theme == "dark");

    std::function<void(QStandardItem*)> updateItem = [&](QStandardItem *item) {
        if (!item) return;
        QString statusTip = item->statusTip();
        bool isProject = !statusTip.isEmpty();
        bool hasChildren = item->hasChildren();

        if (isProject) {
            item->setIcon(createColoredIcon(PROJECT_ICON, iconColor));
        } else if (hasChildren) {
            item->setIcon(createColoredIcon(FOLDER_ICON, iconColor));
        } else {
            item->setIcon(createColoredIcon(IMAGEDATA_ICON, iconColor));
        }

        for (int c = 0; c < item->rowCount(); ++c) {
            updateItem(item->child(c, 0));
        }
    };

    for (int row = 0; row < model->rowCount(); ++row) {
        updateItem(model->item(row, 0));
    }
}


 QList<QStandardItem*> TreeView::returnTheItems()
 {
    return model->findItems("*", Qt::MatchWildcard | Qt::MatchRecursive);
 }

void TreeView::iterateOverItems()
 {
    QList<QStandardItem*> list = returnTheItems();

     foreach(QStandardItem * item, list) {
         qDebug() << item->text();

	}
 }

void TreeView::mouseDoubleClickEvent(QMouseEvent * event)
{
    if (event->button() == Qt::LeftButton) {
        if (model->rowCount() >= 1)
        {
            emit sendindex(currentIndex());
        }
	}
    else
    {
        
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
    if (!this->currentIndex().isValid()) return;
    
    QStandardItem* projItem = model->itemFromIndex(this->currentIndex());
    if (!projItem) return;
    
    QString projectName = projItem->text();
    // Get project path from column 1 of the SAME row (not always row 0)
    QModelIndex projIndex = this->currentIndex();
    QModelIndex pathIndex = projIndex.sibling(projIndex.row(), 1);
    QStandardItem* pathItem = model->itemFromIndex(pathIndex);
    if (!pathItem) return;
    QString projectPath = pathItem->text();
    
    // Get all active node folder names from the project tree
    QStringList activeNodeNames;
    qDebug() << "[CleanOrphanedFiles] Project:" << projectName << "Path:" << projectPath;
    qDebug() << "[CleanOrphanedFiles] Active DataNodes in Tree:";
    for (int i = 0; i < projItem->rowCount(); ++i) {
        QStandardItem* nodeItem = projItem->child(i, 0);
        if (nodeItem) {
            activeNodeNames.append(nodeItem->text());
            qDebug() << "  - " << nodeItem->text();
        }
    }
    
    QDir rootDir(projectPath);
    if (!rootDir.exists()) {
        qDebug() << "[CleanOrphanedFiles] Root dir does not exist!";
        return;
    }
    
    QStringList allDirs = rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    QStringList orphanedDirs;
    
    qDebug() << "[CleanOrphanedFiles] Scanning disk directories:";
    for (const QString& dirName : allDirs) {
        if (dirName == "temp" || dirName == "logs") {
            qDebug() << "  - " << dirName << "(Skipped system dir)";
            continue;
        }
        if (!activeNodeNames.contains(dirName)) {
            orphanedDirs.append(dirName);
            qDebug() << "  - " << dirName << "(ORPHANED!)";
        } else {
            qDebug() << "  - " << dirName << "(Active)";
        }
    }
    
    if (orphanedDirs.isEmpty()) {
        QMessageBox::information(nullptr, QStringLiteral("提示"), QStringLiteral("未发现孤立文件夹。"));
        return;
    }
    
    QString msg = QStringLiteral("发现以下孤立文件夹：\n");
    for (const QString& dirName : orphanedDirs) {
        msg += "- " + dirName + "\n";
    }
    msg += QStringLiteral("\n是否确认删除它们？(此操作不可逆)");
    
    auto reply = QMessageBox::question(nullptr, QStringLiteral("确认删除"), msg, QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        for (const QString& dirName : orphanedDirs) {
            QDir dir(projectPath + "/" + dirName);
            dir.removeRecursively();
        }
        QMessageBox::information(nullptr, QStringLiteral("完成"), QStringLiteral("孤立文件夹已清理完毕。"));
    }
}

void TreeView::Import()
{
    if (this->currentIndex().isValid())
    {
        QModelIndex NameIndex = this->currentIndex();
        QModelIndex PathIndex = NameIndex.sibling(0, 1);
        //QModelIndex ParentIndex = NameIndex.parent();
        QString path = model->itemFromIndex(PathIndex)->text();
        QString name = model->itemFromIndex(NameIndex)->text();
        QString Imagerank = model->itemFromIndex(NameIndex)->toolTip();
        if (!path.isEmpty())
        {
            QString dirname = QFileDialog::getSaveFileName(this,
                QStringLiteral("图像另存为"),
                "/",
                "*.jpg");
            QFileInfo fileinfo = QFileInfo(dirname);
           // QString bmp = QString("%1/%2.jpg").arg(fileinfo.absolutePath()).arg(fileinfo.baseName());
           // QString path_abs = QString("%1%2%3%4").arg(fileinfo.absolutePath()).arg("/").arg(name).arg(".jpg");
            if (!dirname.isEmpty())
            {
                thread = new MyThread;
                thread->moveToThread(new QThread(this));
                mTreeProcess = new QProgressDialog("Loading Image...", "Cancel", 0, 100);
                mTreeProcess->setFixedSize(450, 100);
                mTreeProcess->setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
                mTreeProcess->setWindowTitle(QStringLiteral("保存进度"));
                mTreeProcess->setCancelButton(false);
                //this->Process->setAutoClose(true);
                mTreeProcess->setValue(0);
                mTreeProcess->show();
                cv::waitKey(100);
                connect(this, &TreeView::operate, thread, &MyThread::ShowImage);
                connect(thread, &MyThread::updateProcess, this, &TreeView::updateProcess);
                connect(thread->thread(), &QThread::finished, thread, &MyThread::deleteLater);
                connect(thread, &MyThread::endProcess, this, &TreeView::StopThread);
                thread->thread()->start();
                emit operate(path, dirname, Imagerank);
                //SaveImage(path, dirname, Imagerank);
                //connect(this, &TreeView::updateProcess_info, this, &TreeView::updateProcess);

            }

            
        }
    }
}

void TreeView::StopThread()
{
    mTreeProcess->setValue(100);
    cv::waitKey(100);
    if (!mTreeProcess)
    {
        delete(mTreeProcess);
        mTreeProcess = NULL;
    }
    thread->thread()->quit();
    thread->thread()->wait();

}

void TreeView::updateProcess(int value, QString information)
{
    if (mTreeProcess)
    {
        mTreeProcess->setValue(value);
        mTreeProcess->setLabelText(information);
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

        if (name && dataNodeName == QString::fromLocal8Bit(name))
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
