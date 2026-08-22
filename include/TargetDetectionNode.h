#pragma once


#include "NodeDataTypes.h"
#include "QtNodes/internal/ExecutableNodeDelegateModel.hpp"
#include "TargetDetectionTask.h"
#include <QLineEdit>
#include <QLabel>
#include <QComboBox>
#include <QThreadPool>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QJsonArray>
#include <QJsonObject>

class IApplicationInterface;

namespace QtNodes {

class TargetDetectionNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    TargetDetectionNode();
    ~TargetDetectionNode() override;

    QString caption() const override { return "Target Detection"; }
    QString name() const override { return "TargetDetection"; }

    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    bool portCaptionVisible(PortType portType, PortIndex portIndex) const override;
    QString portCaption(PortType portType, PortIndex portIndex) const override;

    void setInData(std::shared_ptr<NodeData> data, PortIndex port) override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;

    QWidget* embeddedWidget() override;
    void createWidget();
    
    QStringList previewImagePaths() const override;
    QList<QStringList> detectionResults() const override;

    bool isReady() const;
    void execute() override;
    void stopExecution() override;
    void processAutomatically() override;
    void collapseDetailedList() override;

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;
    void prepareForPaste(QJsonObject& json, PasteContext& context) const override;
    ProductInputContract productInputContract(PortIndex portIndex) const override;
    ProductOutputContract productOutputContract(PortIndex portIndex) const override;

protected:
    bool stopExecutionIsAsynchronous() const override { return true; }
    bool supportsAutomaticRestartAfterInputChange() const override { return true; }
    bool validateAndRestoreOutput() override;

private Q_SLOTS:
    void onProgressUpdate(int progress, const QString& message);
    void onDetectionFinished(int imageIndex, bool success, float shipProb, QString resultText, QString errorMsg);
    void onError(const QString& error);
    void onCancelled();
    void onAskUserError(quint64 requestId, const QString& message);

private:
    void executeProcessing();

    // UI
    QWidget* _widget = nullptr;
    QLabel* m_inputImageLabel = nullptr;
    QComboBox* m_modelComboBox = nullptr;
    QLineEdit* m_thresholdEdit = nullptr;

    QWidget* m_simpleResultWidget = nullptr;
    QLabel* m_resultLabel = nullptr;
    QLabel* m_probabilityLabel = nullptr;
    QLabel* m_summaryLabel = nullptr;
    QLabel* m_expandLabel = nullptr;
    bool m_isExpanded = false;

    QTableWidget* m_resultsTable = nullptr;

    // Data
    std::shared_ptr<ImageInfoData> m_inputData = nullptr;
    std::shared_ptr<ImageInfoData> m_outputData = nullptr;
    QString m_selectedModelPath;
    float m_thresholdValue = 0.65f;

    struct DetectionResult {
        QString fileName;
        QString resultText;
        QString probability;
    };
    QList<DetectionResult> m_savedResults;

    // Threading
    TargetDetectionTask* m_task = nullptr;
    quint64 m_executionGeneration = 0;
};

} // namespace QtNodes
