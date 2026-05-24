
#include "TargetDetectionNode.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QDir>
#include <QMessageBox>
#include <QApplication>
#include <QDebug>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>

namespace QtNodes {

TargetDetectionNode::TargetDetectionNode()
    : ExecutableNodeDelegateModel()
    , _widget(nullptr)
    , m_inputImageLabel(nullptr)
    , m_modelComboBox(nullptr)
    , m_thresholdEdit(nullptr)
    , m_simpleResultWidget(nullptr)
    , m_resultLabel(nullptr)
    , m_probabilityLabel(nullptr)
    , m_summaryLabel(nullptr)
    , m_expandLabel(nullptr)
    , m_isExpanded(false)
    , m_resultsTable(nullptr)
    , m_statusLabel(nullptr)
    , m_inputData(nullptr)
    , m_outputData(nullptr)
    , m_task(nullptr)
{
    setExecutionMode(ExecutionMode::Automatic);
}

TargetDetectionNode::~TargetDetectionNode()
{
    stopExecution();
}

unsigned int TargetDetectionNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 1;
    return 0;
}

NodeDataType TargetDetectionNode::dataType(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    return NodeDataType{"image_info", "Image Info"};
}

bool TargetDetectionNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return true;
}

QString TargetDetectionNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) {
        return QStringLiteral("输入图像");
    } else {
        return QStringLiteral("原图输出");
    }
}

void TargetDetectionNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    Q_UNUSED(port);
    m_inputData = std::dynamic_pointer_cast<ImageInfoData>(data);

    if (m_inputImageLabel) {
        if (m_inputData && !m_inputData->filePath().isEmpty()) {
            QFileInfo fi(m_inputData->filePath());
            m_inputImageLabel->setText(fi.fileName());
        } else {
            m_inputImageLabel->setText("");
            // Clear UI results when disconnected
            if (m_resultLabel) m_resultLabel->setText("--");
            if (m_probabilityLabel) m_probabilityLabel->setText("--");
            if (m_summaryLabel) {
                m_summaryLabel->setText("--");
                m_summaryLabel->hide();
            }
            if (m_resultsTable) {
                m_resultsTable->setRowCount(0);
                m_resultsTable->hide();
            }
            if (m_expandLabel) m_expandLabel->hide();
            QWidget* singleView = _widget ? _widget->findChild<QWidget*>("SingleResultView") : nullptr;
            if (singleView) singleView->show();
            m_savedResults.clear();
        }
    }

    ExecutableNodeDelegateModel::setInData(data, port);
}

std::shared_ptr<NodeData> TargetDetectionNode::outData(PortIndex port)
{
    Q_UNUSED(port);
    return m_outputData;
}

QWidget* TargetDetectionNode::embeddedWidget()
{
    if (!_widget) {
        createWidget();
    }
    return _widget;
}

