#ifndef LUTANIMPORTNODE_H
#define LUTANIMPORTNODE_H

#include "ImportNodeBase.h"
#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include "LUTANImportWorker.h"
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

class LUTANImportNode : public ImportNodeBase
{
    Q_OBJECT

public:
    LUTANImportNode();
    ~LUTANImportNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("LuTan-1 Import"); }
    QString name() const override { return QStringLiteral("LUTANImport"); }

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

protected:
    bool validateAndRestoreOutput() override;

private slots:
    void onDataBrowseClicked();
    void onXmlBrowseClicked();
    void onAddTaskClicked();
    void onRemoveTaskClicked();
    void onImportProgress(int progress, const QString& message);
    void onImportFinished();
    void onThreadError(const QString& error);
    void onModelUpdated(QStandardItemModel* model);

private:
    // UI elements
    QLineEdit* m_dataEdit;
    QLineEdit* m_xmlEdit;
    QComboBox* m_modeCombo;
    QListWidget* m_fileListWidget;
    QLineEdit* m_outputNodeNameEdit;
    QLabel* m_projectLabel;

    // State
    QStringList m_dataFiles;
    QStringList m_xmlFiles;
    QList<int> m_modes;
    QStringList m_importedFilePaths;
    QString m_outputNodeName;

    // Port 1 预览数据
    std::shared_ptr<ImageInfoData> m_imageInfoData;
    QFutureWatcher<void> m_remedyWatcher;

    // Worker thread
    LUTANImportWorker* m_workerThread;
    QThread* m_thread;
};

} // namespace QtNodes

#endif // LUTANIMPORTNODE_H
