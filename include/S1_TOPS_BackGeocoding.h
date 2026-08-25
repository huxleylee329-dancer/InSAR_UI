#pragma once
#include <QtWidgets/QMainWindow>
#include <QStringList>
#include<qstandarditemmodel.h>
#include <QPointer>
#include "ui_S1TopsBackGeocoding.h"
#include "S1TopsBackGeocodingWorker.h"

class S1_TOPS_BackGeocoding : public QWidget
{
    Q_OBJECT
public:
    explicit S1_TOPS_BackGeocoding(QWidget* parent = Q_NULLPTR);
    ~S1_TOPS_BackGeocoding();
public slots:
    void ShowProjectList(QStandardItemModel*);
    void updateProcess(int, QString);
    void endProcess();
    void onRegistrationFinished(
        const QStringList& regisH5Paths,
        const QString& dstNode,
        const QString& dstProject,
        const QString& savePath,
        int masterIndex
    );
    void endThread();
    void StopThread();
    void TransitModel(QStandardItemModel*);
private:
    Ui::S1TopsBackGeocoding* ui;
    QStandardItemModel* copy;
    QPointer<S1TopsBackGeocodingWorker> S1_TOPS_BackGeocoding_thread;
    QString save_path;
    QString projectFile;
    int image_number;
    void ChangeVision(bool Editable);
signals:
    void operate(int, QString, QString, QString, QStringList, bool);
    void sendCopy(QStandardItemModel*);
private slots:
    /*工程选择按钮响应函数*/
    void on_comboBox_currentIndexChanged();
    /*数据节点选择按钮响应函数*/
    void on_comboBox_2_currentIndexChanged();
    /*主图像选择按钮响应函数*/
    void on_comboBox_3_currentIndexChanged();
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();
};
