
#include "EvaluationENLNode.h"
#include "SARProcessor.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonArray>

#include "InSARLogManager.h"
namespace QtNodes {

EvaluationENLNode::EvaluationENLNode()
{
    m_stopFlagPtr = std::make_shared<std::atomic<bool>>(false);
    m_watcher = new QFutureWatcher<ENLResultData>(this);
    connect(m_watcher, &QFutureWatcher<ENLResultData>::finished, this, &EvaluationENLNode::onEvaluationFinished);
    createWidget();
}

EvaluationENLNode::~EvaluationENLNode()
{
    if (m_stopFlagPtr) {
        *m_stopFlagPtr = true;
    }
    if (m_watcher) {
        m_watcher->waitForFinished();
    }
}

void EvaluationENLNode::createWidget()
{
    m_widget = new QWidget();
    m_widget->setFixedWidth(180);
    auto* mainLayout = new QVBoxLayout(m_widget);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);

    mainLayout->addWidget(new QLabel(QStringLiteral("区域选择：")));
    m_regionComboBox = new QComboBox();
    m_regionComboBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_regionComboBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_regionComboBox->addItem("全部");
    m_regionComboBox->addItem("中间区域");
    mainLayout->addWidget(m_regionComboBox);

    QFrame* line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    mainLayout->addWidget(line);

    mainLayout->addWidget(new QLabel(QStringLiteral("ENL/EPI结果：")));
    auto* tableLayout = new QVBoxLayout();
    tableLayout->setContentsMargins(0, 0, 0, 0);
    tableLayout->setSpacing(2);

    m_simpleResultWidget = new QWidget();
    auto* simpleLayout = new QVBoxLayout(m_simpleResultWidget);
    simpleLayout->setContentsMargins(0, 0, 0, 0);
    simpleLayout->setSpacing(2);

    QWidget* singleResultView = new QWidget();
    singleResultView->setObjectName("SingleResultView");
    auto* singleLayout = new QFormLayout(singleResultView);
    singleLayout->setContentsMargins(10, 4, 10, 4);

    m_originalEnlLabel = new QLabel("--");
    m_filteredEnlLabel = new QLabel("--");
    m_epiLabel = new QLabel("--");

    singleLayout->addRow(QStringLiteral("原图ENL："), m_originalEnlLabel);
    singleLayout->addRow(QStringLiteral("滤波后ENL："), m_filteredEnlLabel);
    singleLayout->addRow(QStringLiteral("EPI："), m_epiLabel);

    simpleLayout->addWidget(singleResultView);

    m_summaryLabel = new QLabel("--");
    m_summaryLabel->setWordWrap(false);
    m_summaryLabel->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
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
    m_resultsTable->setHorizontalHeaderLabels({QStringLiteral("图像"), "原图ENL", "滤波后ENL", "EPI"});
    m_resultsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    // m_resultsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_resultsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultsTable->setMinimumHeight(150);
    m_resultsTable->setVisible(false);
    tableLayout->addWidget(m_resultsTable);

    connect(m_expandLabel, &QLabel::linkActivated, this, [this](const QString &link) {
        if (link == "#expand") {
            m_isExpanded = !m_isExpanded;
            m_resultsTable->setVisible(m_isExpanded);
            m_expandLabel->setText(m_isExpanded ? 
                QStringLiteral("<a href=\"#expand\" style=\"color: #0078D7; text-decoration: none;\">▲ 收起详细列表</a>") : 
                QStringLiteral("<a href=\"#expand\" style=\"color: #0078D7; text-decoration: none;\">▼ 展开详细列表</a>"));
            updateWidgetSize();
        }
    });
    mainLayout->addLayout(tableLayout);

    // Width is locked via setFixedWidth in widget creation above

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

