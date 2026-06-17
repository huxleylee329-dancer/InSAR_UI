#ifndef TSXBATCHIMPORTNODE_H
#define TSXBATCHIMPORTNODE_H

#include "ImportNodeBase.h"
#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include "TSXImportWorker.h"
#include <QWidget>
#include <QListWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QThread>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>

namespace QtNodes {

// ============================================================================
// TSXBatchImportNode - Batch import TerraSAR-X data
// ============================================================================
class TSXBatchImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    TSXBatchImportNode();
    ~TSXBatchImportNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("TerraSAR-X Batch Import"); }
    QString name() const override { return QStringLiteral("TSXBatchImport"); }

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

    // ExecutableNodeDelegateModel interface implementation
    void setExecutionMode(ExecutionMode mode) override;

protected:
    // ImportNodeBase interface
    QWidget* createWidget() override;
    void executeImport() override;
    QStringList getImportedFilePaths() const override;
    QString getOutputNodeName() const override;
    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    bool portCaptionVisible(PortType portType, PortIndex portIndex) const override;
    QString portCaption(PortType portType, PortIndex portIndex) const override;
    bool portIsOptional(PortType portType, PortIndex portIndex) const override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;
    QStringList previewImagePaths() const override;

    // Thread accessors
    QThread* qThread() const override { return m_thread; }
    void stopExecution() override;

    // Helper methods
    QString generateOutputFileName(const QString& xmlPath) const;

protected:
    bool validateAndRestoreOutput() override;

private slots:
    void onAddFilesClicked();
    void onRemoveFilesClicked();
    void onImportProgress(int progress, const QString& message);
    void onImportFinished();
    void onThreadError(const QString& error);
    void onModelUpdated(QStandardItemModel* model);

private:
    // UI elements
    QLineEdit* m_outputNodeNameEdit;
    QListWidget* m_fileListWidget;
    QComboBox* m_polarizationCombo;
    QLabel* m_projectLabel;

    // State
    QStringList m_xmlPaths;
    QStringList m_importedFilePaths;
    QString m_outputNodeName;
    QString m_polarization = "HH";

    // Port 1 预览数据
    std::shared_ptr<ImageInfoData> m_imageInfoData;
    QFutureWatcher<void> m_remedyWatcher;

    // Worker thread
    TSXImportWorker* m_workerThread;
    QThread* m_thread;
};

} // namespace QtNodes

#endif // TSXBATCHIMPORTNODE_H
