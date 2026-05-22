#pragma once


#include "NodeDataTypes.h"
#include "QtNodes/internal/ExecutableNodeDelegateModel.hpp"
#include <QWidget>
#include <QComboBox>
#include <opencv2/opencv.hpp>
#include <QTableWidget>
#include <QHeaderView>
#include <QMessageBox>

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
    void calculateAndDisplayENL();
    double calculateENL(const cv::Mat& roiGray) const;
    double calculateEPI(const cv::Mat& orig, const cv::Mat& filtered) const;

    QWidget* m_widget = nullptr;
    QComboBox* m_regionComboBox = nullptr;
    QTableWidget* m_resultsTable = nullptr;

    std::shared_ptr<ImageInfoData> m_originalData = nullptr;
    std::shared_ptr<ImageInfoData> m_filteredData = nullptr;
};

} // namespace QtNodes
