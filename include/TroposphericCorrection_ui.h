#pragma once
#include <QtWidgets/QWidget>
#include <QStandardItemModel>
#include "ui_TroposphericCorrection.h"
#include "TroposphericCorrectionWorker.h"

class TroposphericCorrection_ui : public QWidget
{
    Q_OBJECT
public:
    explicit TroposphericCorrection_ui(QWidget* parent = nullptr);
    ~TroposphericCorrection_ui();

public slots:
    void ShowProjectList(QStandardItemModel* model);
    void updateProcess(int value, QString information);
    void endProcess();
    void StopThread();
    void TransitModel(QStandardItemModel* model);

signals:
    void operate(QString era5Dir,
                 QString save_path, QString project_name,
                 QString node_name, QString file_name,
                 QStandardItemModel* model);
    void sendCopy(QStandardItemModel* model);

private slots:
    void on_comboBox_currentIndexChanged();
    void on_pushButton_era5dir_clicked();
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();

private:
    Ui::TroposphericCorrection* ui;
    QStandardItemModel* copy;
    TroposphericCorrectionWorker* m_worker;
    QString save_path;

    void ChangeVision(bool Editable);
};
