#include "EvaluationSCRNode.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QJsonObject>
#include <QJsonValue>
#include <QComboBox>

namespace QtNodes {

EvaluationSCRNode::EvaluationSCRNode()
{
    createWidget();
}

void EvaluationSCRNode::createWidget()
{
    m_widget = new QWidget();
    auto* mainLayout = new QVBoxLayout(m_widget);
    mainLayout->setContentsMargins(5, 5, 5, 5);

    auto* roiGroup = new QGroupBox(QString::fromUtf8("\xe5\x8c\xba\xe5\x9f\x9f\xe9\x80\x89\xe6\x8b\xa9")); // 区域选择
    auto* roiLayout = new QVBoxLayout(roiGroup);
    m_regionComboBox = new QComboBox();
    m_regionComboBox->addItem(QString::fromUtf8("\xe4\xb8\xad\xe5\xbf\x83\xe7\x9b\xae\xe6\xa0\x87/\xe5\x91\xa8\xe5\x9b\xb4\xe6\x9d\x82\xe6\xb3\xa2")); // 中心目标/周围杂波
    m_regionComboBox->addItem(QString::fromUtf8("\xe5\xb7\xa6\xe5\x8d\x8a\xe7\x9b\xae\xe6\xa0\x87/\xe5\x8f\xb3\xe5\x8d\x8a\xe6\x9d\x82\xe6\xb3\xa2")); // 左半目标/右半杂波
    roiLayout->addWidget(m_regionComboBox);
    mainLayout->addWidget(roiGroup);

    auto* resultGroup = new QGroupBox(QString::fromUtf8("SCR\xe7\xbb\x93\xe6\x9e\x9c")); // SCR结果
    auto* formLayout = new QFormLayout(resultGroup);
    
    m_originalScrLabel = new QLabel("--");
    m_filteredScrLabel = new QLabel("--");
    m_improvementLabel = new QLabel("--");
    
    formLayout->addRow(QString::fromUtf8("\xe5\x8e\x9f\xe5\x9b\xbeSCR\xef\xbc\x9a"), m_originalScrLabel); // 原图SCR：
    formLayout->addRow(QString::fromUtf8("\xe6\xbb\xa4\xe6\xb3\xa2\xe5\x90\x8eSCR\xef\xbc\x9a"), m_filteredScrLabel); // 滤波后SCR：
    formLayout->addRow(QString::fromUtf8("\xe6\x80\xa7\xe8\x83\xbd\xe6\x8f\x90\xe5\x8d\x87\xef\xbc\x9a"), m_improvementLabel); // 性能提升：
    mainLayout->addWidget(resultGroup);

    m_widget->setMinimumWidth(200);

    connect(m_regionComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &EvaluationSCRNode::onRegionChanged);
}

unsigned int EvaluationSCRNode::nPorts(PortType portType) const
{
    return (portType == PortType::In) ? 2 : 0;
}

NodeDataType EvaluationSCRNode::dataType(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return ImageInfoData().type();
}

bool EvaluationSCRNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return true;
}

QString EvaluationSCRNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) {
        if (portIndex == 0) return QString::fromUtf8("\xe5\x8e\x9f\xe5\x9b\xbe"); // 原图
        if (portIndex == 1) return QString::fromUtf8("\xe6\xbb\xa4\xe6\xb3\xa2\xe5\x90\x8e\xe5\x9b\xbe\xe5\x83\x8f"); // 滤波后图像
    }
    return QString();
}

void EvaluationSCRNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    if (port == 0) {
        m_originalData = std::dynamic_pointer_cast<ImageInfoData>(data);
    } else if (port == 1) {
        m_filteredData = std::dynamic_pointer_cast<ImageInfoData>(data);
    }

    ExecutableNodeDelegateModel::setInData(data, port);
    
    if (isReady()) {
        execute();
    } else {
        calculateAndDisplaySCR();
    }
}

std::shared_ptr<NodeData> EvaluationSCRNode::outData(PortIndex port)
{
    Q_UNUSED(port);
    return nullptr;
}

QWidget* EvaluationSCRNode::embeddedWidget()
{
    return m_widget;
}

bool EvaluationSCRNode::isReady() const
{
    return (m_originalData != nullptr && !m_originalData->filePath().isEmpty()) || 
           (m_filteredData != nullptr && !m_filteredData->filePath().isEmpty());
}

void EvaluationSCRNode::execute()
{
    calculateAndDisplaySCR();
    setState(ExecutionState::Completed);
}