void TargetDetectionNode::createWidget()
{
    _widget = new QWidget();
    _widget->setObjectName("NodeEmbeddedWidget");
    _widget->setMinimumWidth(260);

    auto* layout = new QVBoxLayout(_widget);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    layout->setSizeConstraint(QLayout::SetFixedSize);

    // Input image label
    m_inputImageLabel = new QLabel("");
    m_inputImageLabel->setWordWrap(true);
    if (m_inputData && !m_inputData->filePath().isEmpty()) {
        QFileInfo fi(m_inputData->filePath());
        m_inputImageLabel->setText(fi.fileName());
    }
    layout->addWidget(m_inputImageLabel);

    // Model Selection
    auto* modelLayout = new QHBoxLayout();
    modelLayout->addWidget(new QLabel(QStringLiteral("模型选择：")));
    m_modelComboBox = new QComboBox();
    m_modelComboBox->addItem("SAR Ship Model 0429", QDir::currentPath() + "/sar_ship_model0429.onnx");
    m_selectedModelPath = m_modelComboBox->currentData().toString();
    // Helper to invalidate node state when parameters change
    auto invalidateNodeData = [this]() {
        if (m_outputData) m_outputData.reset();
        int outCount = nPorts(PortType::Out);
        for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
        invalidateExecution();
    };

    connect(m_modelComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, invalidateNodeData](int) {
        QString newPath = m_modelComboBox->currentData().toString();
        if (m_selectedModelPath != newPath) {
            if (!confirmParameterChange()) {
                QSignalBlocker blocker(m_modelComboBox);
                int oldIndex = m_modelComboBox->findData(m_selectedModelPath);
                if (oldIndex >= 0) m_modelComboBox->setCurrentIndex(oldIndex);
                return;
            }
            m_selectedModelPath = newPath;
            invalidateNodeData();
        }
    });
    modelLayout->addWidget(m_modelComboBox);
    layout->addLayout(modelLayout);

    // Confidence
    auto* confLayout = new QHBoxLayout();
    confLayout->addWidget(new QLabel(QStringLiteral("置信度：")));
    m_thresholdEdit = new QLineEdit();
    m_thresholdEdit->setText(QString::number(m_thresholdValue, 'f', 2));
    connect(m_thresholdEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() { 
        bool ok;
        float val = m_thresholdEdit->text().toFloat(&ok);
        if (ok && m_thresholdValue != val) {
            if (!confirmParameterChange()) {
                m_thresholdEdit->setText(QString::number(m_thresholdValue, 'f', 2));
                return;
            }
            m_thresholdValue = val;
            invalidateNodeData();
        } else if (!ok) {
            m_thresholdEdit->setText(QString::number(m_thresholdValue, 'f', 2));
        }
    });
    confLayout->addWidget(m_thresholdEdit);
    layout->addLayout(confLayout);

    // Separator line
    QFrame* line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line);

    // Output Results Labels
    layout->addWidget(new QLabel(QStringLiteral("检查结果：")));

    m_simpleResultWidget = new QWidget();
    auto* simpleLayout = new QVBoxLayout(m_simpleResultWidget);
    simpleLayout->setContentsMargins(0, 0, 0, 0);
    simpleLayout->setSpacing(2);

    QWidget* singleResultView = new QWidget();
    singleResultView->setObjectName("SingleResultView");
    auto* singleLayout = new QVBoxLayout(singleResultView);
    singleLayout->setContentsMargins(0, 0, 0, 0);
    singleLayout->setSpacing(2);

    auto* resultRowLayout = new QHBoxLayout();
    resultRowLayout->addWidget(new QLabel(QStringLiteral("结果：")));
    m_resultLabel = new QLabel("--");
    resultRowLayout->addWidget(m_resultLabel);
    singleLayout->addLayout(resultRowLayout);

    auto* probRowLayout = new QHBoxLayout();
    probRowLayout->addWidget(new QLabel(QStringLiteral("概率：")));
    m_probabilityLabel = new QLabel("--");
    probRowLayout->addWidget(m_probabilityLabel);
    singleLayout->addLayout(probRowLayout);
    
    simpleLayout->addWidget(singleResultView);

    m_summaryLabel = new QLabel("--");
    m_summaryLabel->hide();
    simpleLayout->addWidget(m_summaryLabel);
    
    layout->addWidget(m_simpleResultWidget);

    m_expandLabel = new QLabel();
    m_expandLabel->setText(QStringLiteral("<a href=\"#expand\" style=\"color: #0078D7; text-decoration: none;\">▼ 展开详细列表</a>"));
    m_expandLabel->setTextFormat(Qt::RichText);
    m_expandLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_expandLabel->setOpenExternalLinks(false);
    m_expandLabel->hide(); // Hidden by default, only shown if multiple images
    layout->addWidget(m_expandLabel);

    m_resultsTable = new QTableWidget();
    m_resultsTable->setColumnCount(3);
    m_resultsTable->setHorizontalHeaderLabels({QStringLiteral("图像"), QStringLiteral("结果"), QStringLiteral("概率")});
    m_resultsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    // m_resultsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_resultsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultsTable->setMinimumHeight(100);
    m_resultsTable->setVisible(false); // Hidden by default
    layout->addWidget(m_resultsTable);

    connect(m_expandLabel, &QLabel::linkActivated, this, [this](const QString &link) {
        if (link == "#expand") {
            m_isExpanded = !m_isExpanded;
            m_resultsTable->setVisible(m_isExpanded);
            m_expandLabel->setText(m_isExpanded ? 
                QStringLiteral("<a href=\"#expand\" style=\"color: #0078D7; text-decoration: none;\">▲ 收起详细列表</a>") : 
                QStringLiteral("<a href=\"#expand\" style=\"color: #0078D7; text-decoration: none;\">▼ 展开详细列表</a>"));
            if (_widget) {
                _widget->setFixedWidth(!m_resultsTable->isHidden() ? 350 : 260);
                _widget->resize(0, 0);
                _widget->adjustSize();
                Q_EMIT embeddedWidgetSizeUpdated();
            }
        }
    });

    if (!m_savedResults.isEmpty()) {
        m_resultsTable->setRowCount(0);
        for (const auto& res : m_savedResults) {
            int row = m_resultsTable->rowCount();
            m_resultsTable->insertRow(row);
            m_resultsTable->setItem(row, 0, new QTableWidgetItem(res.fileName));
            m_resultsTable->setItem(row, 1, new QTableWidgetItem(res.resultText));
            m_resultsTable->setItem(row, 2, new QTableWidgetItem(res.probability));
        }
        
        int totalCount = m_savedResults.size();
        if (totalCount > 0) {
            if (totalCount == 1) {
                if (singleResultView) singleResultView->show();
                m_resultLabel->setText(m_savedResults[0].resultText);
                m_probabilityLabel->setText(m_savedResults[0].probability);
                m_summaryLabel->hide();
                m_expandLabel->hide();
            } else {
                if (singleResultView) singleResultView->hide();
                m_summaryLabel->setText(QStringLiteral("检测完成：共处理 %1 张图像").arg(totalCount));
                m_summaryLabel->show();
                m_expandLabel->show();
            }
            
            if (m_isExpanded) {
                m_resultsTable->show();
                m_expandLabel->setText(QStringLiteral("<a href=\"#expand\" style=\"color: #0078D7; text-decoration: none;\">▲ 收起详细列表</a>"));
            } else {
                m_resultsTable->hide();
                m_expandLabel->setText(QStringLiteral("<a href=\"#expand\" style=\"color: #0078D7; text-decoration: none;\">▼ 展开详细列表</a>"));
            }
        }
    }

    // Status label
    m_statusLabel = new QLabel();
    m_statusLabel->setStyleSheet("color: gray; font-size: 11px;");
    layout->addWidget(m_statusLabel);

    layout->addStretch();
}