    if (isRestoring()) {
        return;
    }
    
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

void EvaluationENLNode::collapseDetailedList()
{
    if (m_isExpanded) {
        m_isExpanded = false;
        if (m_resultsTable) {
            m_resultsTable->setVisible(false);
        }
        if (m_expandLabel) {
            m_expandLabel->setText(QStringLiteral("<a href=\"#expand\" style=\"color: #0078D7; text-decoration: none;\">▼ 展开详细列表</a>"));
        }
        updateWidgetSize();
    }
}

void EvaluationENLNode::execute()
{
    calculateAndDisplayENL();
}

void EvaluationENLNode::stopExecution()
{
    if (m_stopFlagPtr) {
        *m_stopFlagPtr = true;
    }
}

void EvaluationENLNode::processAutomatically()
{
    if (isReady()) {
        execute();
    } else {
        setState(ExecutionState::Idle);
    }
}

void EvaluationENLNode::onRegionChanged(int index)
{
    Q_UNUSED(index);
    if (isReady()) {
        execute();
    } else {
        updateWidgetSize();
    }
}

double EvaluationENLNode::calculateENL(const cv::Mat& roiGray)
{
    return SARProcessor::CalculateENL(roiGray);
}

double EvaluationENLNode::calculateEPI(const cv::Mat& orig, const cv::Mat& filtered)
{
    return SARProcessor::CalculateEPI(orig, filtered);
}

void EvaluationENLNode::calculateAndDisplayENL()
{
    if (m_stopFlagPtr) {
        *m_stopFlagPtr = true;
    }
    if (m_watcher) {
        m_watcher->waitForFinished();
    }

    if (m_resultsTable) m_resultsTable->setRowCount(0);
    if (m_originalEnlLabel) m_originalEnlLabel->setText("--");
    if (m_filteredEnlLabel) m_filteredEnlLabel->setText("--");
    if (m_epiLabel) m_epiLabel->setText("--");
    if (m_summaryLabel) m_summaryLabel->setText("--");

    if (!m_originalData || !m_filteredData || m_originalData->filePaths().isEmpty() || m_filteredData->filePaths().isEmpty()) {
        QWidget* singleView = m_widget ? m_widget->findChild<QWidget*>("SingleResultView") : nullptr;
        if (singleView) singleView->show();
        if (m_summaryLabel) m_summaryLabel->hide();
        if (m_expandLabel) m_expandLabel->hide();
        if (m_resultsTable) m_resultsTable->hide();
        updateWidgetSize();
        setState(ExecutionState::Idle);
        return;
    }
    
    QStringList origPaths = m_originalData->filePaths();
    QStringList filtPaths = m_filteredData->filePaths();
    
    if (origPaths.size() != filtPaths.size()) {
        QWidget* singleView = m_widget ? m_widget->findChild<QWidget*>("SingleResultView") : nullptr;
        if (singleView) singleView->hide();
        m_summaryLabel->setText(QStringLiteral("错误：输入数量不一致 (原图: %1, 滤波: %2)").arg(origPaths.size()).arg(filtPaths.size()));
        m_summaryLabel->show();
        m_expandLabel->hide();
        m_resultsTable->hide();
        InSARLogManager::LogWarning("UI", QStringLiteral("原图和滤波后图像的数量不一致，无法进行批量评估！"));
        QMessageBox::warning(nullptr, QStringLiteral("警告"), QStringLiteral("原图和滤波后图像的数量不一致，无法进行批量评估！"));
        updateWidgetSize();
        setState(ExecutionState::Error);
        return;
    }

    setState(ExecutionState::Running);
    m_stopFlagPtr = std::make_shared<std::atomic<bool>>(false);

    bool hasCustomRoi = m_hasCustomRoi;
    cv::Rect customRoi = m_customRoi;
    int regionIndex = m_regionComboBox->currentIndex();
    auto stopFlagPtr = m_stopFlagPtr;

    QFuture<ENLResultData> future = QtConcurrent::run([=]() -> ENLResultData {
        ENLResultData data;
        data.totalCount = origPaths.size();
        
        double totalOrigEnl = 0.0;
        double totalFiltEnl = 0.0;
        double totalEPI = 0.0;
        
        for (int i = 0; i < data.totalCount; ++i) {
            if (*stopFlagPtr) {
                data.isCancelled = true;
                break;
            }
            
            cv::Mat origMat = cv::imread(origPaths[i].toLocal8Bit().constData(), cv::IMREAD_GRAYSCALE);
            cv::Mat filtMat = cv::imread(filtPaths[i].toLocal8Bit().constData(), cv::IMREAD_GRAYSCALE);
            
            ENLSingleResult singleRes;
            singleRes.fileName = QFileInfo(filtPaths[i]).fileName();
            singleRes.success = false;
            
            if (origMat.empty() || filtMat.empty()) {
                data.results.append(singleRes);
                continue;
            }
            
            cv::Rect roiOrig(0, 0, origMat.cols, origMat.rows);
            cv::Rect roiFilt(0, 0, filtMat.cols, filtMat.rows);
            
            if (hasCustomRoi) {
                roiOrig = customRoi;
                roiFilt = customRoi;
                roiOrig &= cv::Rect(0, 0, origMat.cols, origMat.rows);
                roiFilt &= cv::Rect(0, 0, filtMat.cols, filtMat.rows);
            } else if (regionIndex == 1) { // 中间区域
                roiOrig = cv::Rect(origMat.cols / 4, origMat.rows / 4, origMat.cols / 2, origMat.rows / 2);
                roiFilt = cv::Rect(filtMat.cols / 4, filtMat.rows / 4, filtMat.cols / 2, filtMat.rows / 2);
            }
            
            if (roiOrig.width <= 0 || roiOrig.height <= 0 || roiFilt.width <= 0 || roiFilt.height <= 0) {
                data.results.append(singleRes);
                continue;
            }
            
            double origEnl = calculateENL(origMat(roiOrig));
            double filtEnl = calculateENL(filtMat(roiFilt));
            double epi = calculateEPI(origMat(roiOrig), filtMat(roiFilt));
            
            totalOrigEnl += origEnl;
            totalFiltEnl += filtEnl;
            totalEPI += epi;
            
            singleRes.origEnl = QString::number(origEnl, 'f', 4);
            singleRes.filtEnl = QString::number(filtEnl, 'f', 4);
            singleRes.epi = QString::number(epi, 'f', 4);
            singleRes.success = true;
            
            data.results.append(singleRes);
            data.validCount++;
        }
        
        if (data.validCount > 0) {
            data.avgOrigEnl = totalOrigEnl / data.validCount;
            data.avgFiltEnl = totalFiltEnl / data.validCount;
            data.avgEpi = totalEPI / data.validCount;
        } else {
            QString errorMsg = QStringLiteral("错误：无法读取全部 %1 对图像。\n");
            if (data.totalCount > 0) {
                QString origPath = origPaths.isEmpty() ? "空路径" : origPaths[0];
                QString filtPath = filtPaths.isEmpty() ? "空路径" : filtPaths[0];
                if (origPath == "空路径" || cv::imread(origPath.toLocal8Bit().constData(), cv::IMREAD_GRAYSCALE).empty()) 
                    errorMsg += QString("原图失败: %1\n").arg(origPath);
                if (filtPath == "空路径" || cv::imread(filtPath.toLocal8Bit().constData(), cv::IMREAD_GRAYSCALE).empty()) 
                    errorMsg += QString("滤波图失败: %1").arg(filtPath);
            }
            data.errorMsg = errorMsg.arg(data.totalCount);
        }
        
        return data;
    });

    m_watcher->setFuture(future);
}

void EvaluationENLNode::onEvaluationFinished()
{
    if (!m_watcher) return;

    if (discardObsoleteAutomaticExecution()) {
        return;
    }
    
    ENLResultData data = m_watcher->result();
    if (data.isCancelled) {
        setState(ExecutionState::Stopped);
        Q_EMIT executionStopped();
        Q_EMIT computingFinished();
        return;
    }
    
    m_savedResults.clear();
    if (m_resultsTable) m_resultsTable->setRowCount(0);
    
    int count = data.results.size();
    for (int i = 0; i < count; ++i) {
        const auto& singleRes = data.results[i];
        if (!singleRes.success) continue;
        
        int row = m_resultsTable->rowCount();
        m_resultsTable->insertRow(row);
        m_resultsTable->setItem(row, 0, new QTableWidgetItem(singleRes.fileName));
        m_resultsTable->setItem(row, 1, new QTableWidgetItem(singleRes.origEnl));
        m_resultsTable->setItem(row, 2, new QTableWidgetItem(singleRes.filtEnl));
        m_resultsTable->setItem(row, 3, new QTableWidgetItem(singleRes.epi));
        
        m_savedResults.append({
            singleRes.fileName,
            singleRes.origEnl,
            singleRes.filtEnl,
            singleRes.epi
        });
    }
    
    if (data.validCount > 1) {
        int row = m_resultsTable->rowCount();
        m_resultsTable->insertRow(row);
        
        auto* avgItem = new QTableWidgetItem(QStringLiteral("平均值"));
        avgItem->setFont(QFont("", -1, QFont::Bold));
        m_resultsTable->setItem(row, 0, avgItem);
        m_resultsTable->setItem(row, 1, new QTableWidgetItem(QString::number(data.avgOrigEnl, 'f', 4)));
        m_resultsTable->setItem(row, 2, new QTableWidgetItem(QString::number(data.avgFiltEnl, 'f', 4)));
        m_resultsTable->setItem(row, 3, new QTableWidgetItem(QString::number(data.avgEpi, 'f', 4)));
    }
    
    QWidget* singleView = m_widget ? m_widget->findChild<QWidget*>("SingleResultView") : nullptr;
    
    if (data.validCount > 0) {
        if (data.totalCount == 1) {
            if (singleView) singleView->show();
            if (m_originalEnlLabel) m_originalEnlLabel->setText(QString::number(data.avgOrigEnl, 'f', 4));
            if (m_filteredEnlLabel) m_filteredEnlLabel->setText(QString::number(data.avgFiltEnl, 'f', 4));
            if (m_epiLabel) m_epiLabel->setText(QString::number(data.avgEpi, 'f', 4));
            m_summaryLabel->hide();
            m_expandLabel->hide();
            m_resultsTable->hide();
        } else {
            if (singleView) singleView->hide();
            m_summaryLabel->setText(QStringLiteral("评估完成：共处理 %1 对图像").arg(data.validCount));
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
        finishExecution();
    } else {
        if (singleView) singleView->hide();
        m_summaryLabel->setText(data.errorMsg);
        m_summaryLabel->show();
        m_expandLabel->hide();
        m_resultsTable->hide();
        setState(ExecutionState::Error);
    }
    
    updateWidgetSize();
    
    Q_EMIT dataUpdated(0);
}


void EvaluationENLNode::processRoiSelection(const QRectF& sceneRect, int imageIndex)
{
    m_hasCustomRoi = true;
    m_customRoi = cv::Rect(sceneRect.x(), sceneRect.y(), sceneRect.width(), sceneRect.height());
    
    // Force combobox back to "全部" visual state so user knows it's custom
    m_regionComboBox->blockSignals(true);
    m_regionComboBox->setCurrentIndex(0);
    m_regionComboBox->blockSignals(false);
    
    calculateAndDisplayENL();
}

void EvaluationENLNode::clearRoiSelection()
{
    m_hasCustomRoi = false;
    calculateAndDisplayENL();
}

QStringList EvaluationENLNode::detailTableHeaders() const
{
    return {QStringLiteral("图像"), QStringLiteral("原图ENL"), QStringLiteral("滤波后ENL"), QStringLiteral("EPI")};
}

QList<QStringList> EvaluationENLNode::detectionResults() const
{
    return m_savedResults;
}

QStringList EvaluationENLNode::previewImagePaths() const
{
    if (m_filteredData) {
        return m_filteredData->filePaths();
    } else if (m_originalData) {
        return m_originalData->filePaths();
    }
    return QStringList();
}

QJsonObject EvaluationENLNode::save() const
{
    QJsonObject modelJson = ExecutableNodeDelegateModel::save();
    modelJson["regionIndex"] = m_regionComboBox->currentIndex();
    modelJson["isExpanded"] = m_isExpanded;

    QJsonArray resultsArray;
    if (m_resultsTable) {
        for (int row = 0; row < m_resultsTable->rowCount(); ++row) {
            QJsonObject rowObj;
            rowObj["col0"] = m_resultsTable->item(row, 0) ? m_resultsTable->item(row, 0)->text() : "";
            rowObj["col1"] = m_resultsTable->item(row, 1) ? m_resultsTable->item(row, 1)->text() : "";
            rowObj["col2"] = m_resultsTable->item(row, 2) ? m_resultsTable->item(row, 2)->text() : "";
            rowObj["col3"] = m_resultsTable->item(row, 3) ? m_resultsTable->item(row, 3)->text() : "";
            resultsArray.append(rowObj);
        }
    }
    modelJson["results"] = resultsArray;

    if (m_originalEnlLabel) modelJson["origEnl"] = m_originalEnlLabel->text();
    if (m_filteredEnlLabel) modelJson["filtEnl"] = m_filteredEnlLabel->text();
    if (m_epiLabel) modelJson["epi"] = m_epiLabel->text();
    if (m_summaryLabel && !m_summaryLabel->isHidden()) modelJson["summary"] = m_summaryLabel->text();

    return modelJson;
}

void EvaluationENLNode::load(QJsonObject const &json)
{
    if (json.contains("regionIndex")) {
        m_regionComboBox->setCurrentIndex(json["regionIndex"].toInt());
    }
    m_isExpanded = json["isExpanded"].toBool(false);

    if (json.contains("results") && m_resultsTable) {
        QJsonArray resultsArray = json["results"].toArray();
        m_resultsTable->setRowCount(0);
        for (int i = 0; i < resultsArray.size(); ++i) {
            QJsonObject rowObj = resultsArray[i].toObject();
            int row = m_resultsTable->rowCount();
            m_resultsTable->insertRow(row);
            
            QString col0Text = rowObj["col0"].toString();
            if (col0Text == QStringLiteral("平均值")) {
                auto* avgItem = new QTableWidgetItem(col0Text);
                avgItem->setFont(QFont("", -1, QFont::Bold));
                m_resultsTable->setItem(row, 0, avgItem);
            } else {
                m_resultsTable->setItem(row, 0, new QTableWidgetItem(col0Text));
            }
            m_resultsTable->setItem(row, 1, new QTableWidgetItem(rowObj["col1"].toString()));
            m_resultsTable->setItem(row, 2, new QTableWidgetItem(rowObj["col2"].toString()));
            m_resultsTable->setItem(row, 3, new QTableWidgetItem(rowObj["col3"].toString()));
        }
    }

    if (json.contains("origEnl") && m_originalEnlLabel) m_originalEnlLabel->setText(json["origEnl"].toString());
    if (json.contains("filtEnl") && m_filteredEnlLabel) m_filteredEnlLabel->setText(json["filtEnl"].toString());
    if (json.contains("epi") && m_epiLabel) m_epiLabel->setText(json["epi"].toString());
    
    if (json.contains("summary") && m_summaryLabel) {
        m_summaryLabel->setText(json["summary"].toString());
    }

    ExecutableNodeDelegateModel::load(json);

    if (json.contains("results")) {
        QJsonArray resultsArray = json["results"].toArray();
        int totalCount = resultsArray.size();
        if (totalCount > 0 && resultsArray.last().toObject()["col0"].toString() == QStringLiteral("平均值")) {
            totalCount--;
        }
        
        QWidget* singleView = m_widget ? m_widget->findChild<QWidget*>("SingleResultView") : nullptr;
        if (totalCount == 1) {
            if (singleView) singleView->show();
            if (m_summaryLabel) m_summaryLabel->hide();
            if (m_expandLabel) m_expandLabel->hide();
            if (m_resultsTable) m_resultsTable->hide();
        } else if (totalCount > 1) {
            if (singleView) singleView->hide();
            if (m_summaryLabel) m_summaryLabel->show();
            if (m_expandLabel) m_expandLabel->show();
            
            if (m_isExpanded) {
                if (m_resultsTable) m_resultsTable->show();
                if (m_expandLabel) m_expandLabel->setText(QStringLiteral("<a href=\"#expand\" style=\"color: #0078D7; text-decoration: none;\">▲ 收起详细列表</a>"));
            } else {
                if (m_resultsTable) m_resultsTable->hide();
                if (m_expandLabel) m_expandLabel->setText(QStringLiteral("<a href=\"#expand\" style=\"color: #0078D7; text-decoration: none;\">▼ 展开详细列表</a>"));
            }
        }
        
        updateWidgetSize();
    }
}

bool EvaluationENLNode::validateAndRestoreOutput()
{
    return true;
}

void EvaluationENLNode::updateWidgetSize()
{
    if (m_widget) {
        int targetWidth = (m_resultsTable && !m_resultsTable->isHidden()) ? 450 : 180;
        m_widget->setFixedWidth(targetWidth);
        m_widget->resize(0, 0);
        m_widget->adjustSize();
        Q_EMIT embeddedWidgetSizeUpdated();
    }
}

} // namespace QtNodes
