#include "InSARLogManager.h"
#include "EvaluationSCRNode.h"
#include "SARProcessor.h"
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QFileInfo>
#include <QDebug>
#include <opencv2/opencv.hpp>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonArray>
#include <QComboBox>

namespace QtNodes {

EvaluationSCRNode::EvaluationSCRNode()
{
    m_detailTableHeaders = QStringList() << QStringLiteral("文件名") << QStringLiteral("原图SCR") << QStringLiteral("滤波后SCR") << QStringLiteral("性能提升");
    m_stopFlagPtr = std::make_shared<std::atomic<bool>>(false);
    m_watcher = new QFutureWatcher<SCRResultData>(this);
    connect(m_watcher, &QFutureWatcher<SCRResultData>::finished, this, &EvaluationSCRNode::onEvaluationFinished);
    createWidget();
}

EvaluationSCRNode::~EvaluationSCRNode()
{
    if (m_stopFlagPtr) {
        *m_stopFlagPtr = true;
    }
    if (m_watcher) {
        m_watcher->waitForFinished();
    }
}

void EvaluationSCRNode::createWidget()
{
    m_widget = new QWidget();
    m_widget->setFixedWidth(240);
    auto* mainLayout = new QVBoxLayout(m_widget);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSizeConstraint(QLayout::SetFixedSize);

    mainLayout->addWidget(new QLabel(QStringLiteral("区域选择：")));
    m_regionComboBox = new QComboBox();
    m_regionComboBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_regionComboBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_regionComboBox->addItem(QStringLiteral("中心目标/周围杂波"));
    m_regionComboBox->addItem(QStringLiteral("左半目标/右半杂波"));
    mainLayout->addWidget(m_regionComboBox);

    QFrame* line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    mainLayout->addWidget(line);

    mainLayout->addWidget(new QLabel(QStringLiteral("SCR结果：")));
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

    m_originalScrLabel = new QLabel("--");
    m_filteredScrLabel = new QLabel("--");
    m_improvementLabel = new QLabel("--");
    
    singleLayout->addRow(QStringLiteral("原图SCR："), m_originalScrLabel); // 原图SCR：
    singleLayout->addRow(QStringLiteral("滤波后SCR："), m_filteredScrLabel); // 滤波后SCR：
    singleLayout->addRow(QStringLiteral("性能提升："), m_improvementLabel); // 性能提升：

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
    m_resultsTable->setHorizontalHeaderLabels({QStringLiteral("图像"), QStringLiteral("原图SCR"), QStringLiteral("滤波后SCR"), QStringLiteral("提升(%)")});
    m_resultsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    // m_resultsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_resultsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultsTable->setMinimumHeight(150);
    m_resultsTable->setVisible(false);
    tableLayout->addWidget(m_resultsTable);
    mainLayout->addLayout(tableLayout);

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

    // Width is locked via setFixedWidth in widget creation above

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

    if (isRestoring()) {
        return;
    }
    
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
    return (m_originalData != nullptr && !m_originalData->filePaths().isEmpty()) && 
           (m_filteredData != nullptr && !m_filteredData->filePaths().isEmpty());
}

void EvaluationSCRNode::collapseDetailedList()
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

void EvaluationSCRNode::execute()
{
    calculateAndDisplaySCR();
}

void EvaluationSCRNode::stopExecution()
{
    if (m_stopFlagPtr) {
        *m_stopFlagPtr = true;
    }
}

void EvaluationSCRNode::processAutomatically()
{
    if (isReady()) {
        execute();
    } else {
        setState(ExecutionState::Idle);
    }
}

void EvaluationSCRNode::onRegionChanged(int index)
{
    Q_UNUSED(index);
    if (isReady()) {
        execute();
    } else {
        updateWidgetSize();
    }
}

double EvaluationSCRNode::calculateScr(const cv::Mat& targetGray, const cv::Mat& clutterGray)
{
    return SARProcessor::CalculateSCR(targetGray, clutterGray);
}

void EvaluationSCRNode::calculateAndDisplaySCR()
{
    // 如果两端口数据不同时有效，直接返回 Idle（不进入计算，避免单端口有数据时产生 Error）
    if (!isReady()) {
        if (m_resultsTable) m_resultsTable->setRowCount(0);
        if (m_originalScrLabel) m_originalScrLabel->setText("--");
        if (m_filteredScrLabel) m_filteredScrLabel->setText("--");
        if (m_improvementLabel) m_improvementLabel->setText("--");
        if (m_summaryLabel) m_summaryLabel->setText("--");
        m_detectionResults.clear();
        setState(ExecutionState::Idle);
        return;
    }

    if (m_stopFlagPtr) {
        *m_stopFlagPtr = true;
    }
    if (m_watcher) {
        m_watcher->waitForFinished();
    }

    if (m_resultsTable) m_resultsTable->setRowCount(0);
    if (m_originalScrLabel) m_originalScrLabel->setText("--");
    if (m_filteredScrLabel) m_filteredScrLabel->setText("--");
    if (m_improvementLabel) m_improvementLabel->setText("--");
    if (m_summaryLabel) m_summaryLabel->setText("--");
    m_detectionResults.clear();

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
        updateWidgetSize();
        setState(ExecutionState::Idle);
        return;
    }