void TargetDetectionNode::stopExecution()
{
    if (m_task)
    {
        m_task->stop();
    }
    setState(ExecutionState::Stopped);
}

void TargetDetectionNode::processAutomatically()
{
    if (m_task) {
        return;
    }

    if (isReady()) {
        executeProcessing();
    } else {
        setState(ExecutionState::Idle);
    }
}

bool TargetDetectionNode::isReady() const
{
    if (!m_inputData || m_inputData->filePaths().isEmpty()) {
        return false;
    }
    if (m_selectedModelPath.isEmpty()) {
        return false;
    }
    return true;
}

void TargetDetectionNode::execute()
{
    executeProcessing();
}

void TargetDetectionNode::executeProcessing()
{
    if (m_task)
    {
        m_task->stop();
        m_task = nullptr; // Note: QThreadPool auto-deletes the task when it finishes.
    }

    if (!isReady()) {
        if (m_statusLabel) m_statusLabel->setText(QStringLiteral("状态：未准备好"));
        setState(ExecutionState::Idle); // Explicitly state we are idle so base class won't overwrite
        return;
    }

    setProgress(0);
    if (m_statusLabel) m_statusLabel->setText(QStringLiteral("状态：正在初始化..."));
    
    // Clear previous results
    if (m_resultsTable) {
        m_resultsTable->setRowCount(0);
    }
    if (m_resultLabel) m_resultLabel->setText("--");
    if (m_probabilityLabel) m_probabilityLabel->setText("--");
    if (m_summaryLabel) m_summaryLabel->setText("--");
    
    m_savedResults.clear();

    QStringList inputPaths = m_inputData->filePaths();
    QString modelPath = m_selectedModelPath;
    float thresholdValue = m_thresholdValue;

    m_task = new TargetDetectionTask(inputPaths, modelPath, thresholdValue);

    connect(m_task, &TargetDetectionTask::updateProcess, this, &TargetDetectionNode::onProgressUpdate, Qt::QueuedConnection);
    connect(m_task, &TargetDetectionTask::sendTargetDetectionResult, this, &TargetDetectionNode::onDetectionFinished, Qt::QueuedConnection);
    connect(m_task, &TargetDetectionTask::errorProcess, this, &TargetDetectionNode::onError, Qt::QueuedConnection);
    connect(m_task, &TargetDetectionTask::askUserError, this, &TargetDetectionNode::onAskUserError, Qt::BlockingQueuedConnection);

    QThreadPool::globalInstance()->start(m_task);
    
    if (m_modelComboBox) m_modelComboBox->setEnabled(false);
    if (m_thresholdEdit) m_thresholdEdit->setEnabled(false);
}

void TargetDetectionNode::onProgressUpdate(int progress, const QString& message)
{
    setProgress(progress);
    if (m_statusLabel) {
        m_statusLabel->setText(QStringLiteral("状态：") + message);
    }
}

