
#pragma once
#include <QtGui> 
#include<qtreeview.h>
#include<QtWidgets/qmainwindow.h>
#include"qprogressdialog.h"
#include<MyThread.h>

class TreeView :public QTreeView
{
    Q_OBJECT;
public:
     TreeView(QWidget* parent = Q_NULLPTR);
     void iterateOverItems();
     void NewProject(QString, QString);
     void init_tree();
     void init_mould();
     QList<QStandardItem*> returnTheItems();
     QStandardItemModel* model;
     QProgressDialog* mTreeProcess;
     void updateTreeIcons(const QString &theme);
     //QItemSelectionModel* modelSelection;
     //File_Path file_path[10];
     void mouseDoubleClickEvent(QMouseEvent* event);
public slots:
    void CleanOrphanedFiles();
    void slotCustomContextMenu(const QPoint&);

private:
    int num_pro;
    int type;//1：左上工程树，2：左下模板
    MyThread* thread;
    void updateProcess(int value, QString information);
signals:
    void sendindex(QModelIndex);
    /*更新treeview*/
    void operate(QString, QString, QString);
    void update();
    void updateProcess_info(int, QString);
private slots:
    void Import();
    void Delete();
    /*卸载工程响应函数*/
    void Unload();
    void StopThread();
    void DeleteNode();
 }; 
