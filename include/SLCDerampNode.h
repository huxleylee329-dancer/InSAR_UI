#ifndef SLCDERAMPNODE_H
#define SLCDERAMPNODE_H

#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include "NodeUtils.h"
#include "SLCDerampWorker.h"
#include <QtNodes/internal/ExecutableNodeDelegateModel.hpp>
#include <QtNodes/NodeData>
#include <QWidget>
#include <QLabel>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QThread>
#include <QStandardItemModel>
#include <QFutureWatcher>
#include <QPointer>
#include <memory>
#include <vector>

class IApplicationInterface;
class XMLFile;

namespace QtNodes {

class SLCDerampNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    SLCDerampNode();
    ~SLCDerampNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("SLC Deramp"); }
    QString name() const override { return QStringLiteral("SLCDeramp"); }
    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;
    void setInData(std::shared_ptr<NodeData> data, PortIndex port) override;
    bool portCaptionVisible(PortType portType, PortIndex portIndex) const override;
    QString portCaption(PortType portType, PortIndex portIndex) const override;
    bool portIsOptional(PortType portType, PortIndex portIndex) const override;
    ::QWidget* embeddedWidget() override;

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

    // ExecutableNodeDelegateModel interface implementation
    void setExecutionMode(ExecutionMode mode) override;
    QStringList previewImagePaths() const override;
    std::vector<QString> processingInfo() const override;

protected:
    bool prepareToStart() override;
    bool validateAndRestoreOutput() override;

private:
    // UI elements
    ::QWidget* _widget;
    QLineEdit* m_outputNodeNameEdit;
    QLabel* m_masterIndexLabel;
    QCheckBox* m_deflatCheckBox = nullptr;
    QCheckBox* m_topoRemovalCheckBox = nullptr;

    QLabel* m_demPathLabel = nullptr;
    QLineEdit* m_demPathEdit = nullptr;
    QPushButton* m_demBrowseBtn = nullptr;

    // Input/output data storage
    std::shared_ptr<ImportedFileData> m_inputData;
    std::shared_ptr<ImportedFileData> m_demInputData;
    std::shared_ptr<ImportedFileData> m_outputData;
    std::shared_ptr<ImageInfoData> m_imageInfoData;
    QString m_outputNodeName;
    QString m_demPath;
    int m_masterIndex;
    bool m_isDeflat = true;
    bool m_isTopoRemoval = true;

    // Worker thread
    QPointer<SLCDerampWorker> m_worker;
    QPointer<QThread> m_thread;
    QFutureWatcher<void> m_remedyWatcher;

    // Helper methods
    void createWidget();
    void updateLabels();
    void updateWidgetSize();
    void updateParameterWidgetsEnableState();
    void onProgressUpdate(int progress, const QString& message);
    void onProcessingFinished();
    void onCancelled();
    void onError(const QString& error);
    void onResultsReceived(const QString& dstNode, const QStringList& h5Paths, const QStringList& originNames,
                           const QString& savePath, const QString& projectName);
    bool commitResultsToProjectXml(const QStringList& h5Paths, const QStringList& originNames);
    void publishResultsToProjectTree(const QStringList& h5Paths, const QStringList& originNames);
    bool validateInputs() const;
    QString generateDefaultOutputName() const;
    void executeProcessing();
    void releaseFinishedThreadAndWorker();

    // Get project context interface
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

    // Prepared data for pre-execution lifecycle
    NodeUtils::OverwriteResult m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;
    QString m_preparedDstNode;
    QString m_preparedSavePath;
    QString m_preparedProjectName;
    QString m_preparedDemPath;
    QStringList m_preparedInputPaths;
    QStringList m_preparedOutputPaths;
    int m_preparedMasterIndex = 1;
    bool m_preparedIsDeflat = true;
    bool m_preparedIsTopoRemoval = true;
    NodeUtils::OutputTransaction m_outputTransaction;
    QStringList m_generatedOutputPaths;
    QStringList m_pendingOriginNames;
    bool m_xmlDirty = false;
    bool m_previewGenerationPending = false;
    quint64 m_previewGenerationId = 0;
    QString m_processingStatus;

signals:
    void startDeramp(int masterIndex, QString projectName, QString savePath,
                     QString dstNode, QStringList inputPaths, QString demPath,
                     bool isDeflat, bool isTopoRemoval);
};

} // namespace QtNodes

#endif // SLCDERAMPNODE_H
