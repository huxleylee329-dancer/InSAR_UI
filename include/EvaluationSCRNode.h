#pragma once

#include "NodeDataTypes.h"
#include "QtNodes/internal/ExecutableNodeDelegateModel.hpp"
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <opencv2/opencv.hpp>

namespace QtNodes {

class EvaluationSCRNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    EvaluationSCRNode();
    ~EvaluationSCRNode() override = default;

    QString caption() const override { return "Evaluation-SCR"; }
    QString name() const override { return "EvaluationSCR"; }

    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    bool portCaptionVisible(PortType portType, PortIndex portIndex) const override;
    QString portCaption(PortType portType, PortIndex portIndex) const override;

    void setInData(std::shared_ptr<NodeData> data, PortIndex port) override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;

    QWidget* embeddedWidget() override;

    bool isReady() const;
    void execute() override;
    void stopExecution() override;
    void processAutomatically() override;

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

private slots:
    void onRegionChanged(int index);

private:
    void createWidget();
    void calculateAndDisplaySCR();
    double calculateScr(const cv::Mat& targetGray, const cv::Mat& clutterGray) const;

    QWidget* m_widget = nullptr;
    QComboBox* m_regionComboBox = nullptr;

    QLabel* m_originalScrLabel = nullptr;
    QLabel* m_filteredScrLabel = nullptr;
    QLabel* m_improvementLabel = nullptr;

    std::shared_ptr<ImageInfoData> m_originalData = nullptr;
    std::shared_ptr<ImageInfoData> m_filteredData = nullptr;
};

} // namespace QtNodes
