#ifndef BASELINEPREVIEWNODE_H
#define BASELINEPREVIEWNODE_H

#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include "BaselineWorker.h"
#include "Baseline_Preview.h"
#include <QtNodes/internal/ExecutableNodeDelegateModel.hpp>
#include <QtNodes/NodeData>
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QThread>
#include <QStandardItemModel>
#include <QJsonObject>
#include <QJsonArray>
#include <memory>

namespace QtNodes {

class BaselinePreviewNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    BaselinePreviewNode();
    ~BaselinePreviewNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("Baseline Preview"); }
    QString name() const override { return QStringLiteral("BaselinePreview"); }
    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    bool portCaptionVisible(PortType portType, PortIndex portIndex) const override;
    QString portCaption(PortType portType, PortIndex portIndex) const override;
    bool portIsOptional(PortType portType, PortIndex portIndex) const override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;
    void setInData(std::shared_ptr<NodeData> data, PortIndex port) override;
    void inputConnectionDeleted(ConnectionId const& connectionId) override;
    ::QWidget* embeddedWidget() override;

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

    // ExecutableNodeDelegateModel interface implementation
    void setExecutionMode(ExecutionMode mode) override;

protected:
    bool validateAndRestoreOutput() override;
    QStringList previewImagePaths() const override;

private:
    // UI elements
    ::QWidget* _widget;
    QLabel* m_inputNodeLabel;
    QCheckBox* m_defaultMasterCheckBox;
    QComboBox* m_masterImageCombo;
    QPushButton* m_showChartBtn;
    QLabel* m_resultLabel;

    // Input/output data
    std::shared_ptr<ImportedFileData> m_inputData;
    std::shared_ptr<BaselineData> m_outputData;

    // Parameters
    int m_masterIndex;
    bool m_useDefaultMaster;
    QList<double> m_temporalBaselines;
    QList<double> m_spatialBaselines;

    // Worker thread
    BaselineWorker* m_worker;
    QThread* m_thread;

    // Helper methods
    void createWidget();
    void onProgressUpdate(int progress, const QString& message);
    void onProcessingFinished(QList<double> temporal_baseline, QList<double> spatial_baseline, int index);
    void onError(const QString& error);
    bool validateInputs() const;
    void updateLabels();
    void updateWidgetSize();
    void updateMasterImageCombo();
    void executeProcessing();
    void showChart();
    void generateStaticPreviewJpg();
    QString projectPath() const;

    // Executable interface implementation
    void execute() override;
    void stopExecution() override;
    void processAutomatically() override;

signals:
    void startEstimate(int index, QStringList filePaths);
};

} // namespace QtNodes

#endif // BASELINEPREVIEWNODE_H
