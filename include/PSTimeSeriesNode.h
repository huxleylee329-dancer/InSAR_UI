#pragma once

#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include "PSTimeSeriesWorker.h"
#include <QtNodes/internal/ExecutableNodeDelegateModel.hpp>
#include <QtNodes/NodeData>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QThread>
#include <QJsonObject>
#include <memory>

namespace QtNodes {

class PSTimeSeriesNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    PSTimeSeriesNode();
    ~PSTimeSeriesNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("PS Time Series Analysis"); }
    QString name() const override { return QStringLiteral("PSTimeSeries"); }
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
    void execute() override;
    void stopExecution() override;
    void processAutomatically() override;

    QString projectPath() const;
    QString projectName() const;

private:
    // UI elements
    ::QWidget* _widget;
    QLabel* m_inputNodeLabel;
    QLineEdit* m_coherenceThreshEdit;
    QLineEdit* m_maxDeformationRateEdit;
    QLineEdit* m_atmosphericWindowEdit;
    QLineEdit* m_outputNodeNameEdit;
    QLabel* m_resultLabel;

    // Input/output data
    std::shared_ptr<ImportedFileData> m_inputData;
    std::shared_ptr<ImportedFileData> m_outputData; // H5 output
    std::shared_ptr<ImageInfoData> m_previewData;  // JPG preview output

    // Parameters
    double m_coherenceThresh;
    double m_maxDeformationRate;
    int m_atmosphericWindow;
    QString m_outputNodeName;

    // Worker thread
    PSTimeSeriesWorker* m_worker;
    QThread* m_thread;

    // Helper methods
    void createWidget();
    void onProgressUpdate(int progress, const QString& message);
    void onProcessingFinished();
    void onError(const QString& error);
    bool validateInputs() const;
    void updateLabels();
    void updateWidgetSize();
    void executeProcessing();
    void generateStaticPreviewJpg();
};

} // namespace QtNodes