    setState(ExecutionState::Running);
    m_stopFlagPtr = std::make_shared<std::atomic<bool>>(false);

    bool hasTargetRoi = m_hasTargetRoi;
    QRectF targetRoi = m_targetRoi;
    bool hasClutterRoi = m_hasClutterRoi;
    QRectF clutterRoi = m_clutterRoi;
    int regionIndex = m_regionComboBox->currentIndex();
    auto stopFlagPtr = m_stopFlagPtr;

    QFuture<SCRResultData> future = QtConcurrent::run([=]() -> SCRResultData {
        SCRResultData data;
        data.totalCount = maxCount;

        auto getScrLocal = [=](const QString& path) -> double {
            if (path.isEmpty()) return -9999.0;
            cv::Mat mat = cv::imread(path.toLocal8Bit().constData(), cv::IMREAD_GRAYSCALE);
            if (mat.empty()) return -9999.0;
            
            cv::Rect targetRect;
            cv::Rect clutterRect;

            if (hasTargetRoi && hasClutterRoi) {
                targetRect = cv::Rect(targetRoi.x(), targetRoi.y(), targetRoi.width(), targetRoi.height());
                clutterRect = cv::Rect(clutterRoi.x(), clutterRoi.y(), clutterRoi.width(), clutterRoi.height());
            } else if (regionIndex == 0) { // 中心目标/周围杂波
                targetRect = cv::Rect(mat.cols / 4, mat.rows / 4, mat.cols / 2, mat.rows / 2);
                clutterRect = cv::Rect(0, 0, mat.cols / 4, mat.rows / 4);
            } else { // 左半目标/右半杂波
                targetRect = cv::Rect(0, 0, mat.cols / 2, mat.rows);
                clutterRect = cv::Rect(mat.cols / 2, 0, mat.cols / 2, mat.rows);
            }
            
            cv::Rect bounds(0, 0, mat.cols, mat.rows);
            targetRect &= bounds;
            clutterRect &= bounds;
            
            if (targetRect.width < 2 || targetRect.height < 2 || clutterRect.width < 2 || clutterRect.height < 2) {
                return -9999.0;
            }
            
            cv::Mat targetMat = mat(targetRect);
            cv::Mat clutterMat = mat(clutterRect);
            
            double finalScr = calculateScr(targetMat, clutterMat);
            return finalScr;
        };

        for (int i = 0; i < maxCount; ++i) {
            if (*stopFlagPtr) {
                data.isCancelled = true;
                break;
            }

            QString origPath = i < origPaths.size() ? origPaths[i] : "";
            QString filtPath = i < filtPaths.size() ? filtPaths[i] : "";

            double origScr = getScrLocal(origPath);
            double filtScr = getScrLocal(filtPath);

            SCRSingleResult singleRes;
            singleRes.fileName = origPath.isEmpty() ? QFileInfo(filtPath).fileName() : QFileInfo(origPath).fileName();
            singleRes.origScr = origScr > -9000.0 ? QString::number(origScr, 'f', 4) : "--";
            singleRes.filtScr = filtScr > -9000.0 ? QString::number(filtScr, 'f', 4) : "--";
            singleRes.imp = "--";
            singleRes.success = true;

            if (origScr > -9000.0) {
                data.totalOrigScr += origScr;
                data.validOrigCount++;
            }
            if (filtScr > -9000.0) {
                data.totalFiltScr += filtScr;
                data.validFiltCount++;
            }
            
            if (origScr > -9000.0 && filtScr > -9000.0 && origScr != 0) {
                double imp = (filtScr - origScr) / std::abs(origScr) * 100.0;
                data.totalImp += imp;
                data.validImpCount++;
                singleRes.imp = QString::number(imp, 'f', 2);
            }

            data.results.append(singleRes);
        }

        if (!(data.validOrigCount > 0 && data.validFiltCount > 0)) {
            QString errorMsg = QStringLiteral("错误：无法读取全部 %1 对图像。\n");
            if (maxCount > 0) {
                QString origPath = origPaths.isEmpty() ? "空路径" : origPaths[0];
                QString filtPath = filtPaths.isEmpty() ? "空路径" : filtPaths[0];
                if (origPath == "空路径" || cv::imread(origPath.toLocal8Bit().constData(), cv::IMREAD_GRAYSCALE).empty()) 
                    errorMsg += QString("原图失败: %1\n").arg(origPath);
                if (filtPath == "空路径" || cv::imread(filtPath.toLocal8Bit().constData(), cv::IMREAD_GRAYSCALE).empty()) 
                    errorMsg += QString("滤波图失败: %1").arg(filtPath);
            }
            data.errorMsg = errorMsg.arg(maxCount);
        }

        return data;
    });

