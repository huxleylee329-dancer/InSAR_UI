#pragma once
#include <QtWidgets/QMainWindow>
#include<qstandarditemmodel.h>
#include"ui_BaselineFormation.h"
#include "BaselineWorker.h"
#include <QPointer>

class Baseline_Formation : public QWidget
{
    Q_OBJECT
public:
    explicit Baseline_Formation(QWidget* parent = Q_NULLPTR);
    ~Baseline_Formation();
public slots:
    void ShowProjectList(QStandardItemModel*);
    void updateProcess(int, QString);
    void endProcess();
    void endThread();
    void StopThread();
    void Paint_Baseline(QList<double> temporal_baseline, QList<double> spatial_baseline, int index);
private:
    Ui::BaselineFormation* ui = nullptr;
    QStandardItemModel* copy = nullptr;
    QPointer<BaselineWorker> Baseline_Formation_thread;
    QString save_path;
    int method = 0;
    int image_number = 0;
    double temporal_thresh = 0.0;
    double temporal_thresh_low = 0.0;
    double spatial_thresh = 0.0;
signals:
    void operate(int, const QStringList&);
    void sendCopy(QStandardItemModel*);
    void sendBaseline(QList<double> temporal_baseline, QList<double> spatial_baseline, int index, double temporal_thresh, double temporal_thresh_low, double spatial_thresh);

private slots:
    void on_comboBox_currentIndexChanged();
    void on_comboBox_dst_node_currentIndexChanged();
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();
    void ChangeVision(bool Editable);
};
