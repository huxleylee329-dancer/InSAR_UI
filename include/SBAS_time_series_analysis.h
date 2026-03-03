#pragma once
#include <QtWidgets/QMainWindow>
#include<qstandarditemmodel.h>
#include"ui_SBAS_time_series_analysis.h"
#include"MyThread.h"

class SBAS_time_series_analysis : public QWidget
{
    Q_OBJECT
public:
    explicit SBAS_time_series_analysis(QWidget* parent = Q_NULLPTR);
    ~SBAS_time_series_analysis();
public slots:
    void ShowProjectList(QStandardItemModel*);
    void updateProcess(int, QString);
    void endProcess();
    void endThread();
    void StopThread();
    void TransitModel(QStandardItemModel* model);
private:
    Ui::SBAS_time_series_analysis* ui;
    QStandardItemModel* copy;
    MyThread* SBAS_time_series_analysis_thread;
    QString save_path;
    int method;//1：Delaunay_MCF，2：SNAPHU，3：MCF
    int image_number;
    double temporal_thresh;
    double spatial_thresh;
    
signals:
    void operate(double, double, double, int, int, int, double, double, double, double, double, QString, QString, QString, QString, QStandardItemModel*);
    void sendCopy(QStandardItemModel*);
    void sendBaseline(QList<double> temporal_baseline, QList<double> spatial_baseline, int index, double temporal_thresh, double spatial_thresh);

private slots:
    void on_comboBox_project_currentIndexChanged();
    void on_comboBox_srcNode_currentIndexChanged();
    void on_buttonbrowse_triggered();
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();
    void ChangeVision(bool Editable);
};