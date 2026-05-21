#pragma once

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "NodeDataTypes.h"
#include "QtNodes/internal/ExecutableNodeDelegateModel.hpp"
#include <QWidget>
#include <QComboBox>
#include <QLabel>
#include <opencv2/opencv.hpp>

namespace QtNodes {

class EvaluationENLNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    EvaluationENLNode();
    ~EvaluationENLNode() override = default;

    QString caption() const override { return "Evaluation-ENL"; }
    QString name() const override { return "EvaluationENL"; }

    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    bool portCaptionVisible(PortType portType, PortIndex portIndex) const override;
    QString portCaption(PortType portType, PortIndex portIndex) const override;
    bool portIsOptional(PortType portType, PortIndex portIndex) const override;

    void setInData(std::shared_ptr<NodeData> data, PortIndex port) override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;

    QWidget* embeddedWidget() override;

    bool isReady() const override;
    void execute() override;

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

private slots:
    void onRegionChanged(int index);

private:
    void createWidget();
    void calculateAndDisplayENL();
    double calculateENL(const cv::Mat& roiGray) const;

    QWidget* m_widget = nullptr;
    QComboBox* m_regionComboBox = nullptr;
    QLabel* m_originalEnlLabel = nullptr;
    QLabel* m_filteredEnlLabel = nullptr;

    std::shared_ptr<ImageInfoData> m_originalData = nullptr;
    std::shared_ptr<ImageInfoData> m_filteredData = nullptr;
};

} // namespace QtNodes
