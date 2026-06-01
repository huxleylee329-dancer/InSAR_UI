#pragma once
#include <QtWidgets/QMainWindow>
#include<qstandarditemmodel.h>
#include "ui_S1FrameMerge.h"

class S1FrameMergeWorker;

class S1_frame_merge : public QWidget
{
    Q_OBJECT
public:
    explicit S1_frame_merge(QWidget* parent = Q_NULLPTR);
    ~S1_frame_merge();
public slots:
    void ShowProjectList(QStandardItemModel*);
    void updateProcess(int, QString);
    void endProcess();
    void errorProcess(QString error_msg);
    void endThread();
    void StopThread();
    void TransitModel(QStandardItemModel*);
private:
    Ui::S1FrameMerge* ui;
    QStandardItemModel* copy;
    S1FrameMergeWorker* S1_frame_merge_worker;
    QString save_path;
    QString projectFile;
    int image_number;
    void ChangeVision(bool Editable);
signals:
    void operate(int index1, int index2, QString project, QString node1, QString node2, QString dstNode, QStandardItemModel*);
    void sendCopy(QStandardItemModel*);
private slots:
    /*工程选择按鈕响应函数*/
    void on_comboBox_project_currentIndexChanged();
    void on_comboBox_node1_currentIndexChanged();
    void on_comboBox_node2_currentIndexChanged();
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();
    // 接收 Worker 的 sendResult 信号，完成 Workspace UI 路径的 XML 写入
    void handleResult(const QString& dstNode, const QString& filename, const QString& savePath, const QString& projectName);
};