#ifndef SENTINEL1BATCHIMPORTNODE_H
#define SENTINEL1BATCHIMPORTNODE_H

#include "ImportNodeBase.h"
#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include "Sentinel1ImportWorker.h"
#include "NodeUtils.h"
#include <QWidget>
#include <QListWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>
#include <QThread>

namespace QtNodes {

// ============================================================================
// Sentinel1BatchImportNode - Batch import Sentinel-1 data
// ============================================================================
class Sentinel1BatchImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    Sentinel1BatchImportNode();
    ~Sentinel1BatchImportNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("Sentinel-1 Batch Import"); }
    QString name() const override { return QStringLiteral("Sentinel1BatchImport"); }

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    bool portCaptionVisible(PortType portType, PortIndex portIndex) const override;
    QString portCaption(PortType portType, PortIndex portIndex) const override;
    bool portIsOptional(PortType portType, PortIndex portIndex) const override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;

    // ExecutableNodeDelegateModel interface implementation
    void setExecutionMode(ExecutionMode mode) override;

protected:
    // ImportNodeBase interface
    QWidget* createWidget() override;
    void executeImport() override;
    bool prepareToStart() override;
    QStringList getImportedFilePaths() const override;
    QString getOutputNodeName() const override;
    QStringList previewImagePaths() const override;

    // Thread accessors
    QThread* qThread() const override { return m_thread; }
    void stopExecution() override;

    // Helper methods
    QString generateImportName(const QString& manifestPath) const;
    void updateAvailableParameters();

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
    QComboBox* m_subswathCombo;
    QComboBox* m_polarizationCombo;
    QLabel* m_projectLabel;

    // State
    QStringList m_manifestPaths;
    QStringList m_importedFilePaths;
    QString m_outputNodeName;
    QString m_subswath = "iw1";
    QString m_polarization = "vv";

    std::vector<QString> m_preparedOriginalNameList;
    std::vector<QString> m_preparedImportNameList;
    QString m_preparedOutputNodeName;
    NodeUtils::OverwriteResult m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;

    // Worker thread
    Sentinel1ImportWorker* m_workerThread;
    QThread* m_thread;

    QFutureWatcher<void> m_remedyWatcher;
    std::shared_ptr<ImageInfoData> m_imageInfoData;
};

} // namespace QtNodes

#endif // SENTINEL1BATCHIMPORTNODE_H
