#pragma once
#include <QtWidgets/QMainWindow>
#include<qstandarditemmodel.h>
#include "ui_Geocoding.h"
#include"MyThread.h"

class Geocoding : public QWidget
{
    Q_OBJECT
public:
    explicit Geocoding(QWidget* parent = Q_NULLPTR);
    ~Geocoding();
public slots:
    void ShowProjectList(QStandardItemModel*);
    void updateProcess(int, QString);
    void endProcess();
    void endThread();
    void StopThread();
    void TransitModel(QStandardItemModel*);
private:
    Ui::Geocoding* ui;
    QStandardItemModel* copy;
    MyThread* Geocoding_thread;
    QString save_path;
    QString projectFile;
    int image_number;
    void ChangeVision(bool Editable);
signals:
    void operate(int, int, int, QString, QString, QString, QStandardItemModel*);
    void sendCopy(QStandardItemModel*);
private slots:
    /*工程选择按钮响应函数*/
    void on_comboBox_project1_currentIndexChanged();
    /*工程选择按钮响应函数*/
    void on_comboBox_project2_currentIndexChanged();
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();
    void on_buttonBox_2_accepted();
    void on_buttonBox_2_rejected();
};