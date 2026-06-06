
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

protected:
     void rowsInserted(const QModelIndex &parent, int start, int end) override;
     void drawBranches(QPainter *painter, const QRect &rect, const QModelIndex &index) const override;

private:
    int num_pro;
    int type;//1：左上工程树，2：左下模板
    QString m_cachedTheme;
signals:
    void sendindex(QModelIndex);
    /*更新treeview*/
    void update();
private slots:
    void Import();
    void Delete();
    /*卸载工程响应函数*/
    void Unload();
    void DeleteNode();
    void onSaveImageFinished();
 }; 
