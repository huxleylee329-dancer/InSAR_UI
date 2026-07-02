#ifndef BASELINEFORMATIONNODE_H
#define BASELINEFORMATIONNODE_H

#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include "BaselineWorker.h"
#include <QtNodes/internal/ExecutableNodeDelegateModel.hpp>
#include <QtNodes/NodeData>
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QThread>
#include <QJsonObject>
#include <QJsonArray>
#include <memory>

namespace QtNodes {

class BaselineFormationNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    BaselineFormationNode();
    ~BaselineFormationNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("Baseline Estimation"); }
    QString name() const override { return QStringLiteral("BaselineFormation"); }
    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    bool portCaptionVisible(PortType portType, PortIndex portIndex) const override;
    QString portCaption(PortType portType, PortIndex portIndex) const override;
    bool portIsOptional(PortType portType, PortIndex portIndex) const override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;
    void setInData(std::shared_ptr<NodeData> data, PortIndex port) override;
    ::QWidget* embeddedWidget() override;

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

    // ExecutableNodeDelegateModel interface implementation
    void setExecutionMode(ExecutionMode mode) override;

protected:
    bool validateAndRestoreOutput() override;
    QStringList previewImagePaths() const override;

private:
    // UI elements
    ::QWidget* _widget;
    QLabel* m_inputNodeLabel;
    QCheckBox* m_defaultMasterCheckBox;
    QComboBox* m_masterImageCombo;
    QLineEdit* m_spatialThreshEdit;
    QLineEdit* m_temporalThreshEdit;
    QLineEdit* m_temporalThreshLowEdit;
    QLineEdit* m_outputNodeNameEdit;
    QPushButton* m_showChartBtn;
    QLabel* m_resultLabel;

    // Input/output data
    std::shared_ptr<ImportedFileData> m_inputData;
    std::shared_ptr<ImportedFileData> m_outputData; // pass-through

    // Parameters
    int m_masterIndex;
    bool m_useDefaultMaster;
    double m_spatialThresh;
    double m_temporalThresh;
    double m_temporalThreshLow;
    QString m_outputNodeName;
    
    QList<double> m_temporalBaselines;
    QList<double> m_spatialBaselines;

    // Worker thread
    BaselineWorker* m_worker;
    QThread* m_thread;

    // Helper methods
    void createWidget();
    void onProgressUpdate(int progress, const QString& message);
    void onProcessingFinished(QList<double> temporal_baseline, QList<double> spatial_baseline, int index);
    void onError(const QString& error);
    bool validateInputs() const;
    void updateLabels();
    void updateWidgetSize();
    void updateMasterImageCombo();
    void executeProcessing();
    void showChart();
    QString projectPath() const;

    // Executable interface implementation
    void execute() override;
    void stopExecution() override;
    void processAutomatically() override;

signals:
    void startEstimate(int index, QStringList filePaths);
};

} // namespace QtNodes

#endif // BASELINEFORMATIONNODE_H
