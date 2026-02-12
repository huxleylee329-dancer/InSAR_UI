#include "treeview.h"  
#include"icon_source.h"
#include<qmessagebox.h>
#include <QMenu>  
#include <QMenuBar>  
#include <QStatusBar> 
#include <QFileDialog>
#include<FormatConversion.h>
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
    model->setHeaderData(0, Qt::Horizontal, QString::fromLocal8Bit("template"));
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
        if (!model->itemFromIndex(this->currentIndex())->parent())//工程节点菜单栏
        {
            QMenu* menu = new QMenu(this);
            QAction* unload = new QAction(QString::fromLocal8Bit("卸载工程"));
            menu->addAction(unload);
            connect(unload, SIGNAL(triggered()), this, SLOT(Unload()));
            menu->exec(this->mapToGlobal(point));
        }
        //图像数据节点菜单栏
        else if (!model->itemFromIndex(this->currentIndex())->hasChildren() && model->itemFromIndex(this->currentIndex())->parent())
        {
            QMenu* menu = new QMenu(this);
            QAction* image_saveas = new QAction(QString::fromLocal8Bit("另存为"));
            QAction* image_delete = new QAction(QString::fromLocal8Bit("删除"));
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
    if (this->currentIndex().isValid())
    {
        QString Project_path = model->itemFromIndex(this->currentIndex().parent().parent().sibling(0, 1))->text();
        QString Project_name = model->itemFromIndex(this->currentIndex().parent().parent())->text();
        QString DataNode_name = model->itemFromIndex(this->currentIndex().parent())->text();
        QModelIndex NameIndex = this->currentIndex();
        QModelIndex PathIndex = NameIndex.sibling(0, 1);
        QString path = model->itemFromIndex(PathIndex)->text();
        QString name = model->itemFromIndex(NameIndex)->text();
        model->removeRow(this->currentIndex().row(), this->currentIndex().parent());
        XMLFile xml;
        xml.XMLFile_load((Project_path+"/"+ Project_name).toStdString().c_str());
        xml.XMLFile_remove_node(DataNode_name.toStdString().c_str(), name.toStdString().c_str(), path.toStdString().c_str());
        xml.XMLFile_save((Project_path + "/" + Project_name).toStdString().c_str());
        emit update();
    }
    else
    {
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该数据正在处理中，无法删除！"));
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
        QMessageBox::warning(NULL, "Warning!", QString::fromLocal8Bit("该工程正在处理中，无法卸载！"));
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
                QString::fromLocal8Bit("图像另存为"),
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
                mTreeProcess->setWindowTitle(QString::fromLocal8Bit("保存进度"));
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
