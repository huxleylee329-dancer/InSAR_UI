#pragma once
#include<Baseline_Preview.h>
#include <QtWidgets/QMainWindow>
#include<qstandarditemmodel.h>
#include "ui_Baseline.h"
#include"MyThread.h"

class Baseline : public QWidget
{
    Q_OBJECT
public:
    explicit Baseline(QWidget* parent = Q_NULLPTR);
    ~Baseline();
public slots:
    void ShowProjectList(QStandardItemModel*);
    void updateProcess(int, QString);
    void endProcess();
    void endThread();
    void StopThread();
    void Paint_Baseline(QList<double> temporal_baseline, QList<double> spatial_baseline, int index);
private:
    Ui::Baseline* ui;
    QStandardItemModel* copy;
    MyThread* Baseline_thread;
    QString save_path;
    int method;
    int image_number;
signals:
    void operate(int, QString, QString, QStandardItemModel*);
    void sendCopy(QStandardItemModel*);
    void sendBaseline(QList<double> temporal_baseline, QList<double> spatial_baseline, int index);
   
private slots:
    void on_comboBox_currentIndexChanged();
    void on_comboBox_dst_node_currentIndexChanged();
    // void on_masterpushButton_pressed();
     //void on_slavepushButton_pressed()
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();
    void ChangeVision(bool Editable);
};