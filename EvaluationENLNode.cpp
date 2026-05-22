
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

    auto* resultGroup = new QGroupBox("ENL/EPI结果");
    auto* tableLayout = new QVBoxLayout(resultGroup);
    
    m_resultsTable = new QTableWidget();
    m_resultsTable->setColumnCount(4);
    m_resultsTable->setHorizontalHeaderLabels({QString::fromUtf8("\xe5\x9b\xbe\xe5\x83\x8f"), "原图ENL", "滤波后ENL", "EPI"});
    m_resultsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_resultsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_resultsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultsTable->setMinimumHeight(150);
    tableLayout->addWidget(m_resultsTable);

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

void EvaluationENLNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    if (port == 0) {
        m_originalData = std::dynamic_pointer_cast<ImageInfoData>(data);
    } else if (port == 1) {
        m_filteredData = std::dynamic_pointer_cast<ImageInfoData>(data);
    }

    // Call base class setInData to correctly update execution state
    ExecutableNodeDelegateModel::setInData(data, port);
    
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
    return (m_originalData != nullptr && !m_originalData->filePaths().isEmpty()) && 
           (m_filteredData != nullptr && !m_filteredData->filePaths().isEmpty());
}

void EvaluationENLNode::execute()
{
    calculateAndDisplayENL();
    setState(ExecutionState::Completed);
}

void EvaluationENLNode::stopExecution()
{
    setState(ExecutionState::Stopped);
}

void EvaluationENLNode::processAutomatically()
{
    if (isReady()) {
        execute();
    }
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

double EvaluationENLNode::calculateEPI(const cv::Mat& orig, const cv::Mat& filtered) const
{
    if (orig.empty() || filtered.empty()) return 0.0;
    cv::Mat lapOrig, lapFilt;
    cv::Laplacian(orig, lapOrig, CV_64F);
    cv::Laplacian(filtered, lapFilt, CV_64F);
    double sumOrig = cv::sum(cv::abs(lapOrig))[0];
    double sumFilt = cv::sum(cv::abs(lapFilt))[0];
    if (sumOrig == 0) return 0.0;
    return sumFilt / sumOrig;
}

void EvaluationENLNode::calculateAndDisplayENL()
{
    if (m_resultsTable) m_resultsTable->setRowCount(0);

    if (!m_originalData || !m_filteredData || m_originalData->filePaths().isEmpty() || m_filteredData->filePaths().isEmpty()) {
        return;
    }
    
    QStringList origPaths = m_originalData->filePaths();
    QStringList filtPaths = m_filteredData->filePaths();
    
    if (origPaths.size() != filtPaths.size()) {
        QMessageBox::warning(nullptr, QString::fromUtf8("\xe8\xad\xa6\xe5\x91\x8a"), QString::fromUtf8("\xe5\x8e\x9f\xe5\x9b\xbe\xe5\x92\x8c\xe6\xbb\xa4\xe6\xb3\xa2\xe5\x90\x8e\xe5\x9b\xbe\xe5\x83\x8f\xe7\x9a\x84\xe6\x95\xb0\xe9\x87\x8f\xe4\xb8\x8d\xe4\xb8\x80\xe8\x87\xb4\xef\xbc\x8c\xe6\x97\xa0\xe6\xb3\x95\xe8\xbf\x9b\xe8\xa1\x8c\xe6\x89\xb9\xe9\x87\x8f\xe8\xaf\x84\xe4\xbc\xb0\xef\xbc\x81"));
        return;
    }
    
    double totalOrigEnl = 0.0;
    double totalFiltEnl = 0.0;
    double totalEPI = 0.0;
    int count = origPaths.size();
    int validCount = 0;
    
    for (int i = 0; i < count; ++i) {
        cv::Mat origMat = cv::imread(origPaths[i].toLocal8Bit().constData(), cv::IMREAD_GRAYSCALE);
        cv::Mat filtMat = cv::imread(filtPaths[i].toLocal8Bit().constData(), cv::IMREAD_GRAYSCALE);
        
        if (origMat.empty() || filtMat.empty()) continue;
        
        cv::Rect roiOrig(0, 0, origMat.cols, origMat.rows);
        cv::Rect roiFilt(0, 0, filtMat.cols, filtMat.rows);
        
        if (m_regionComboBox->currentIndex() == 1) { // 中间区域
            roiOrig = cv::Rect(origMat.cols / 4, origMat.rows / 4, origMat.cols / 2, origMat.rows / 2);
            roiFilt = cv::Rect(filtMat.cols / 4, filtMat.rows / 4, filtMat.cols / 2, filtMat.rows / 2);
        }
        
        double origEnl = calculateENL(origMat(roiOrig));
        double filtEnl = calculateENL(filtMat(roiFilt));
        double epi = calculateEPI(origMat(roiOrig), filtMat(roiFilt));
        
        totalOrigEnl += origEnl;
        totalFiltEnl += filtEnl;
        totalEPI += epi;
        validCount++;
        
        int row = m_resultsTable->rowCount();
        m_resultsTable->insertRow(row);
        m_resultsTable->setItem(row, 0, new QTableWidgetItem(QFileInfo(filtPaths[i]).fileName()));
        m_resultsTable->setItem(row, 1, new QTableWidgetItem(QString::number(origEnl, 'f', 4)));
        m_resultsTable->setItem(row, 2, new QTableWidgetItem(QString::number(filtEnl, 'f', 4)));
        m_resultsTable->setItem(row, 3, new QTableWidgetItem(QString::number(epi, 'f', 4)));
    }
    
    if (validCount > 1) {
        int row = m_resultsTable->rowCount();
        m_resultsTable->insertRow(row);
        
        auto* avgItem = new QTableWidgetItem(QString::fromUtf8("\xe5\xb9\xb3\xe5\x9d\x87\xe5\x80\xbc"));
        avgItem->setFont(QFont("", -1, QFont::Bold));
        m_resultsTable->setItem(row, 0, avgItem);
        m_resultsTable->setItem(row, 1, new QTableWidgetItem(QString::number(totalOrigEnl / validCount, 'f', 4)));
        m_resultsTable->setItem(row, 2, new QTableWidgetItem(QString::number(totalFiltEnl / validCount, 'f', 4)));
        m_resultsTable->setItem(row, 3, new QTableWidgetItem(QString::number(totalEPI / validCount, 'f', 4)));
    }
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
