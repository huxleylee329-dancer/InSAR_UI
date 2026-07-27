#pragma once
#include <QtWidgets/QMainWindow>
#include<qstandarditemmodel.h>
#include "ui_S1Deburst.h"
#include "S1DeburstWorker.h"
#include <QThread>
#include <QStringList>

class S1_Deburst : public QWidget
{
    Q_OBJECT
public:
    explicit S1_Deburst(QWidget* parent = Q_NULLPTR);
    ~S1_Deburst();
public slots:
    void ShowProjectList(QStandardItemModel*);
    void updateProcess(int, QString);
    void endProcess();
    void endThread();
    void StopThread();
    void TransitModel(QStandardItemModel*);
private:
    Ui::S1Deburst* ui;
    QStandardItemModel* copy;
    S1DeburstWorker* S1_Deburst_worker;
    QThread* m_thread;
    QString save_path;
    QString projectFile;
    int image_number;
    void ChangeVision(bool Editable);
signals:
    void operate(QString, QString, QString, QStringList);
    void sendCopy(QStandardItemModel*);
private slots:
    /*工程选择按钮响应函数*/
    void on_comboBox_currentIndexChanged();
    /*数据节点选择按钮响应函数*/
    void on_comboBox_2_currentIndexChanged();
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();
    void handleResults(const QString& dstNode, const QStringList& deburstH5Paths, const QStringList& originNames);
};
