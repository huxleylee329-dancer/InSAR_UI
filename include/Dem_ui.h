#pragma once
#include <QtWidgets/QMainWindow>
#include<qstandarditemmodel.h>
#include "ui_Dem_ui.h"
#include"MyThread.h"

class Dem_ui : public QWidget
{
    Q_OBJECT
public:
    explicit Dem_ui(QWidget* parent = Q_NULLPTR);
    ~Dem_ui();
public slots:
    void ShowProjectList(QStandardItemModel*);
    void updateProcess(int, QString);
    void endProcess();
    void endThread();
    void StopThread();
    void TransitModel(QStandardItemModel*);
private:
    Ui::Dem_ui* ui;
    QStandardItemModel* copy;
    MyThread* Dem_thread;
    QString save_path;
    int method;
    int image_number;
    void ChangeVision(bool Editable);
signals:
    void operate(int, int, QString, QString, QString, QString, QStandardItemModel*);
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