#ifndef EXPORTKMLNODE_H
#define EXPORTKMLNODE_H

#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include "ExportKMLWorker.h"
#include <QtNodes/internal/ExecutableNodeDelegateModel.hpp>
#include <QtNodes/NodeData>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QThread>
#include <QJsonObject>
#include <memory>
#include "NodeUtils.h"

namespace QtNodes {

class ExportKMLNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    ExportKMLNode();
    ~ExportKMLNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("KML Export"); }
    QString name() const override { return QStringLiteral("ExportKML"); }
    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    bool portCaptionVisible(PortType portType, PortIndex portIndex) const override;
    QString portCaption(PortType portType, PortIndex portIndex) const override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;
    void setInData(std::shared_ptr<NodeData> data, PortIndex port) override;
    ::QWidget* embeddedWidget() override;

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

    // ExecutableNodeDelegateModel interface implementation
    void setExecutionMode(ExecutionMode mode) override;

protected:
    bool validateAndRestoreOutput() override;

private slots:
    void onBrowseClicked();

private:
    void createWidget();
    void onProgressUpdate(int progress, const QString& message);
    void onProcessingFinished();
    void onError(const QString& error);
    bool validateInputs() const;
    void updateLabels();
    void updateWidgetSize();
    void executeProcessing();
    QString projectPath() const;

    // UI elements
    ::QWidget* _widget;
    QLabel* m_inputNodeLabel;
    QLineEdit* m_outputPathEdit;
    QPushButton* m_browseBtn;
    QLineEdit* m_fileNameEdit;
    QLabel* m_resultLabel;

    // Input/output data
    std::shared_ptr<ImportedFileData> m_inputData;
    std::shared_ptr<ImportedFileData> m_outputData; // KML output file path

    // Parameters
    QString m_outputPath;
    QString m_fileName;

    // Worker thread
    ExportKMLWorker* m_worker;
    QThread* m_thread;

    // Executable interface implementation
    void execute() override;
    void stopExecution() override;
    void processAutomatically() override;
    bool prepareToStart() override;

private:
    NodeUtils::OverwriteResult m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;

signals:
    void startProcess();
};

} // namespace QtNodes

#endif // EXPORTKMLNODE_H