    m_watcher->setFuture(future);
}

void EvaluationSCRNode::onEvaluationFinished()
{
    if (!m_watcher) return;

    if (discardObsoleteAutomaticExecution()) {
        return;
    }
    
    SCRResultData data = m_watcher->result();
    if (data.isCancelled) {
        setState(ExecutionState::Stopped);
        Q_EMIT executionStopped();
        Q_EMIT computingFinished();
        return;
    }
    
    if (m_resultsTable) m_resultsTable->setRowCount(0);
    m_detectionResults.clear();

    int count = data.results.size();
    for (int i = 0; i < count; ++i) {
        const auto& singleRes = data.results[i];
        int row = m_resultsTable->rowCount();
        m_resultsTable->insertRow(row);
        m_resultsTable->setItem(row, 0, new QTableWidgetItem(singleRes.fileName));
        m_resultsTable->setItem(row, 1, new QTableWidgetItem(singleRes.origScr));
        m_resultsTable->setItem(row, 2, new QTableWidgetItem(singleRes.filtScr));
        m_resultsTable->setItem(row, 3, new QTableWidgetItem(singleRes.imp));
        
        m_detectionResults.append(QStringList() << singleRes.fileName << singleRes.origScr << singleRes.filtScr << singleRes.imp);
    }

    if (data.totalCount > 1) {
        QString origStr = data.validOrigCount > 0 ? QString::number(data.totalOrigScr / data.validOrigCount, 'f', 4) : "--";
        QString filtStr = data.validFiltCount > 0 ? QString::number(data.totalFiltScr / data.validFiltCount, 'f', 4) : "--";
        QString impStr = data.validImpCount > 0 ? QString::number(data.totalImp / data.validImpCount, 'f', 2) : "--";

        int row = m_resultsTable->rowCount();
        m_resultsTable->insertRow(row);
        
        auto* avgItem = new QTableWidgetItem(QStringLiteral("平均值"));
        avgItem->setFont(QFont("", -1, QFont::Bold));
        m_resultsTable->setItem(row, 0, avgItem);
        m_resultsTable->setItem(row, 1, new QTableWidgetItem(origStr));
        m_resultsTable->setItem(row, 2, new QTableWidgetItem(filtStr));
        m_resultsTable->setItem(row, 3, new QTableWidgetItem(impStr));
    }

    QWidget* singleView = m_widget ? m_widget->findChild<QWidget*>("SingleResultView") : nullptr;
    if (data.validOrigCount > 0 && data.validFiltCount > 0) {
        if (data.totalCount <= 1) {
            if (singleView) singleView->show();
            QString origStr = data.validOrigCount > 0 ? QString::number(data.totalOrigScr / data.validOrigCount, 'f', 4) : "--";
            QString filtStr = data.validFiltCount > 0 ? QString::number(data.totalFiltScr / data.validFiltCount, 'f', 4) : "--";
            QString impStr = data.validImpCount > 0 ? QString::number(data.totalImp / data.validImpCount, 'f', 2) : "--";
            if (m_originalScrLabel) m_originalScrLabel->setText(origStr);
            if (m_filteredScrLabel) m_filteredScrLabel->setText(filtStr);
            if (m_improvementLabel && data.validImpCount > 0) m_improvementLabel->setText(impStr + "%");
            if (m_summaryLabel) m_summaryLabel->hide();
            if (m_expandLabel) m_expandLabel->hide();
            if (m_resultsTable) m_resultsTable->hide();
        } else {
            if (singleView) singleView->hide();
            m_summaryLabel->setText(QStringLiteral("评估完成：共处理 %1 对图像").arg(data.totalCount));
            m_summaryLabel->show();
            m_expandLabel->show();
            
            if (m_isExpanded) {
                m_resultsTable->show();
                m_expandLabel->setText(QStringLiteral("<a href=\"#expand\" style=\"color: #0078D7; text-decoration: none;\">▲ 收起详细列表</a>"));
            } else {
                m_resultsTable->hide();
                m_expandLabel->setText(QStringLiteral("<a href=\"#expand\" style=\"color: #0078D7; text-decoration: none;\">▼ 展开详细列表</a>"));
            }
            
            QString impStr = data.validImpCount > 0 ? QString::number(data.totalImp / data.validImpCount, 'f', 2) : "--";
            if (m_summaryLabel) {
                m_summaryLabel->setText(QString("所有图像平均提升: %1%").arg(impStr));
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
    
    triggerVisualUpdate();

    updateWidgetSize();

    Q_EMIT dataUpdated(0);
}

void EvaluationSCRNode::updateWidgetSize()
{
    if (m_widget) {
        int targetWidth = (m_resultsTable && !m_resultsTable->isHidden()) ? 450 : 240;
        m_widget->setFixedWidth(targetWidth);
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
    
    modelJson["hasTargetRoi"] = m_hasTargetRoi;
    if (m_hasTargetRoi) {
        QJsonObject tr;
        tr["x"] = m_targetRoi.x(); tr["y"] = m_targetRoi.y();
        tr["w"] = m_targetRoi.width(); tr["h"] = m_targetRoi.height();
        modelJson["targetRoi"] = tr;
    }
    modelJson["hasClutterRoi"] = m_hasClutterRoi;
    if (m_hasClutterRoi) {
        QJsonObject cr;
        cr["x"] = m_clutterRoi.x(); cr["y"] = m_clutterRoi.y();
        cr["w"] = m_clutterRoi.width(); cr["h"] = m_clutterRoi.height();
        modelJson["clutterRoi"] = cr;
    }

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

    if (m_originalScrLabel) modelJson["origScr"] = m_originalScrLabel->text();
    if (m_filteredScrLabel) modelJson["filtScr"] = m_filteredScrLabel->text();
    if (m_improvementLabel) modelJson["imp"] = m_improvementLabel->text();
    if (m_summaryLabel && !m_summaryLabel->isHidden()) modelJson["summary"] = m_summaryLabel->text();

    return modelJson;
}

void EvaluationSCRNode::load(QJsonObject const &json)
{
    if (json.contains("regionIndex")) {
        m_regionComboBox->setCurrentIndex(json["regionIndex"].toInt());
    }
    m_isExpanded = json["isExpanded"].toBool(false);

    m_hasTargetRoi = json["hasTargetRoi"].toBool(false);
    if (m_hasTargetRoi && json.contains("targetRoi")) {
        QJsonObject tr = json["targetRoi"].toObject();
        m_targetRoi = QRectF(tr["x"].toDouble(), tr["y"].toDouble(), tr["w"].toDouble(), tr["h"].toDouble());
    }
    
    m_hasClutterRoi = json["hasClutterRoi"].toBool(false);
    if (m_hasClutterRoi && json.contains("clutterRoi")) {
        QJsonObject cr = json["clutterRoi"].toObject();
        m_clutterRoi = QRectF(cr["x"].toDouble(), cr["y"].toDouble(), cr["w"].toDouble(), cr["h"].toDouble());
    }

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

    if (json.contains("origScr") && m_originalScrLabel) m_originalScrLabel->setText(json["origScr"].toString());
    if (json.contains("filtScr") && m_filteredScrLabel) m_filteredScrLabel->setText(json["filtScr"].toString());
    if (json.contains("imp") && m_improvementLabel) m_improvementLabel->setText(json["imp"].toString());
    
    if (json.contains("summary") && m_summaryLabel) {
        m_summaryLabel->setText(json["summary"].toString());
    }

    ExecutableNodeDelegateModel::load(json);
}

QStringList EvaluationSCRNode::previewImagePaths() const
{
    if (m_originalData) {
        return m_originalData->filePaths();
    } else if (m_filteredData) {
        return m_filteredData->filePaths();
    }
    return QStringList();
}

bool EvaluationSCRNode::validateAndRestoreOutput()
{
    return true;
}

void EvaluationSCRNode::processTargetRoiSelection(const QRectF& sceneRect, int imageIndex)
{
    m_hasTargetRoi = true;
    m_targetRoi = sceneRect;
    calculateAndDisplaySCR();
    triggerVisualUpdate();
}

void EvaluationSCRNode::processClutterRoiSelection(const QRectF& sceneRect, int imageIndex)
{
    m_hasClutterRoi = true;
    m_clutterRoi = sceneRect;
    calculateAndDisplaySCR();
    triggerVisualUpdate();
}

void EvaluationSCRNode::clearTargetRoiSelection()
{
    m_hasTargetRoi = false;
    m_targetRoi = QRectF();
    calculateAndDisplaySCR();
    triggerVisualUpdate();
}

void EvaluationSCRNode::clearClutterRoiSelection()
{
    m_hasClutterRoi = false;
    m_clutterRoi = QRectF();
    calculateAndDisplaySCR();
    triggerVisualUpdate();
}

} // namespace QtNodes
