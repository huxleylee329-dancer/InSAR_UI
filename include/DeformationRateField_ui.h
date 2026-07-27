#pragma once

#include <QWidget>
#include <QStandardItemModel>
#include "DeformationRateFieldWorker.h"

namespace Ui {
class DeformationRateField;
}

class DeformationRateField_ui : public QWidget
{
    Q_OBJECT
public:
    explicit DeformationRateField_ui(QWidget* parent = nullptr);
    ~DeformationRateField_ui();

public slots:
    void ShowProjectList(QStandardItemModel* model);
    void updateProcess(int progress, QString message);
    void endProcess();
    void StopThread();
    void TransitModel(QStandardItemModel* model);
    void handleResults(const QString& dstNode, const QString& outputH5Path);

signals:
    void operate(
        QString  projectPath,
        QString  projectName,
        QString  dstNode,
        QStringList filePaths,
        int      modelType,
        double   confidenceLevel,
        double   coherenceThresholdHigh,
        double   coherenceThresholdMid,
        double   uncertaintyThresholdHigh,
        double   uncertaintyThresholdMid,
        int      colorMap,
        bool     showContour,
        int      contourInterval,
        bool     showArrow,
        int      arrowSpacing
    );
    void sendCopy(QStandardItemModel* model);

private slots:
    void on_comboBox_project_currentIndexChanged();
    void on_buttonBox_accepted();
    void on_buttonBox_rejected();

private:
    Ui::DeformationRateField* ui;
    QStandardItemModel* copy;
    DeformationRateFieldWorker* m_worker;
    QThread* m_thread;

    QString save_path;
    QString m_activeProjectName;
    QString m_activeProjectPath;

    void updateSrcNodeCombo();
    void setControlsEnabled(bool enabled);
    QStringList getSelectedFilePaths();
};
