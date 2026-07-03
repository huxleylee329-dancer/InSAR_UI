#pragma once
#include <QtWidgets/QWidget>
#include <QStandardItemModel>
#include "ui_PhaseElevationRegression.h"
#include "PhaseElevationRegressionWorker.h"

class PhaseElevationRegression_ui : public QWidget
{
    Q_OBJECT
public:
    explicit PhaseElevationRegression_ui(QWidget* parent = nullptr);
    ~PhaseElevationRegression_ui();

public slots:
    void ShowProjectList(QStandardItemModel* model);
    void updateProcess(int value, QString information);
    void endProcess();
    void StopThread();
    void TransitModel(QStandardItemModel* model);

signals:
    void operate(int polyOrder, int windowSize, double coherenceThresh,
                 QString save_path, QString project_name,
                 QString node_name, QString file_name,
                 QStandardItemModel* model);
    void sendCopy(QStandardItemModel* model);

private slots:
    void on_comboBox_currentIndexChanged();
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();

private:
    Ui::PhaseElevationRegression* ui;
    QStandardItemModel* copy;
    PhaseElevationRegressionWorker* m_worker;
    QString save_path;

    void ChangeVision(bool Editable);
};
