#ifndef TSXIMPORTNODE_H
#define TSXIMPORTNODE_H

#include "ImportNodeBase.h"
#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include "TSXImportWorker.h"
#include <QWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QThread>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>

namespace QtNodes {

// ============================================================================
// TSXImportNode - Single file TerraSAR-X import node
// ============================================================================
class TSXImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    TSXImportNode();
    ~TSXImportNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("TerraSAR-X Import"); }
    QString name() const override { return QStringLiteral("TSXImport"); }

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
    QString generateOutputFileName() const;

protected:
    bool validateAndRestoreOutput() override;

private slots:
    void onXmlBrowseClicked();
    void onImportProgress(int progress, const QString& message);
    void onImportFinished();
    void onThreadError(const QString& error);
    void onModelUpdated(QStandardItemModel* model);

private:
    // UI elements
    QLineEdit* m_outputNodeNameEdit;
    QLineEdit* m_outputFileNameEdit;
    QLineEdit* m_xmlEdit;
    QComboBox* m_polarizationCombo;
    QLabel* m_projectLabel;

    // State
    QString m_xmlPath;
    QString m_importedFilePath;
    QString m_outputNodeName;
    QString m_outputFileName;
    QString m_polarization = "HH";

    // Port 1 预览数据
    std::shared_ptr<ImageInfoData> m_imageInfoData;
    QFutureWatcher<void> m_remedyWatcher;

    // Worker thread
    TSXImportWorker* m_workerThread;
    QThread* m_thread;
};

} // namespace QtNodes

#endif // TSXIMPORTNODE_H
