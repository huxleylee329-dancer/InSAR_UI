#ifndef CSKIMPORTNODE_H
#define CSKIMPORTNODE_H


#include "ImportNodeBase.h"
#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include "CSKImportWorker.h"
#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QThread>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>

namespace QtNodes {

// ============================================================================
// CSKImportNode - Batch import COSMO-SkyMed data
// ============================================================================
class CSKImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    CSKImportNode();
    ~CSKImportNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("COSMO-SkyMed Import"); }
    QString name() const override { return QStringLiteral("CSKImport"); }

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
    QString generateOutputFileName(const QString& filePath) const;

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
    QLabel* m_projectLabel;  // Target project label (read-only)
    QPushButton* m_importButton;  // Kept for compatibility, not used in UI
    QPushButton* m_stopButton;  // Kept for compatibility, not used in UI

    // State
    QStringList m_filePaths;
    QStringList m_importedFilePaths;
    QString m_outputNodeName;

    // Port 1 预览数据
    std::shared_ptr<ImageInfoData> m_imageInfoData;
    QFutureWatcher<void> m_remedyWatcher;

    // Worker thread
    CSKImportWorker* m_workerThread;
    QThread* m_thread;
};

} // namespace QtNodes

#endif // CSKIMPORTNODE_H
