#ifndef S1SWATHMERGENODE_H
#define S1SWATHMERGENODE_H

#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include "NodeUtils.h"
#include "S1SwathMergeWorker.h"
#include <QtNodes/internal/ExecutableNodeDelegateModel.hpp>
#include <QtNodes/NodeData>
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QThread>
#include <QStandardItemModel>
#include <QFutureWatcher>
#include <memory>

class IApplicationInterface;
class XMLFile;

namespace QtNodes {

// ============================================================================
// S1SwathMergeNode - Sentinel-1 Swath Merge preprocessing node
// ============================================================================
class S1SwathMergeNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    S1SwathMergeNode();
    ~S1SwathMergeNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("S1 Swath Merge"); }
    QString name() const override { return QStringLiteral("S1SwathMerge"); }
    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;
    void setInData(std::shared_ptr<NodeData> data, PortIndex port) override;
    ::QWidget* embeddedWidget() override;

    bool portCaptionVisible(PortType portType, PortIndex portIndex) const override;
    QString portCaption(PortType portType, PortIndex portIndex) const override;
    bool portIsOptional(PortType portType, PortIndex portIndex) const override;

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

    // ExecutableNodeDelegateModel interface implementation
    void setExecutionMode(ExecutionMode mode) override;
    QStringList previewImagePaths() const override;

protected:
    bool stopExecutionIsAsynchronous() const override { return true; }
    bool supportsAutomaticRestartAfterInputChange() const override { return true; }
    bool prepareToStart() override;
    bool validateAndRestoreOutput() override;

private:
    QSpinBox* m_indexSpins[3];
    QLineEdit* m_outputNodeNameEdit;

    // Input data storage
    std::shared_ptr<ImportedFileData> m_inputs[3];
    std::shared_ptr<ImportedFileData> m_outputData;
    std::shared_ptr<ImageInfoData> m_imageInfoData;
    QString m_outputNodeName;
    int m_index1 = 1;
    int m_index2 = 1;
    int m_index3 = 1;

    // Worker thread
    S1SwathMergeWorker* m_worker;
    QThread* m_thread;
    QFutureWatcher<void> m_remedyWatcher;

    // Helper methods
    void createWidget();
    void onProgressUpdate(int progress, const QString& message);
    void onProcessingFinished();
    void onError(const QString& error);
    void onCancelled();
    void onResultReceived(const QString& dstNode, const QString& filename,
                          const QString& mergedH5Path, const QString& savePath,
                          const QString& projectName);
    void publishResultToProjectTree(const QString& h5Path, const QString& filename);
    bool validateInputs() const;
    QString generateDefaultOutputName() const;
    void executeProcessing();

    // Get project context interface
    QStandardItemModel* projectModel() const;
    QString projectPath() const;
    QString projectName() const;
    XMLFile* projectXml() const;

    // Executable interface implementation
    void execute() override;
    void stopExecution() override;
    void processAutomatically() override;

    // Prepared data for pre-execution lifecycle
    NodeUtils::OverwriteResult m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;
    QString m_preparedDstNode;
    QString m_preparedSavePath;
    QString m_preparedProjectName;
    QStringList m_preparedInputPaths;
    QStringList m_preparedOutputPaths;
    NodeUtils::OutputTransaction m_outputTransaction;
    QString m_generatedOutputPath;
    QString m_pendingOutputName;
    bool m_xmlDirty = false;
    bool m_previewGenerationPending = false;
    quint64 m_previewGenerationId = 0;

signals:
    void startSwathMerge(QString projectName, QString savePath, QString dstNode,
                         QString firstH5Path, QString secondH5Path, QString thirdH5Path);
};

} // namespace QtNodes

#endif // S1SWATHMERGENODE_H