void TargetDetectionNode::onDetectionFinished(int imageIndex, bool success, float shipProb, QString resultText, QString errorMsg)
{
    QString fileName = "";
    if (m_inputData && imageIndex < m_inputData->filePaths().size()) {
        fileName = QFileInfo(m_inputData->filePaths()[imageIndex]).fileName();
    }

    if (m_resultsTable && !fileName.isEmpty()) {
        int row = m_resultsTable->rowCount();
        m_resultsTable->insertRow(row);
        m_resultsTable->setItem(row, 0, new QTableWidgetItem(fileName));
        
        if (success) {
            m_resultsTable->setItem(row, 1, new QTableWidgetItem(resultText));
            m_resultsTable->setItem(row, 2, new QTableWidgetItem(QString::number(shipProb * 100.0f, 'f', 2) + "%"));
        } else {
            m_resultsTable->setItem(row, 1, new QTableWidgetItem("Error"));
            m_resultsTable->setItem(row, 2, new QTableWidgetItem(errorMsg));
        }
    }
    
    if (!fileName.isEmpty()) {
        DetectionResult res;
        res.fileName = fileName;
        if (success) {
            res.resultText = resultText;
            res.probability = QString::number(shipProb * 100.0f, 'f', 2) + "%";
        } else {
            res.resultText = "Error";
            res.probability = errorMsg;
        }
        m_savedResults.append(res);
    }
    
    // We only finish execution if this is the last image.
    if (m_inputData && imageIndex == m_inputData->filePaths().size() - 1) {
        if (m_statusLabel) m_statusLabel->setText(QStringLiteral("状态：完成"));
        
        int totalCount = m_inputData->filePaths().size();
        QWidget* singleView = _widget ? _widget->findChild<QWidget*>("SingleResultView") : nullptr;
        
        if (totalCount == 1) {
            if (singleView) singleView->show();
            if (m_resultLabel) m_resultLabel->setText(success ? resultText : "Error");
            if (m_probabilityLabel) m_probabilityLabel->setText(success ? QString::number(shipProb * 100.0f, 'f', 2) + "%" : errorMsg);
            if (m_summaryLabel) m_summaryLabel->hide();
            if (m_expandLabel) m_expandLabel->hide();
        } else {
            if (singleView) singleView->hide();
            if (m_summaryLabel) {
                m_summaryLabel->setText(QStringLiteral("检测完成：共处理 %1 张图像").arg(totalCount));
                m_summaryLabel->show();
            }
            if (m_expandLabel) {
                m_expandLabel->show();
            }
        }
        
        if (_widget) {
            _widget->setFixedWidth(!m_resultsTable->isHidden() ? 350 : 260);
            _widget->resize(0, 0);
            _widget->adjustSize();
            Q_EMIT embeddedWidgetSizeUpdated();
        }

        m_outputData = m_inputData;
        setOutputData(0, m_outputData);
        Q_EMIT dataUpdated(0);
        
        finishExecution();

        if (m_modelComboBox) m_modelComboBox->setEnabled(true);
        if (m_thresholdEdit) m_thresholdEdit->setEnabled(true);

        m_task = nullptr;
    }
}

void TargetDetectionNode::onError(const QString& error)
{
    Q_EMIT executionError(error);
    setState(ExecutionState::Error);
    if (m_statusLabel) {
        m_statusLabel->setText(QStringLiteral("状态：错误 - ") + error);
    }
    
    if (m_modelComboBox) m_modelComboBox->setEnabled(true);
    if (m_thresholdEdit) m_thresholdEdit->setEnabled(true);

    m_task = nullptr;

    m_outputData.reset();
}

QJsonObject TargetDetectionNode::save() const
{
    QJsonObject modelJson = ExecutableNodeDelegateModel::save();
    modelJson["thresholdValue"] = m_thresholdValue;
    modelJson["isExpanded"] = m_isExpanded;

    QJsonArray resultsArray;
    if (m_resultsTable) {
        for (int row = 0; row < m_resultsTable->rowCount(); ++row) {
            QJsonObject resultObj;
            resultObj["fileName"] = m_resultsTable->item(row, 0) ? m_resultsTable->item(row, 0)->text() : "";
            resultObj["resultText"] = m_resultsTable->item(row, 1) ? m_resultsTable->item(row, 1)->text() : "";
            resultObj["probability"] = m_resultsTable->item(row, 2) ? m_resultsTable->item(row, 2)->text() : "";
            resultsArray.append(resultObj);
        }
    } else {
        for (const auto& res : m_savedResults) {
            QJsonObject resultObj;
            resultObj["fileName"] = res.fileName;
            resultObj["resultText"] = res.resultText;
            resultObj["probability"] = res.probability;
            resultsArray.append(resultObj);
        }
    }
    modelJson["results"] = resultsArray;

    return modelJson;
}

