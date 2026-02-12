#pragma once
#include <QtWidgets/QMainWindow>
#include<qstandarditemmodel.h>
#include"ui_SLC_deramp.h"
#include"MyThread.h"

class SLC_deramp : public QWidget
{
    Q_OBJECT
public:
    explicit SLC_deramp(QWidget* parent = Q_NULLPTR);
    ~SLC_deramp();
public slots:
    void ShowProjectList(QStandardItemModel*);
    void updateProcess(int, QString);
    void endProcess();
    void endThread();
    void StopThread();
private:
    Ui::SLC_deramp* ui;
    QStandardItemModel* copy;
    MyThread* SLC_deramp_thread;
    QString save_path;
    int method;
    int image_number;
signals:
    void operate(int, QString, QString, QString, QStandardItemModel*);
    void sendCopy(QStandardItemModel*);
    void sendBaseline(QList<double> temporal_baseline, QList<double> spatial_baseline, int index);

private slots:
    void on_comboBox_currentIndexChanged();
    void on_comboBox_dst_node_currentIndexChanged();
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();
    void ChangeVision(bool Editable);
};