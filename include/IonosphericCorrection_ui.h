#pragma once
#include <QtWidgets/QWidget>
#include <QStandardItemModel>
#include "ui_IonosphericCorrection.h"
#include "IonosphericCorrectionWorker.h"

class IonosphericCorrection_ui : public QWidget
{
    Q_OBJECT
public:
    explicit IonosphericCorrection_ui(QWidget* parent = nullptr);
    ~IonosphericCorrection_ui();

public slots:
    void ShowProjectList(QStandardItemModel* model);
    void updateProcess(int value, QString information);
    void endProcess();
    void StopThread();
    void TransitModel(QStandardItemModel* model);
    void onProcessingError(const QString& error);
    void onProcessingCancelled();

signals:
    void operate(double subbandRatio, double filterStrength, bool outputTEC,
                 QString save_path, QString project_name,
                 QString node_name, QString file_name,
                 QStringList slcNames, QStringList slcPaths);
    void sendCopy(QStandardItemModel* model);

private slots:
    void on_comboBox_currentIndexChanged();
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();

private:
    Ui::IonosphericCorrection* ui;
    QStandardItemModel* copy;
    IonosphericCorrectionWorker* m_worker;
    QString save_path;
    QStringList m_generatedOutputNames;
    QStringList m_generatedOutputPaths;

    void ChangeVision(bool Editable);
    void cleanUpWorker();
    void persistGeneratedOutputs();
};
