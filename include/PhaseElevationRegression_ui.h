#pragma once
#include <QtWidgets/QWidget>
#include <QStandardItemModel>
#include "ui_PhaseElevationRegression.h"
#include "PhaseElevationRegressionWorker.h"
#include "NodeUtils.h"

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
                 QString outputDirectory, QStringList phaseNames, QStringList phasePaths);
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
    QStringList m_generatedOutputNames;
    QStringList m_generatedOutputPaths;
    QList<int> m_generatedOffsetRows;
    QList<int> m_generatedOffsetCols;
    QStringList m_preparedOutputPaths;
    NodeUtils::OutputTransaction m_outputTransaction;

    void ChangeVision(bool Editable);
    bool persistGeneratedOutputs(QString* errorMessage = nullptr);
    void onWorkerError(const QString& error);
};
