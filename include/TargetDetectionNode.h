#pragma once

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "NodeDataTypes.h"
#include "QtNodes/internal/ExecutableNodeDelegateModel.hpp"
#include "MyThread.h"
#include <QLineEdit>
#include <QLabel>
#include <QComboBox>
#include <QThread>

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

    bool isReady() const;
    void execute() override;
    void stopExecution() override;
    void processAutomatically() override;

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

protected:
    bool validateAndRestoreOutput() override;

signals:
    void startTargetDetection(QString imagePath, QString modelPath, float thresholdValue);

private Q_SLOTS:
    void onProgressUpdate(int progress, const QString& message);
    void onDetectionFinished(bool success, float shipProb, QString resultText, QString errorMsg);
    void onError(const QString& error);

private:
    void executeProcessing();

    // UI
    QWidget* _widget = nullptr;
    QLabel* m_inputImageLabel = nullptr;
    QComboBox* m_modelComboBox = nullptr;
    QLineEdit* m_thresholdEdit = nullptr;
    QLabel* m_resultLabel = nullptr;
    QLabel* m_probabilityLabel = nullptr;
    QLabel* m_statusLabel = nullptr;

    // Data
    std::shared_ptr<ImageInfoData> m_inputData = nullptr;
    std::shared_ptr<ImageInfoData> m_outputData = nullptr;
    QString m_selectedModelPath;
    float m_thresholdValue = 0.65f;
    QString m_savedResultText = "--";
    float m_savedShipProb = 0.0f;

    // Threading
    QThread* m_thread = nullptr;
    MyThread* m_workerThread = nullptr;
};

} // namespace QtNodes
