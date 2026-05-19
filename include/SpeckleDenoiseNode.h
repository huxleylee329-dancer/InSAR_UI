#pragma once

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "NodeDataTypes.h"
#include "QtNodes/internal/ExecutableNodeDelegateModel.hpp"
#include <QLineEdit>
#include <QLabel>
#include <QCheckBox>
#include <QFileInfo>
#include <opencv2/opencv.hpp>

class IApplicationInterface;
class QStandardItemModel;

namespace QtNodes {

class SpeckleDenoiseNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    SpeckleDenoiseNode();
    ~SpeckleDenoiseNode() override;

    QString caption() const override { return "Speckle Denoise"; }
    QString name() const override { return "SpeckleDenoise"; }

    // 端口定义
    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    bool portCaptionVisible(PortType portType, PortIndex portIndex) const override;
    QString portCaption(PortType portType, PortIndex portIndex) const override;
    bool portIsOptional(PortType portType, PortIndex portIndex) const override;

    // 数据输入输出
    void setInData(std::shared_ptr<NodeData> data, PortIndex port) override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;

    // 嵌入Widget
    QWidget* embeddedWidget() override;
    void createWidget();

    // 执行
    bool isReady() const;
    void execute() override;
    void stopExecution() override;
    void processAutomatically() override;

    // 序列化
    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

private Q_SLOTS:
    void onSaveToProjectChanged(int state);

private:
    IApplicationInterface* getProjectContext() const;
    QStandardItemModel* projectModel() const;
    QString projectPath() const;
    QString projectName() const;
    // UI控件
    QWidget* _widget = nullptr;
    QLabel* m_inputImageLabel = nullptr;
    QCheckBox* m_saveToProjectCheckBox = nullptr;
    QLineEdit* m_outputNodeNameEdit = nullptr;
    QLabel* m_statusLabel = nullptr;

    // 数据
    std::shared_ptr<ImageInfoData> m_inputData = nullptr;
    std::shared_ptr<ImageInfoData> m_outputData = nullptr;
    QString m_outputImagePath;

    // 内部方法
    QString generateOutputFileName() const;
    cv::Mat runBm3dCoreLogic(const cv::Mat& inputGray) const;
    cv::Mat runBm3dDenoise(const cv::Mat& imgNorm, double sigmaFinal) const;
    bool saveResultToProject(const cv::Mat& resultImage);
    double calcMedian(const cv::Mat& img) const;
};

} // namespace QtNodes
