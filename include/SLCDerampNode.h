#ifndef SLCDERAMPNODE_H
#define SLCDERAMPNODE_H

#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include "SLCDerampWorker.h"
#include <QtNodes/internal/ExecutableNodeDelegateModel.hpp>
#include <QtNodes/NodeData>
#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QThread>
#include <QStandardItemModel>
#include <QFileInfo>
#include <QRegularExpression>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>
#include <memory>

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

protected:
    bool validateAndRestoreOutput() override;

private:
    // UI elements
    ::QWidget* _widget;
    QLineEdit* m_outputNodeNameEdit;
    QLabel* m_masterIndexLabel;

    // Input/output data storage
    std::shared_ptr<ImportedFileData> m_inputData;
    std::shared_ptr<ImportedFileData> m_outputData;
    std::shared_ptr<ImageInfoData> m_imageInfoData;
    QString m_outputNodeName;
    int m_masterIndex;

    // Worker thread
    SLCDerampWorker* m_worker;
    QThread* m_thread;
    QFutureWatcher<void> m_remedyWatcher;

    // Helper methods
    void createWidget();
    void updateLabels();
    void updateWidgetSize();
    void onProgressUpdate(int progress, const QString& message);
    void onProcessingFinished();
    void onError(const QString& error);
    void onModelUpdated(QStandardItemModel* model);
    void onResultsReceived(const QString& dstNode, const QStringList& h5Paths, const QStringList& originNames);
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

signals:
    void startDeramp(int masterIndex, QString project_name,
                     QString src_node, QString dst_node, QStandardItemModel* model);
};

} // namespace QtNodes

#endif // SLCDERAMPNODE_H
