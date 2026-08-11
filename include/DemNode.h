#ifndef DEMNODE_H
#define DEMNODE_H

#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include "DemWorker.h"
#include "NodeUtils.h"
#include <QtNodes/internal/ExecutableNodeDelegateModel.hpp>
#include <QtNodes/NodeData>
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
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

class DemNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    DemNode();
    ~DemNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("DEM Generation"); }
    QString name() const override { return QStringLiteral("DEM Generation"); }
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

    bool supportsValidation() const override { return true; }
    ::QWidget* createValidationWidget(::QWidget* parent) override;
    std::shared_ptr<ImportedFileData> inputDataForValidation() const { return m_inputData; }

    // ExecutableNodeDelegateModel interface implementation
    void setExecutionMode(ExecutionMode mode) override;

protected:
    bool validateAndRestoreOutput() override;
    QStringList previewImagePaths() const override;
    bool prepareToStart() override;

private:
    ::QWidget* _widget;
    QComboBox* m_methodCombo;
    
    QLabel* m_timesLabel;
    QLineEdit* m_timesEdit;
    
    QLineEdit* m_outputNodeNameEdit;

    // Input/output data
    std::shared_ptr<ImportedFileData> m_inputData;
    std::shared_ptr<InsarDemData> m_outputData;
    std::shared_ptr<ImageInfoData> m_imageInfoData;
    
    // Parameters
    QString m_outputNodeName;
    int m_method; // 1: Newton
    int m_times;  // default 20

    // Worker thread
    DemWorker* m_workerThread;
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
    int m_preparedTimes = 20;
    NodeUtils::OutputTransaction m_outputTransaction;
    QList<DemFileResult> m_pendingDemResults;
    bool m_xmlDirty = false;
    bool m_previewGenerationPending = false;
    quint64 m_previewGenerationId = 0;

    // Helper methods
    void cleanupThreadResources();
    void releaseFinishedThreadResources();
    void createWidget();
    void onProgressUpdate(int progress, const QString& message);
    void onProcessingFinished();
    void onCancelled();
    void onError(const QString& error);
    void handleDemFileGenerated(const DemFileResult& result);
    void onModelUpdated(QStandardItemModel* model);
    bool validateInputs() const;
    void updateWidgetSize();
    void onMethodChanged(int index);
    QString generateDefaultOutputName() const;
    bool commitWidgetParametersForExecution();
    void executeProcessing();
    void commitDemResult(const DemFileResult& result);
    void publishDemResultToProjectTree(const DemFileResult& result);

    // Context helpers
    QStandardItemModel* projectModel() const;
    QString projectPath() const;
    QString projectName() const;
    XMLFile* projectXml() const;

    // Executable interface implementation
    void execute() override;
    void stopExecution() override;
    void processAutomatically() override;
    bool stopExecutionIsAsynchronous() const override { return true; }
    bool supportsAutomaticRestartAfterInputChange() const override { return true; }

signals:
    void startDem(int method, int times, QString savePath, QString outputNode,
                  QStringList phaseNames, QStringList phasePaths);
};

} // namespace QtNodes

#endif // DEMNODE_H
