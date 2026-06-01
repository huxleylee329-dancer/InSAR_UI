#ifndef S1DEBURSTNODE_H
#define S1DEBURSTNODE_H

#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include "S1DeburstWorker.h"
#include <QtNodes/internal/ExecutableNodeDelegateModel.hpp>
#include <QtNodes/NodeData>
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QThread>
#include <QStandardItemModel>
#include <QFileInfo>
#include <QRegularExpression>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>
#include <memory>

// Forward declarations
class IApplicationInterface;
class XMLFile;

namespace QtNodes {

// ============================================================================
// S1DeburstNode - Sentinel-1 Deburst preprocessing node
// ============================================================================
class S1DeburstNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    S1DeburstNode();
    ~S1DeburstNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("S1 Deburst"); }
    QString name() const override { return QStringLiteral("S1Deburst"); }
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

protected:
    bool validateAndRestoreOutput() override;

private:
    // UI elements
    QComboBox* m_projectCombo;
    QComboBox* m_dataNodeCombo;
    QLineEdit* m_outputNodeNameEdit;

    // Input/output data storage
    std::shared_ptr<ImportedFileData> m_inputData;
    std::shared_ptr<ImportedFileData> m_outputData;
    std::shared_ptr<ImageInfoData> m_imageInfoData;
    QString m_outputNodeName;

    // Worker thread
    S1DeburstWorker* m_worker;
    QThread* m_thread;
    QFutureWatcher<void> m_remedyWatcher;

    // Helper methods
    void createWidget();
    void onProgressUpdate(int progress, const QString& message);
    void onProcessingFinished();
    void onError(const QString& error);
    void onModelUpdated(QStandardItemModel* model);
    // 接收 Worker 的 sendResults 信号，用原生 TinyXML 完成 XML 落盘（SOP 避坑经验 #9）
    void onResultsReceived(const QString& dstNode, const QStringList& deburstH5Paths, const QStringList& originNames);
    bool validateInputs() const;
    void updateLabels();
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

signals:
    void startDeburst(QString savePath, QString dstProject,
                     QString srcNode, QString dstNode, QStandardItemModel* model);
};

} // namespace QtNodes

#endif // S1DEBURSTNODE_H
