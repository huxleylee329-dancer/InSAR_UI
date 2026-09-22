#pragma once
#include <QtWidgets/QMainWindow>
#include<qstandarditemmodel.h>
#include "ui_S1SwathMerge.h"
#include "S1SwathMergeWorker.h"
#include <QThread>
#include <QPointer>

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
    void handleResult(const QString& dstNode, const QString& filename, const QString& mergedH5Path,
                      const QString& savePath, const QString& projectName);
private:
    Ui::S1SwathMerge* ui = nullptr;
    QStandardItemModel* copy = nullptr;
    QPointer<S1SwathMergeWorker> S1_swath_merge_worker;
    QPointer<QThread> S1_swath_merge_thread;
    QString save_path;
    QString projectFile;
    int image_number = 0;
    void ChangeVision(bool Editable);
signals:
    void operate(QString projectName, QString savePath, QString dstNode,
                 QString firstH5Path, QString secondH5Path, QString thirdH5Path);
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
