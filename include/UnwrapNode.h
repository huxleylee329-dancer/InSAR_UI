#ifndef UNWRAPNODE_H
#define UNWRAPNODE_H

#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include "UnwrapWorker.h"
#include "NodeUtils.h"
#include <QtNodes/internal/ExecutableNodeDelegateModel.hpp>
#include <QtNodes/NodeData>
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QThread>
#include <QStandardItemModel>
#include <QFutureWatcher>
#include <QList>
#include <memory>

class IApplicationInterface;
class XMLFile;

namespace QtNodes {

class UnwrapNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    UnwrapNode();
    ~UnwrapNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("Phase Unwrapping"); }
    QString name() const override { return QStringLiteral("Phase Unwrapping"); }
    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    ProductInputContract productInputContract(PortIndex portIndex) const override;
    ProductOutputContract productOutputContract(PortIndex portIndex) const override;
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
    bool supportsValidation() const override { return true; }
    ::QWidget* createValidationWidget(::QWidget* parent) override;

protected:
    bool validateAndRestoreOutput() override;
    QStringList previewImagePaths() const override;
    bool prepareToStart() override;

private:
    ::QWidget* _widget;
    QComboBox* m_methodCombo;
    
    QLabel* m_coherenceLabel;
    QLineEdit* m_coherenceEdit;
    
    QLineEdit* m_outputNodeNameEdit;
    QGroupBox* m_snaphuOptionsGroup;
    QLabel* m_snaphuStatusLabel;
    QSpinBox* m_snaphuTileRowsSpin;
    QSpinBox* m_snaphuTileColsSpin;
    QSpinBox* m_snaphuRowOverlapSpin;
    QSpinBox* m_snaphuColOverlapSpin;
    QSpinBox* m_snaphuTimeoutSpin;
    QCheckBox* m_snaphuKeepArtifactsCheck;
    QComboBox* m_snaphuCostModeCombo;

    // Input/output data
    std::shared_ptr<ImportedFileData> m_inputData;
    std::shared_ptr<ImportedFileData> m_outputData;
    std::shared_ptr<ImageInfoData> m_imageInfoData;
    
    // Parameters
    QString m_outputNodeName;
    int m_method; // 1: SPD Guided, 2: MCF, 3: Snaphu, 4: Quality Guided MCF
    double m_coherenceThreshold;
    SnaphuUiOptions m_snaphuOptions;

    // Worker thread
    UnwrapWorker* m_workerThread;
    QThread* m_thread;

    // Remedy watcher for missing JPG regeneration
    QFutureWatcher<void> m_remedyWatcher;

    NodeUtils::OverwriteResult m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;
    QString m_preparedDstNode;
    QString m_preparedSavePath;
    QString m_preparedProjectName;
    QString m_preparedSrcNode;
    QStringList m_preparedPhasePaths;
    QStringList m_preparedOutputPaths;
    int m_preparedMethod = 1;
    double m_preparedThreshold = 0.3;
    SnaphuUiOptions m_preparedSnaphuOptions;
    NodeUtils::OutputTransaction m_outputTransaction;
    QList<UnwrapFileResult> m_pendingUnwrapResults;
    QString m_pendingWarningMessage;
    bool m_xmlDirty = false;
    quint64 m_snaphuLastUiHeartbeatMilliseconds;
    quint64 m_snaphuLastLogHeartbeatMilliseconds;

    // Helper methods
    void createWidget();
    void onProgressUpdate(int progress, const QString& message);
    void onProcessingFinished();
    void onCancelled();
    void onError(const QString& error);
    void onUnwrapFileGenerated(const UnwrapFileResult& result);
    void onModelUpdated(QStandardItemModel* model);
    bool validateInputs() const;
    void updateWidgetSize();
    void onMethodChanged(int index);
    void updateSnaphuOptionWidgets();
    void onSnaphuRunEvent(const SnaphuRunEventInfo& event);
    QString generateDefaultOutputName() const;
    void executeProcessing();
    void cleanUpThreadAndWorker();
    void commitUnwrapResult(const UnwrapFileResult& result);
    void publishUnwrapResultToProjectTree(const UnwrapFileResult& result);
    void startPreviewGeneration(const QStringList& h5Paths, const QStringList& generatedJpgPaths,
                                const QStringList& types, const QStringList& resultH5Paths, const QStringList& resultJpgPaths,
                                bool completeExecution);

    // Context helpers
    QStandardItemModel* projectModel() const;
    QString projectPath() const;
    QString projectName() const;
    XMLFile* projectXml() const;
    QString committedAmplitudeWarningMessage(const QStringList& h5Paths) const;

    // Executable interface implementation
    void execute() override;
    void stopExecution() override;
    void processAutomatically() override;
    bool stopExecutionIsAsynchronous() const override { return true; }
    bool supportsAutomaticRestartAfterInputChange() const override { return true; }

signals:
    void startUnwrap(int method, double coherenceThreshold, QString savePath, QString fileName,
                     QStringList phasePaths, SnaphuUiOptions snaphuOptions);
};

} // namespace QtNodes

#endif // UNWRAPNODE_H
