#pragma once
#include <QtWidgets/QMainWindow>
#include<qstandarditemmodel.h>
#include "ui_S1_swath_merge.h"
#include"MyThread.h"

class S1_swath_merge : public QWidget
{
    Q_OBJECT
public:
    explicit S1_swath_merge(QWidget* parent = Q_NULLPTR);
    ~S1_swath_merge();
public slots:
    void ShowProjectList(QStandardItemModel*);
    void updateProcess(int, QString);
    void endProcess();
    void errorProcess(QString error_msg);
    void endThread();
    void StopThread();
    void TransitModel(QStandardItemModel*);
private:
    Ui::S1_swath_merge* ui;
    QStandardItemModel* copy;
    MyThread* S1_swath_merge_thread;
    QString save_path;
    QString projectFile;
    int image_number;
    void ChangeVision(bool Editable);
signals:
    void operate(int index1, int index2, int index3, QString project, QString node1, QString node2, QString node3, QString dstNode, QStandardItemModel*);
    void sendCopy(QStandardItemModel*);
private slots:
    /*工程选择按钮响应函数*/
    void on_comboBox_project_currentIndexChanged();
    void on_comboBox_IW1_node_currentIndexChanged();
    void on_comboBox_IW2_node_currentIndexChanged();
    void on_comboBox_IW3_node_currentIndexChanged();
    /*工程选择按钮响应函数*/
    //void on_comboBox_project2_currentIndexChanged();
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();
    //void on_buttonBox_2_accepted();
    //void on_buttonBox_2_rejected();
};