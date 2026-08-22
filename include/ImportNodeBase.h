#ifndef IMPORTNODEBASE_H
#define IMPORTNODEBASE_H

#include <QtNodes/internal/ExecutableNodeDelegateModel.hpp>
#include <QtNodes/NodeData>
#include <QtNodes/NodeDelegateModelRegistry>
#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QStandardItemModel>
#include <QFutureWatcher>
#include <memory>
#include "NodeUtils.h"


// Forward declarations
class IApplicationInterface;
class BaseImportWorker;
struct ImportTask;

namespace QtNodes {

// ============================================================================
// ImportNodeBase - Base class for all import nodes
// ============================================================================
class ImportNodeBase : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    ImportNodeBase();
    virtual ~ImportNodeBase();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("Import"); }
    QString name() const override { return QStringLiteral("ImportBase"); }

    // 统一的端口配置（默认 2 端口：Port0=成果, Port1=预览）
    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    bool portCaptionVisible(PortType portType, PortIndex portIndex) const override;
    QString portCaption(PortType portType, PortIndex portIndex) const override;
    bool portIsOptional(PortType portType, PortIndex portIndex) const override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;
    QStringList previewImagePaths() const override;

    void setInData(std::shared_ptr<NodeData> data, PortIndex port) override;

    // Get project context (to be accessed by derived classes)
    QStandardItemModel* projectModel() const;
    QString projectPath() const;
    QString projectName() const;
    ::QWidget* embeddedWidget() override;

    // ExecutableNodeDelegateModel interface implementation
    void setExecutionMode(ExecutionMode mode) override;

    // 统一的执行控制
    void stopExecution() override;
    bool validateAndRestoreOutput() override;
    bool prepareToStart() override;


protected slots:
    // 统一的 Worker 槽函数
    void onImportProgress(int progress, const QString& message);
    void onImportFinished();
    void onThreadError(const QString& error);
    void onOutputsGenerated(const QString& dstNode, const QStringList& outputNames,
                            const QStringList& outputPaths, const QString& dataType,
                            const QString& satelliteFormat);
    void onModelUpdated(QStandardItemModel* model);

protected:
    // Subclass must override these (legacy interface)
    virtual void executeImport() = 0;

    // 子类必须实现：返回预期的输出文件路径列表
    virtual QStringList getExpectedOutputFilePaths() const = 0;
    // Optional preview artifacts committed with the primary outputs. The
    // default expects a JPG beside each primary output.
    virtual QStringList getExpectedPreviewFilePaths() const;
    // Source files are fingerprinted so an import cannot commit data copied
    // from inputs that changed while its worker was running.
    virtual QStringList transactionInputPaths() const;
    // 子类必须实现：返回输出的 XML 节点名称
    virtual QString getOutputNodeName() const = 0;

    // 子类可覆写：指定预览图渲染的数据类型（默认 "complex"）
    virtual QString previewDataType() const { return "complex"; }

    virtual QWidget* createWidget() = 0;

    // New Executable interface that subclasses must override
    void execute() override;
    void processAutomatically() override;

    // 辅助启动函数：子类只需调用此函数即可启动异步导入
    void startWorker(BaseImportWorker* worker, const std::vector<ImportTask>& tasks);

    // Helper methods
    void onProgressUpdate(int progress, const QString& message);
    void onError(const QString& error);
    void onImportCancelled();

    // Get project context interface
    IApplicationInterface* getProjectContext() const;

    // 统一的工作线程管理
    QThread* m_thread = nullptr;
    BaseImportWorker* m_worker = nullptr;
    NodeUtils::OverwriteResult m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;


    // 统一的输出数据
    std::shared_ptr<ImportedFileData> m_importedFiles;
    std::shared_ptr<ImageInfoData> m_imageInfo;
    QStringList m_importedFilePaths;
    QStringList m_generatedOutputPaths;
    QStringList m_generatedOutputNames;
    QString m_generatedDataType;
    QString m_generatedSatelliteFormat;
    QString m_outputFileName;

    // 异步预览生成
    QFutureWatcher<void> m_remedyWatcher;

    // Flag for stop request
    bool m_stopRequested;
    bool m_outputPersistenceFailed = false;
    bool m_semanticTransactionActive = false;
    NodeUtils::OutputTransaction m_outputTransaction;
};

} // namespace QtNodes

#endif // IMPORTNODEBASE_H
