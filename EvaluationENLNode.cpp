#include "EvaluationENLNode.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QJsonObject>
#include <QJsonValue>

namespace QtNodes {

EvaluationENLNode::EvaluationENLNode()
{
    createWidget();
}

void EvaluationENLNode::createWidget()
{
    m_widget = new QWidget();
    auto* mainLayout = new QVBoxLayout(m_widget);
    mainLayout->setContentsMargins(5, 5, 5, 5);

    auto* roiGroup = new QGroupBox("区域选择");
    auto* roiLayout = new QVBoxLayout(roiGroup);
    m_regionComboBox = new QComboBox();
    m_regionComboBox->addItem("全部");
    m_regionComboBox->addItem("中间区域");
    roiLayout->addWidget(m_regionComboBox);
    mainLayout->addWidget(roiGroup);

    auto* resultGroup = new QGroupBox("ENL结果");
    auto* formLayout = new QFormLayout(resultGroup);
    
    m_originalEnlLabel = new QLabel("--");
    m_filteredEnlLabel = new QLabel("--");
    
    formLayout->addRow("原图ENL：", m_originalEnlLabel);
    formLayout->addRow("滤波后ENL：", m_filteredEnlLabel);
    mainLayout->addWidget(resultGroup);

    m_widget->setMinimumWidth(200);

    connect(m_regionComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &EvaluationENLNode::onRegionChanged);
}

unsigned int EvaluationENLNode::nPorts(PortType portType) const
{
    return (portType == PortType::In) ? 2 : 0;
}

NodeDataType EvaluationENLNode::dataType(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return ImageInfoData().type();
}

bool EvaluationENLNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return true;
}

QString EvaluationENLNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) {
        if (portIndex == 0) return "原图";
        if (portIndex == 1) return "滤波后图像";
    }
    return QString();
}

bool EvaluationENLNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return true;
}

void EvaluationENLNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    if (port == 0) {
        m_originalData = std::dynamic_pointer_cast<ImageInfoData>(data);
    } else if (port == 1) {
        m_filteredData = std::dynamic_pointer_cast<ImageInfoData>(data);
    }

    Q_EMIT dataUpdated(port);
    
    // Auto execute if ready
    if (isReady()) {
        execute();
    } else {
        // Clear ENL if data is removed
        calculateAndDisplayENL();
    }
}

std::shared_ptr<NodeData> EvaluationENLNode::outData(PortIndex port)
{
    Q_UNUSED(port);
    return nullptr;
}

QWidget* EvaluationENLNode::embeddedWidget()
{
    return m_widget;
}

bool EvaluationENLNode::isReady() const
{
    return (m_originalData != nullptr && !m_originalData->filePath().isEmpty()) || 
           (m_filteredData != nullptr && !m_filteredData->filePath().isEmpty());
}

void EvaluationENLNode::execute()
{
    calculateAndDisplayENL();
    setExecutionState(ExecutionState::Completed);
}

void EvaluationENLNode::onRegionChanged(int index)
{
    Q_UNUSED(index);
    if (isReady()) {
        execute();
    }
}

double EvaluationENLNode::calculateENL(const cv::Mat& roiGray) const
{
    if (roiGray.empty()) return 0.0;
    
    cv::Mat meanMat, stddevMat;
    cv::meanStdDev(roiGray, meanMat, stddevMat);
    
    double mean = meanMat.at<double>(0, 0);
    double stddev = stddevMat.at<double>(0, 0);
    
    if (stddev == 0) return 0.0;
    
    return (mean * mean) / (stddev * stddev);
}

void EvaluationENLNode::calculateAndDisplayENL()
{
    auto processImage = [this](std::shared_ptr<ImageInfoData> data, QLabel* label) {
        if (!data || data->filePath().isEmpty()) {
            label->setText("--");
            return;
        }
        
        cv::Mat mat = cv::imread(data->filePath().toLocal8Bit().constData(), cv::IMREAD_GRAYSCALE);
        if (mat.empty()) {
            label->setText("--");
            return;
        }
        
        cv::Rect roi(0, 0, mat.cols, mat.rows);
        if (m_regionComboBox->currentIndex() == 1) { // 中间区域
            roi = cv::Rect(mat.cols / 4, mat.rows / 4, mat.cols / 2, mat.rows / 2);
        }
        
        cv::Mat roiMat = mat(roi);
        double enl = calculateENL(roiMat);
        label->setText(QString::number(enl, 'f', 4));
    };

    processImage(m_originalData, m_originalEnlLabel);
    processImage(m_filteredData, m_filteredEnlLabel);
}

QJsonObject EvaluationENLNode::save() const
{
    QJsonObject modelJson = ExecutableNodeDelegateModel::save();
    modelJson["regionIndex"] = m_regionComboBox->currentIndex();
    return modelJson;
}

void EvaluationENLNode::load(QJsonObject const &json)
{
    ExecutableNodeDelegateModel::load(json);
    if (json.contains("regionIndex")) {
        m_regionComboBox->setCurrentIndex(json["regionIndex"].toInt());
    }
}

} // namespace QtNodes
