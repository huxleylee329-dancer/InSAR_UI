#ifndef SBASREFERENCERESELECTIONNODE_H
#define SBASREFERENCERESELECTIONNODE_H

#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include "NodeUtils.h"
#include "SBASReferenceReselectionWorker.h"
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
#include <QJsonArray>
#include <QPoint>
#include <QList>
#include <memory>

namespace QtNodes {

class SBASReferenceReselectionNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    SBASReferenceReselectionNode();
    ~SBASReferenceReselectionNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("Reference Point Re-selection"); }
    QString name() const override { return QStringLiteral("SBASReferenceReselection"); }
    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    bool portCaptionVisible(PortType portType, PortIndex portIndex) const override;
    QString portCaption(PortType portType, PortIndex portIndex) const override;
    bool portIsOptional(PortType portType, PortIndex portIndex) const override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;
    void setInData(std::shared_ptr<NodeData> data, PortIndex port) override;
    ::QWidget* embeddedWidget() override;

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

    // ExecutableNodeDelegateModel interface implementation
    void setExecutionMode(ExecutionMode mode) override;

protected:
    bool stopExecutionIsAsynchronous() const override { return true; }
    bool supportsAutomaticRestartAfterInputChange() const override { return true; }
    bool prepareToStart() override;
    bool validateAndRestoreOutput() override;
    QStringList previewImagePaths() const override;

private:
    // UI elements
    ::QWidget* _widget;
    QLabel* m_inputNodeLabel;
    QLabel* m_refPointLabel;
    QLabel* m_gcpLabel;
    QPushButton* m_selectBtn;
    QLineEdit* m_outputNodeNameEdit;
    QLabel* m_resultLabel;

    // Input/output data
    std::shared_ptr<ImportedFileData> m_inputData;
    std::shared_ptr<ImportedFileData> m_outputData; // H5 output
    std::shared_ptr<ImageInfoData> m_previewData;  // JPG preview output

    // Parameters
    int m_refRow;
    int m_refCol;
    QList<QPoint> m_GCPs;
    QString m_outputNodeName;

    // Worker thread
    SBASReferenceReselectionWorker* m_worker;
    QThread* m_thread;

    // Helper methods
    void createWidget();
    void onProgressUpdate(int progress, const QString& message);
    void onProcessingFinished();
    void onError(const QString& error);
    void onCancelled();
    bool validateInputs() const;
    void updateLabels();
    void updateWidgetSize();
    void executeProcessing();
    void generateStaticPreviewJpg();
    QString projectPath() const;
    QString projectName() const;

    // Executable interface implementation
    void execute() override;
    void stopExecution() override;
    void processAutomatically() override;

    // Prepared data for pre-execution lifecycle
    NodeUtils::OverwriteResult m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;

private slots:
    void onSelectClicked();
    void onCoordinatesSelected(int ref_row, int ref_col, QList<QPoint> plist);

signals:
    void startProcess();
};

} // namespace QtNodes

#endif // SBASREFERENCERESELECTIONNODE_H
