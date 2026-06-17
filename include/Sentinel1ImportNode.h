#ifndef SENTINEL1IMPORTNODE_H
#define SENTINEL1IMPORTNODE_H

#include "ImportNodeBase.h"
#include "Sentinel1ImportWorker.h"
#include "NodeDataTypes.h"
#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>
#include <QThread>

namespace QtNodes {

// ============================================================================
// Sentinel1ImportNode - Single file Sentinel-1 import node
// ============================================================================
class Sentinel1ImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    Sentinel1ImportNode();
    ~Sentinel1ImportNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("Sentinel-1 Import"); }
    QString name() const override { return QStringLiteral("Sentinel1Import"); }

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
    QStringList getImportedFilePaths() const override;
    QString getOutputNodeName() const override;
    QStringList previewImagePaths() const override;

    // Thread accessors
    QThread* qThread() const override { return m_thread; }
    void stopExecution() override;

    // Helper methods
    QString generateOutputFileName() const;
    QString getOutputFileName() const;  // 获取输出文件名（优先使用用户输入，否则自动生成）
    QString resolveInputName(const QString& name) const;
    void updateAvailableParameters(const QString& manifestPath);

protected:
    bool validateAndRestoreOutput() override;

private slots:
    void onManifestBrowseClicked();
    void onPodBrowseClicked();
    void onImportProgress(int progress, const QString& message);
    void onImportFinished();
    void onThreadError(const QString& error);
    void onModelUpdated(QStandardItemModel* model);

private:
    // UI elements
    QLineEdit* m_outputNodeNameEdit;
    QLineEdit* m_outputFileNameEdit;  // 目标文件名输入框
    QLabel* m_projectLabel;      // 目标工程标签
    QLineEdit* m_manifestEdit;
    QLineEdit* m_podEdit;
    QComboBox* m_subswathCombo;
    QComboBox* m_polarizationCombo;

    // State
    QString m_manifestPath;
    QString m_podPath;
    QString m_importedFilePath;
    QString m_outputNodeName;
    QString m_outputFileName;
    QString m_subswath = "iw1";
    QString m_polarization = "vv";

    // Worker thread
    Sentinel1ImportWorker* m_workerThread;
    QThread* m_thread;

    QFutureWatcher<void> m_remedyWatcher;
    std::shared_ptr<ImageInfoData> m_imageInfoData;
};

} // namespace QtNodes

#endif // SENTINEL1IMPORTNODE_H