void EvaluationSCRNode::stopExecution()
{
    setState(ExecutionState::Stopped);
}

void EvaluationSCRNode::processAutomatically()
{
    if (isReady()) {
        execute();
    }
}

void EvaluationSCRNode::onRegionChanged(int index)
{
    Q_UNUSED(index);
    if (isReady()) {
        execute();
    }
}

double EvaluationSCRNode::calculateScr(const cv::Mat& targetGray, const cv::Mat& clutterGray) const
{
    if (targetGray.empty() || clutterGray.empty()) return 0.0;
    
    cv::Mat targetDouble, clutterDouble;
    targetGray.convertTo(targetDouble, CV_64F);
    clutterGray.convertTo(clutterDouble, CV_64F);

    cv::Scalar targetMeanValue, targetStdValue;
    cv::Scalar clutterMeanValue, clutterStdValue;

    cv::meanStdDev(targetDouble, targetMeanValue, targetStdValue);
    cv::meanStdDev(clutterDouble, clutterMeanValue, clutterStdValue);

    double targetMean = targetMeanValue[0];
    double clutterMean = clutterMeanValue[0];
    double clutterStd = clutterStdValue[0];

    if (clutterStd <= 1e-12) return 0.0;

    double numerator = std::abs(targetMean - clutterMean);
    if (numerator <= 1e-12) return 0.0;

    return 20.0 * std::log10(numerator / clutterStd);
}

void EvaluationSCRNode::calculateAndDisplaySCR()
{
    double originalScr = 0.0;
    double filteredScr = 0.0;
    bool originalValid = false;
    bool filteredValid = false;

    auto processImage = [this](std::shared_ptr<ImageInfoData> data, double& scrOut, bool& validOut) {
        if (!data || data->filePath().isEmpty()) return;
        
        cv::Mat mat = cv::imread(data->filePath().toLocal8Bit().constData(), cv::IMREAD_GRAYSCALE);
        if (mat.empty()) return;
        
        cv::Rect targetRect;
        cv::Rect clutterRect;

        if (m_regionComboBox->currentIndex() == 0) { // 中心目标/周围杂波
            targetRect = cv::Rect(mat.cols / 4, mat.rows / 4, mat.cols / 2, mat.rows / 2);
            clutterRect = cv::Rect(0, 0, mat.cols / 4, mat.rows / 4);
        } else { // 左半目标/右半杂波
            targetRect = cv::Rect(0, 0, mat.cols / 2, mat.rows);
            clutterRect = cv::Rect(mat.cols / 2, 0, mat.cols / 2, mat.rows);
        }
        
        cv::Rect bounds(0, 0, mat.cols, mat.rows);
        targetRect &= bounds;
        clutterRect &= bounds;
        
        if (targetRect.width < 2 || targetRect.height < 2 || clutterRect.width < 2 || clutterRect.height < 2) return;
        
        cv::Mat targetMat = mat(targetRect);
        cv::Mat clutterMat = mat(clutterRect);
        
        scrOut = calculateScr(targetMat, clutterMat);
        validOut = true;
    };

    processImage(m_originalData, originalScr, originalValid);
    processImage(m_filteredData, filteredScr, filteredValid);

    if (originalValid) {
        m_originalScrLabel->setText(QString::number(originalScr, 'f', 4));
    } else {
        m_originalScrLabel->setText("--");
    }

    if (filteredValid) {
        m_filteredScrLabel->setText(QString::number(filteredScr, 'f', 4));
    } else {
        m_filteredScrLabel->setText("--");
    }

    if (originalValid && filteredValid && std::abs(originalScr) > 1e-12) {
        double improvement = ((filteredScr - originalScr) / std::abs(originalScr)) * 100.0;
        m_improvementLabel->setText(QString("%1%").arg(QString::number(improvement, 'f', 2)));
    } else {
        m_improvementLabel->setText("--");
    }
}

QJsonObject EvaluationSCRNode::save() const
{
    QJsonObject modelJson = ExecutableNodeDelegateModel::save();
    modelJson["regionIndex"] = m_regionComboBox->currentIndex();
    return modelJson;
}

void EvaluationSCRNode::load(QJsonObject const &json)
{
    ExecutableNodeDelegateModel::load(json);
    if (json.contains("regionIndex")) {
        m_regionComboBox->setCurrentIndex(json["regionIndex"].toInt());
    }
}

} // namespace QtNodes