void TargetDetectionNode::load(QJsonObject const &json)
{
    // Assign fields first
    m_thresholdValue = json["thresholdValue"].toDouble(0.65);
    m_isExpanded = json["isExpanded"].toBool(false);

    m_savedResults.clear();
    if (json.contains("results") && json["results"].isArray()) {
        QJsonArray resultsArray = json["results"].toArray();
        for (int i = 0; i < resultsArray.size(); ++i) {
            QJsonObject resultObj = resultsArray[i].toObject();
            DetectionResult res;
            res.fileName = resultObj["fileName"].toString();
            res.resultText = resultObj["resultText"].toString();
            res.probability = resultObj["probability"].toString();
            m_savedResults.append(res);
        }
    } else if (json.contains("resultText")) {
        DetectionResult res;
        res.fileName = "Unknown";
        res.resultText = json["resultText"].toString("--");
        res.probability = QString::number(json["shipProb"].toDouble(0.0) * 100.0f, 'f', 2) + "%";
        if (res.resultText != "--") {
            m_savedResults.append(res);
        }
    }

    // Call base class load which will trigger validateAndRestoreOutput()
    ExecutableNodeDelegateModel::load(json);

    if (m_thresholdEdit) {
        m_thresholdEdit->setText(QString::number(m_thresholdValue, 'f', 2));
    }

    if (m_resultsTable) {
        m_resultsTable->setRowCount(0);
        for (const auto& res : m_savedResults) {
            int row = m_resultsTable->rowCount();
            m_resultsTable->insertRow(row);
            m_resultsTable->setItem(row, 0, new QTableWidgetItem(res.fileName));
            m_resultsTable->setItem(row, 1, new QTableWidgetItem(res.resultText));
            m_resultsTable->setItem(row, 2, new QTableWidgetItem(res.probability));
        }
        
        int totalCount = m_savedResults.size();
        if (totalCount > 0) {
            QWidget* singleView = _widget ? _widget->findChild<QWidget*>("SingleResultView") : nullptr;
            if (totalCount == 1) {
                if (singleView) singleView->show();
                if (m_resultLabel) m_resultLabel->setText(m_savedResults[0].resultText);
                if (m_probabilityLabel) m_probabilityLabel->setText(m_savedResults[0].probability);
                if (m_summaryLabel) m_summaryLabel->hide();
                if (m_expandLabel) m_expandLabel->hide();
            } else {
                if (singleView) singleView->hide();
                if (m_summaryLabel) {
                    m_summaryLabel->setText(QStringLiteral("检测完成：共处理 %1 张图像").arg(totalCount));
                    m_summaryLabel->show();
                }
                if (m_expandLabel) m_expandLabel->show();
            }
            
            if (m_isExpanded) {
                m_resultsTable->show();
                if (m_expandLabel) m_expandLabel->setText(QStringLiteral("<a href=\"#expand\" style=\"color: #0078D7; text-decoration: none;\">▲ 收起详细列表</a>"));
            } else {
                m_resultsTable->hide();
                if (m_expandLabel) m_expandLabel->setText(QStringLiteral("<a href=\"#expand\" style=\"color: #0078D7; text-decoration: none;\">▼ 展开详细列表</a>"));
            }
            if (_widget) {
                _widget->setFixedWidth(!m_resultsTable->isHidden() ? 350 : 260);
                _widget->resize(0, 0);
                _widget->adjustSize();
                Q_EMIT embeddedWidgetSizeUpdated();
            }
        }
    }

    if (executionState() == ExecutionState::Completed) {
        if (m_statusLabel) m_statusLabel->setText(QStringLiteral("状态：完成"));
    }
}

void TargetDetectionNode::onAskUserError(const QString& message, bool* skip)
{
    QMessageBox::StandardButton reply = QMessageBox::question(
        nullptr,
        QStringLiteral("错误"), // 错误
        message,
        QMessageBox::Yes | QMessageBox::No
    );
    *skip = (reply == QMessageBox::Yes);
}

bool TargetDetectionNode::validateAndRestoreOutput()
{
    // Target detection does not output a new file, it just displays text results.
    // If we reach here, it means it was previously marked as Completed.
    // We can directly return true to restore the state.
    return true;
}

} // namespace QtNodes