#pragma once
#include <QtWidgets/QMainWindow>
#include<qstandarditemmodel.h>
#include "ui_Filter_ui.h"
#include"MyThread.h"

class Filter_ui : public QWidget
{
    Q_OBJECT
public:
    explicit Filter_ui(QWidget* parent = Q_NULLPTR);
    ~Filter_ui();
public slots:
    void ShowProjectList(QStandardItemModel*);
    void updateProcess(int, QString);
    void endProcess();
    void endThread();
    void StopThread();
    void TransitModel(QStandardItemModel*);
private:
    Ui::Filter_ui* ui;
    QStandardItemModel* copy;
    MyThread* Filter_thread;
    QString save_path;
    int method;
    int image_number;
    void ChangeVision(bool Editable);
signals:
    void operate(QList<int>, double, QString, QString, QString, QString, QStandardItemModel*);
    void sendCopy(QStandardItemModel*);
private slots:
    void on_comboBox_currentIndexChanged();
    void on_comboBox_2_currentIndexChanged();
    // void on_masterpushButton_pressed();
     //void on_slavepushButton_pressed()
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();
    void Change_Setting();
};