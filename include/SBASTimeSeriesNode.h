#ifndef SBASTIMESERIESNODE_H
#define SBASTIMESERIESNODE_H

#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include "NodeUtils.h"
#include "SBASTimeSeriesWorker.h"
#include <QtNodes/internal/ExecutableNodeDelegateModel.hpp>
#include <QtNodes/NodeData>
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QThread>
#include <QPointer>
#include <QList>
#include <QJsonObject>
#include <QJsonArray>
#include <memory>

namespace QtNodes {

class SBASTimeSeriesNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    SBASTimeSeriesNode();
    ~SBASTimeSeriesNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("SBAS Time Series Analysis"); }
    QString name() const override { return QStringLiteral("SBASTimeSeries"); }
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
    bool stopExecutionIsAsynchronous() const override { return true; }
    bool supportsAutomaticRestartAfterInputChange() const override { return true; }
    bool prepareToStart() override;
    bool validateAndRestoreOutput() override;
    QStringList previewImagePaths() const override;

private:
    // UI elements
    ::QWidget* _widget;
    QLabel* m_inputNodeLabel;
    QLineEdit* m_spatialThreshEdit;
    QLineEdit* m_temporalThreshEdit;
    QLineEdit* m_temporalThreshLowEdit;
    QLineEdit* m_multilookRgEdit;
    QLineEdit* m_multilookAzEdit;
    QComboBox* m_unwrapMethodCombo;
    QLineEdit* m_alphaEdit;
    QLineEdit* m_coherenceThreshEdit;
    QLineEdit* m_temporalCoherenceThreshEdit;
    QLineEdit* m_refinementCohThreshEdit;
    QLineEdit* m_refinementDefThreshEdit;
    QLineEdit* m_outputNodeNameEdit;
    QLabel* m_resultLabel;

    // Input/output data
    std::shared_ptr<ImportedFileData> m_inputData;
    std::shared_ptr<ImportedFileData> m_outputData; // H5 output
    std::shared_ptr<ImageInfoData> m_previewData;  // JPG preview output

    // Parameters
    double m_temporalThreshLow;
    double m_temporalThresh;
    double m_spatialThresh;
    int m_multilookRg;
    int m_multilookAz;
    int m_unwrapMethod; // 1: Delaunay_MCF, 2: SNAPHU, 3: MCF
    double m_alpha;
    double m_coherenceThresh;
    double m_temporalCoherenceThresh;
    double m_refinementCohThresh;
    double m_refinementDefThresh;
    QString m_outputNodeName;
    QString m_preparedProjectRoot;
    QString m_preparedProjectName;
    QStringList m_preparedInputPaths;
    QStringList m_preparedOutputPaths;
    SBASTimeSeriesResult m_pendingResult;
    NodeUtils::OutputTransaction m_outputTransaction;
    quint64 m_executionGeneration = 0;

    // Worker thread
    SBASTimeSeriesWorker* m_worker;
    QThread* m_thread;
    struct TrackedExecution {
        QPointer<SBASTimeSeriesWorker> worker;
        QPointer<QThread> thread;
    };
    QList<TrackedExecution> m_trackedExecutions;

    // Helper methods
    void createWidget();
    void onProgressUpdate(int progress, const QString& message);
    void onProcessingFinished();
    void onError(const QString& error);
    void onCancelled();
    void onSbasGenerated(const SBASTimeSeriesResult& result);
    bool validateInputs() const;
    void updateLabels();
    void updateWidgetSize();
    void executeProcessing();
    bool generateStaticPreviewJpg(bool completeExecution = false);
    bool commitOutputTransaction(QString* errorMessage);
    void rollbackOutputTransaction(const QString& reason);
    void trackExecution(SBASTimeSeriesWorker* worker, QThread* thread);
    void stopAndWaitForTrackedExecutions();
    QString projectPath() const;
    QString projectName() const;

    // Executable interface implementation
    void execute() override;
    void stopExecution() override;
    void processAutomatically() override;

    // Prepared data for pre-execution lifecycle
    NodeUtils::OverwriteResult m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;
    bool m_xmlDirty = false;

signals:
    void startProcess();
};

} // namespace QtNodes

#endif // SBASTIMESERIESNODE_H
