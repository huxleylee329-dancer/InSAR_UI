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
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);

    auto* roiGroup = new QGroupBox(QStringLiteral("区域选择")); // 区域选择
    auto* roiLayout = new QVBoxLayout(roiGroup);
    m_regionComboBox = new QComboBox();
    m_regionComboBox->addItem(QStringLiteral("中心目标/周围杂波")); // 中心目标/周围杂波
    m_regionComboBox->addItem(QStringLiteral("左半目标/右半杂波")); // 左半目标/右半杂波
    roiLayout->addWidget(m_regionComboBox);
    mainLayout->addWidget(roiGroup);

    auto* resultGroup = new QGroupBox(QStringLiteral("SCR结果")); // SCR结果
    auto* tableLayout = new QVBoxLayout(resultGroup);
    tableLayout->setContentsMargins(5, 5, 5, 5);
    tableLayout->setSpacing(2);

    m_simpleResultWidget = new QWidget();
    auto* simpleLayout = new QVBoxLayout(m_simpleResultWidget);
    simpleLayout->setContentsMargins(0, 0, 0, 0);
    simpleLayout->setSpacing(2);

    QWidget* singleResultView = new QWidget();
    singleResultView->setObjectName("SingleResultView");
    auto* singleLayout = new QFormLayout(singleResultView);
    singleLayout->setContentsMargins(0, 0, 0, 0);

    m_originalScrLabel = new QLabel("--");
    m_filteredScrLabel = new QLabel("--");
    m_improvementLabel = new QLabel("--");
    
    singleLayout->addRow(QStringLiteral("原图SCR："), m_originalScrLabel); // 原图SCR：
    singleLayout->addRow(QStringLiteral("滤波后SCR："), m_filteredScrLabel); // 滤波后SCR：
    singleLayout->addRow(QStringLiteral("性能提升："), m_improvementLabel); // 性能提升：

    simpleLayout->addWidget(singleResultView);

    m_summaryLabel = new QLabel("--");
    m_summaryLabel->hide();
    simpleLayout->addWidget(m_summaryLabel);

    tableLayout->addWidget(m_simpleResultWidget);

    m_expandLabel = new QLabel();
    m_expandLabel->setText(QStringLiteral("<a href=\"#expand\" style=\"color: #0078D7; text-decoration: none;\">▼ 展开详细列表</a>"));
    m_expandLabel->setTextFormat(Qt::RichText);
    m_expandLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_expandLabel->setOpenExternalLinks(false);
    m_expandLabel->hide();
    tableLayout->addWidget(m_expandLabel);
    
    m_resultsTable = new QTableWidget();
    m_resultsTable->setColumnCount(4);
    m_resultsTable->setHorizontalHeaderLabels({QStringLiteral("图像"), QStringLiteral("原图SCR"), QStringLiteral("滤波后SCR"), QStringLiteral("提升(%)")});
    m_resultsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    // m_resultsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_resultsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultsTable->setMinimumHeight(150);
    m_resultsTable->setVisible(false);
    tableLayout->addWidget(m_resultsTable);

    mainLayout->addWidget(resultGroup);

    connect(m_expandLabel, &QLabel::linkActivated, this, [this](const QString &link) {
        if (link == "#expand") {
            m_isExpanded = !m_isExpanded;
            m_resultsTable->setVisible(m_isExpanded);
            m_expandLabel->setText(m_isExpanded ? 
                QStringLiteral("<a href=\"#expand\" style=\"color: #0078D7; text-decoration: none;\">▲ 收起详细列表</a>") : 
                QStringLiteral("<a href=\"#expand\" style=\"color: #0078D7; text-decoration: none;\">▼ 展开详细列表</a>"));
            if (m_widget) {
                m_widget->setFixedWidth(!m_resultsTable->isHidden() ? 450 : 200);
                m_widget->resize(0, 0);
                m_widget->adjustSize();
                Q_EMIT embeddedWidgetSizeUpdated();
            }
        }
    });

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
        if (portIndex == 0) return QStringLiteral("原图"); // 原图
        if (portIndex == 1) return QStringLiteral("滤波后图像"); // 滤波后图像
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
    return (m_originalData != nullptr && !m_originalData->filePaths().isEmpty()) || 
           (m_filteredData != nullptr && !m_filteredData->filePaths().isEmpty());
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
    if (m_resultsTable) m_resultsTable->setRowCount(0);
    if (m_originalScrLabel) m_originalScrLabel->setText("--");
    if (m_filteredScrLabel) m_filteredScrLabel->setText("--");
    if (m_improvementLabel) m_improvementLabel->setText("--");
    if (m_summaryLabel) m_summaryLabel->setText("--");

    int maxCount = 0;
    QStringList origPaths;
    QStringList filtPaths;
    if (m_originalData) {
        origPaths = m_originalData->filePaths();
        if (origPaths.size() > maxCount) maxCount = origPaths.size();
    }
    if (m_filteredData) {
        filtPaths = m_filteredData->filePaths();
        if (filtPaths.size() > maxCount) maxCount = filtPaths.size();
    }

    if (maxCount == 0) {
        QWidget* singleView = m_widget ? m_widget->findChild<QWidget*>("SingleResultView") : nullptr;
        if (singleView) singleView->show();
        if (m_summaryLabel) m_summaryLabel->hide();
        if (m_expandLabel) m_expandLabel->hide();
        if (m_resultsTable) m_resultsTable->hide();
        return;
    }

    auto getScr = [this](const QString& path) -> double {
        if (path.isEmpty()) return -1.0;
        cv::Mat mat = cv::imread(path.toLocal8Bit().constData(), cv::IMREAD_GRAYSCALE);
        if (mat.empty()) return -1.0;
        
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
        
        if (targetRect.width < 2 || targetRect.height < 2 || clutterRect.width < 2 || clutterRect.height < 2) return -1.0;
        
        cv::Mat targetMat = mat(targetRect);
        cv::Mat clutterMat = mat(clutterRect);
        
        return calculateScr(targetMat, clutterMat);
    };

    double totalOrigScr = 0.0;
    double totalFiltScr = 0.0;
    double totalImp = 0.0;
    int validOrigCount = 0;
    int validFiltCount = 0;
    int validImpCount = 0;

    for (int i = 0; i < maxCount; ++i) {
        QString origPath = i < origPaths.size() ? origPaths[i] : "";
        QString filtPath = i < filtPaths.size() ? filtPaths[i] : "";

        double origScr = getScr(origPath);
        double filtScr = getScr(filtPath);

        QString origScrStr = origScr >= 0.0 ? QString::number(origScr, 'f', 4) : "--";
        QString filtScrStr = filtScr >= 0.0 ? QString::number(filtScr, 'f', 4) : "--";
        QString impStr = "--";

        if (origScr >= 0.0) {
            totalOrigScr += origScr;
            validOrigCount++;
        }
        if (filtScr >= 0.0) {
            totalFiltScr += filtScr;
            validFiltCount++;
        }
        if (origScr >= 0.0 && filtScr >= 0.0 && origScr > 1e-12) {
            double imp = ((filtScr - origScr) / origScr) * 100.0;
            impStr = QString::number(imp, 'f', 2);
            totalImp += imp;
            validImpCount++;
        }

        QString fileName = origPath.isEmpty() ? QFileInfo(filtPath).fileName() : QFileInfo(origPath).fileName();

        int row = m_resultsTable->rowCount();
        m_resultsTable->insertRow(row);
        m_resultsTable->setItem(row, 0, new QTableWidgetItem(fileName));
        m_resultsTable->setItem(row, 1, new QTableWidgetItem(origScrStr));
        m_resultsTable->setItem(row, 2, new QTableWidgetItem(filtScrStr));
        m_resultsTable->setItem(row, 3, new QTableWidgetItem(impStr));
    }

    if (maxCount > 1) {
        int row = m_resultsTable->rowCount();
        m_resultsTable->insertRow(row);
        
        auto* avgItem = new QTableWidgetItem(QStringLiteral("平均值"));
        avgItem->setFont(QFont("", -1, QFont::Bold));
        m_resultsTable->setItem(row, 0, avgItem);
        m_resultsTable->setItem(row, 1, new QTableWidgetItem(validOrigCount > 0 ? QString::number(totalOrigScr / validOrigCount, 'f', 4) : "--"));
        m_resultsTable->setItem(row, 2, new QTableWidgetItem(validFiltCount > 0 ? QString::number(totalFiltScr / validFiltCount, 'f', 4) : "--"));
        m_resultsTable->setItem(row, 3, new QTableWidgetItem(validImpCount > 0 ? QString::number(totalImp / validImpCount, 'f', 2) : "--"));
    }

    QWidget* singleView = m_widget ? m_widget->findChild<QWidget*>("SingleResultView") : nullptr;
    if (validOrigCount > 0 && validFiltCount > 0) {
        if (maxCount <= 1) {
            if (singleView) singleView->show();
            if (m_originalScrLabel) m_originalScrLabel->setText(validOrigCount > 0 ? QString::number(totalOrigScr / validOrigCount, 'f', 4) : "--");
            if (m_filteredScrLabel) m_filteredScrLabel->setText(validFiltCount > 0 ? QString::number(totalFiltScr / validFiltCount, 'f', 4) : "--");
            if (m_improvementLabel && validImpCount > 0) m_improvementLabel->setText(QString::number(totalImp / validImpCount, 'f', 2) + "%");
            m_summaryLabel->hide();
            m_expandLabel->hide();
            m_resultsTable->hide();
        } else {
            if (singleView) singleView->hide();
            m_summaryLabel->setText(QStringLiteral("评估完成：共处理 %1 对图像").arg(maxCount));
            m_summaryLabel->show();
            m_expandLabel->show();
            
            if (m_isExpanded) {
                m_resultsTable->show();
                m_expandLabel->setText(QStringLiteral("<a href=\"#expand\" style=\"color: #0078D7; text-decoration: none;\">▲ 收起详细列表</a>"));
            } else {
                m_resultsTable->hide();
                m_expandLabel->setText(QStringLiteral("<a href=\"#expand\" style=\"color: #0078D7; text-decoration: none;\">▼ 展开详细列表</a>"));
            }
        }
    } else {
        if (singleView) singleView->hide();
        m_summaryLabel->setText(QStringLiteral("错误：无法读取全部 %1 对图像，请检查路径。").arg(maxCount));
        m_summaryLabel->show();
        m_expandLabel->hide();
        m_resultsTable->hide();
    }

    if (m_widget) {
        m_widget->setFixedWidth(!m_resultsTable->isHidden() ? 450 : 200);
        m_widget->resize(0, 0);
        m_widget->adjustSize();
        Q_EMIT embeddedWidgetSizeUpdated();
    }
}

QJsonObject EvaluationSCRNode::save() const
{
    QJsonObject modelJson = ExecutableNodeDelegateModel::save();
    modelJson["regionIndex"] = m_regionComboBox->currentIndex();
    modelJson["isExpanded"] = m_isExpanded;
    return modelJson;
}

void EvaluationSCRNode::load(QJsonObject const &json)
{
    if (json.contains("regionIndex")) {
        m_regionComboBox->setCurrentIndex(json["regionIndex"].toInt());
    }
    m_isExpanded = json["isExpanded"].toBool(false);
    
    ExecutableNodeDelegateModel::load(json);
}

} // namespace QtNodes
