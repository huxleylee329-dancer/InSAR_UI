#include "BaselinePreviewNode.h"
#include "NodeUtils.h"
#include "IApplicationInterface.h"
#include "InterfaceManager.h"
#include "MainWindow.h"
#include "WorkspaceUI.h"
#include "InSARLogManager.h"
#include <QTimer>
#include <QJsonDocument>
#include <QMessageBox>
#include <QFileInfo>
#include <QPainter>
#include <QDir>
#include <QApplication>

namespace QtNodes {

BaselinePreviewNode::BaselinePreviewNode()
    : ExecutableNodeDelegateModel()
    , _widget(nullptr)
    , m_inputNodeLabel(nullptr)
    , m_defaultMasterCheckBox(nullptr)
    , m_masterImageCombo(nullptr)
    , m_showChartBtn(nullptr)
    , m_masterIndex(1)
    , m_useDefaultMaster(true)
    , m_worker(nullptr)
    , m_thread(nullptr)
{
    setExecutionMode(ExecutionMode::Automatic);
}

BaselinePreviewNode::~BaselinePreviewNode()
{
    stopExecution();
}

unsigned int BaselinePreviewNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 1;
    else
        return 1;
}

NodeDataType BaselinePreviewNode::dataType(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    if (portType == PortType::In)
        return NodeDataType{"imported_file", "Imported File"};
    else
        return NodeDataType{"baseline", "Baseline Data"};
}

bool BaselinePreviewNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return true;
}

QString BaselinePreviewNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In)
        return QStringLiteral("输入图像");
    else
        return QStringLiteral("基线数据");
}

bool BaselinePreviewNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out)
        return true;
    return false;
}

std::shared_ptr<NodeData> BaselinePreviewNode::outData(PortIndex port)
{
    Q_UNUSED(port);
    // 状态守卫：非 Completed 时返回 nullptr，确保脏传播正确级联
    if (executionState() != ExecutionState::Completed)
        return nullptr;
    return m_outputData;
}

void BaselinePreviewNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    Q_UNUSED(port);
    m_inputData = std::dynamic_pointer_cast<ImportedFileData>(data);
    
    QStringList filePaths = m_inputData ? m_inputData->filePaths() : QStringList();

    if (filePaths.isEmpty()) {
        m_outputData.reset();
        m_temporalBaselines.clear();
        m_spatialBaselines.clear();
        setOutputData(0, nullptr);
        if (m_showChartBtn) m_showChartBtn->setEnabled(false);
    }

    updateMasterImageCombo();
    updateLabels();

    ExecutableNodeDelegateModel::setInData(data, port);

    // 恢复工程时基类会抑制输入状态更新；无效输入不能保留已完成状态。
    if (filePaths.isEmpty() && executionState() == ExecutionState::Completed) {
        setState(ExecutionState::Idle);
    }
}

void BaselinePreviewNode::inputConnectionDeleted(ConnectionId const& connectionId)
{
    // 该节点的输出使用私有缓存，断开输入时需显式清理，不能只依赖基类输出缓存。
    m_inputData.reset();
    m_outputData.reset();
    m_temporalBaselines.clear();
    m_spatialBaselines.clear();
    setOutputData(0, nullptr);
    if (m_showChartBtn) m_showChartBtn->setEnabled(false);
    updateMasterImageCombo();
    updateLabels();

    ExecutableNodeDelegateModel::inputConnectionDeleted(connectionId);
}

::QWidget* BaselinePreviewNode::embeddedWidget()
{
    if (!_widget)
    {
        createWidget();
    }
    return _widget;
}

void BaselinePreviewNode::setExecutionMode(ExecutionMode mode)
{
    ExecutableNodeDelegateModel::setExecutionMode(mode);
}

void BaselinePreviewNode::createWidget()
{
    _widget = new QWidget();
    _widget->setObjectName("NodeEmbeddedWidget");
    _widget->setFixedWidth(300); // SOP critical: Lock width to avoid infinite expansion bug

    QVBoxLayout* mainLayout = new QVBoxLayout(_widget);
    mainLayout->setContentsMargins(5, 5, 5, 5);
    mainLayout->setSpacing(4);

    // Row 1: Input node display
    QHBoxLayout* inputLayout = new QHBoxLayout();
    QLabel* inputTitleLabel = new QLabel(QStringLiteral("输入节点:"));
    inputTitleLabel->setFixedWidth(80); // SOP alignment: Fixed label width
    m_inputNodeLabel = new QLabel(QStringLiteral("等待输入"));
    m_inputNodeLabel->setStyleSheet("color: gray;");
    inputLayout->addWidget(inputTitleLabel);
    inputLayout->addWidget(m_inputNodeLabel);
    inputLayout->addStretch();
    mainLayout->addLayout(inputLayout);

    // Row 2: Default master checkbox
    QHBoxLayout* defaultLayout = new QHBoxLayout();
    m_defaultMasterCheckBox = new QCheckBox(QStringLiteral("默认首张图像为主图像"));
    m_defaultMasterCheckBox->setChecked(m_useDefaultMaster);
    defaultLayout->addWidget(m_defaultMasterCheckBox);
    mainLayout->addLayout(defaultLayout);

    // Row 4: Master Image ComboBox
    QHBoxLayout* masterLayout = new QHBoxLayout();
    QLabel* masterTitleLabel = new QLabel(QStringLiteral("主图像选择:"));
    masterTitleLabel->setFixedWidth(80); // SOP alignment: Fixed label width
    m_masterImageCombo = new QComboBox();
    m_masterImageCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    masterLayout->addWidget(masterTitleLabel);
    masterLayout->addWidget(m_masterImageCombo);
    mainLayout->addLayout(masterLayout);

    // Row 5: Result display label
    m_resultLabel = new QLabel(QStringLiteral("状态：等待计算"));
    m_resultLabel->setStyleSheet("QLabel { background-color: rgba(128, 128, 128, 20); border: 1px solid rgba(128, 128, 128, 80); border-radius: 4px; padding: 6px; font-size: 11px; line-height: 14px; }");
    m_resultLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_resultLabel->setWordWrap(true);
    mainLayout->addWidget(m_resultLabel);

    // Row 6: Show Chart Button
    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_showChartBtn = new QPushButton(QStringLiteral("查看基线图"));
    m_showChartBtn->setEnabled(false); // Disabled until run completes
    QFont btnFont = m_showChartBtn->font();
    btnFont.setBold(true);
    m_showChartBtn->setFont(btnFont);
    btnLayout->addWidget(m_showChartBtn);
    mainLayout->addLayout(btnLayout);

    // Connect signals
    connect(m_defaultMasterCheckBox, &QCheckBox::stateChanged, this, [this](int state) {
        m_useDefaultMaster = (state == Qt::Checked);
        updateMasterImageCombo();
        invalidateExecution();
    });

    connect(m_masterImageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_useDefaultMaster) {
            m_masterIndex = 1;
        } else {
            // Under user selection mode, item 0 is the placeholder "请选择主图像..."
            if (index > 0) {
                m_masterIndex = index; // 1-based index (representing elements at index 1..N of combo, which match files 0..N-1)
            } else {
                m_masterIndex = -1; // Invalid selection
            }
        }
        invalidateExecution();
    });

    connect(m_showChartBtn, &QPushButton::clicked, this, &BaselinePreviewNode::showChart);

    // Initialize list states
    updateMasterImageCombo();
    updateLabels(); // SOP: initial update and self-remedy
}

void BaselinePreviewNode::updateLabels()
{
    if (!m_inputNodeLabel) return;

    if (m_inputData) {
        m_inputNodeLabel->setText(m_inputData->nodeName());
        m_inputNodeLabel->setStyleSheet("color: green; font-weight: bold;");
    } else {
        m_inputNodeLabel->setText(QStringLiteral("等待输入"));
        m_inputNodeLabel->setStyleSheet("color: gray;");
    }

    if (m_resultLabel) {
        if (m_temporalBaselines.isEmpty() || m_spatialBaselines.isEmpty()) {
            m_resultLabel->setText(QStringLiteral("状态：等待计算或输入"));
        } else {
            double maxTemp = 0;
            double maxSpat = 0;
            for (double t : m_temporalBaselines) {
                if (qAbs(t) > qAbs(maxTemp)) maxTemp = t;
            }
            for (double s : m_spatialBaselines) {
                if (qAbs(s) > qAbs(maxSpat)) maxSpat = s;
            }
            QString masterName = QStringLiteral("未知");
            QStringList filePaths = m_inputData ? m_inputData->filePaths() : QStringList();
            if (m_inputData && m_masterIndex >= 1 && m_masterIndex <= filePaths.size()) {
                masterName = QFileInfo(filePaths.at(m_masterIndex - 1)).fileName();
            }
            m_resultLabel->setText(QStringLiteral("主图像: %1\n最大时间基线: %2 天\n最大空间基线: %3 米")
                .arg(masterName)
                .arg(QString::number(maxTemp, 'f', 1))
                .arg(QString::number(maxSpat, 'f', 1)));
        }
    }
    updateWidgetSize();
}

void BaselinePreviewNode::updateWidgetSize()
{
    if (_widget) {
        _widget->setFixedWidth(300);
        _widget->adjustSize();
        Q_EMIT embeddedWidgetSizeUpdated();
    }
}

void BaselinePreviewNode::updateMasterImageCombo()
{
    if (!m_masterImageCombo) return;

    m_masterImageCombo->disconnect(this);

    if (m_useDefaultMaster) {
        m_masterImageCombo->clear();
        // SOP: virtual placeholder item under disabled state
        m_masterImageCombo->addItem(QStringLiteral("自动选择首张图像..."));
        m_masterImageCombo->setDisabled(true);
        m_masterIndex = 1;
    } else {
        m_masterImageCombo->clear();
        m_masterImageCombo->setDisabled(false);

        // Add guidance item
        m_masterImageCombo->addItem(QStringLiteral("请选择主图像..."));

        QStringList filePaths = m_inputData ? m_inputData->filePaths() : QStringList();
        if (m_inputData && !filePaths.isEmpty()) {
            for (const QString& path : filePaths) {
                m_masterImageCombo->addItem(QFileInfo(path).fileName());
            }
            if (m_masterIndex >= 1 && m_masterIndex <= filePaths.size()) {
                m_masterImageCombo->setCurrentIndex(m_masterIndex);
            } else {
                m_masterImageCombo->setCurrentIndex(0); // Show "请选择主图像..."
            }
        } else {
            m_masterImageCombo->setCurrentIndex(0);
        }
    }

    // Re-connect
    connect(m_masterImageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_useDefaultMaster) {
            m_masterIndex = 1;
        } else {
            if (index > 0) {
                m_masterIndex = index;
            } else {
                m_masterIndex = -1;
            }
        }
        invalidateExecution();
    });
}

bool BaselinePreviewNode::validateInputs() const
{
    QStringList filePaths = m_inputData ? m_inputData->filePaths() : QStringList();
    if (filePaths.isEmpty()) {
        return false;
    }
    if (!m_useDefaultMaster && m_masterIndex < 1) {
        return false; // Guidance item is selected
    }
    return true;
}

void BaselinePreviewNode::execute()
{
    if (!validateInputs()) {
        onError(QStringLiteral("参数校验未通过，请连接输入并正确选择主图像！"));
        return;
    }
    executeProcessing();
}

void BaselinePreviewNode::executeProcessing()
{
    stopExecution();

    m_thread = new QThread(this);
    m_worker = new BaselineWorker();
    m_worker->moveToThread(m_thread);

    int finalMasterIndex = m_useDefaultMaster ? 1 : m_masterIndex;
    QStringList filePaths = m_inputData ? m_inputData->filePaths() : QStringList();

    connect(m_thread, &QThread::started, m_worker, [this, finalMasterIndex, filePaths]() {
        m_worker->Baseline_Estimate(finalMasterIndex, filePaths);
    });

    connect(m_worker, &BaselineWorker::updateProcess, this, &BaselinePreviewNode::onProgressUpdate);
    connect(m_worker, &BaselineWorker::sendBL, this, &BaselinePreviewNode::onProcessingFinished);
    connect(m_worker, &BaselineWorker::errorProcess, this, &BaselinePreviewNode::onError);

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);

    setState(ExecutionState::Running);
    setProgress(0);

    // SOP: Force execution state back to Running in automatic mode next cycle
    QTimer::singleShot(0, this, [this]() {
        if (m_thread && m_thread->isRunning()) {
            setState(ExecutionState::Running);
        }
    });

    m_thread->start();
}

void BaselinePreviewNode::stopExecution()
{
    if (m_thread && m_thread->isRunning()) {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
    }
    m_thread = nullptr;
    m_worker = nullptr;
}

void BaselinePreviewNode::processAutomatically()
{
    if (validateInputs()) {
        executeProcessing();
    }
}

void BaselinePreviewNode::onProgressUpdate(int progress, const QString& message)
{
    Q_UNUSED(message);
    setProgress(progress);
}

void BaselinePreviewNode::onError(const QString& error)
{
    InSARLogManager::LogError("BaselinePreviewNode", "Error during baseline estimate: " + error);
    setState(ExecutionState::Error);
    if (m_showChartBtn) m_showChartBtn->setEnabled(false);
    finishExecution();
    stopExecution();
}

void BaselinePreviewNode::onProcessingFinished(QList<double> temporal_baseline, QList<double> spatial_baseline, int index)
{
    m_temporalBaselines = temporal_baseline;
    m_spatialBaselines = spatial_baseline;
    m_masterIndex = index;

    // Serialize baseline results into BaselineData
    QJsonObject blObj;
    blObj["master_index"] = m_masterIndex;
    
    QJsonArray tempArr;
    for (double t : m_temporalBaselines) tempArr.append(t);
    blObj["temporal_baseline"] = tempArr;

    QJsonArray spatArr;
    for (double s : m_spatialBaselines) spatArr.append(s);
    blObj["spatial_baseline"] = spatArr;

    QJsonDocument doc(blObj);
    m_outputData = std::make_shared<BaselineData>(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));

    setProgress(100);
    setState(ExecutionState::Running);
    if (m_showChartBtn) m_showChartBtn->setEnabled(true);

    generateStaticPreviewJpg();
    updateLabels();

    Q_EMIT dataUpdated(0);
    finishExecution();
    stopExecution();
}

void BaselinePreviewNode::showChart()
{
    if (m_temporalBaselines.isEmpty() || m_spatialBaselines.isEmpty()) {
        QMessageBox::warning(nullptr, "Warning", QStringLiteral("没有可用的基线计算数据，请先运行节点！"));
        return;
    }
    Baseline_Preview* chartWindow = new Baseline_Preview();
    chartWindow->setAttribute(Qt::WA_DeleteOnClose, true);
    chartWindow->show();
    chartWindow->Paint(m_temporalBaselines, m_spatialBaselines, m_masterIndex);
}

bool BaselinePreviewNode::validateAndRestoreOutput()
{
    // High self-containment check: check if we have cached baseline lists loaded from load()
    if (!m_temporalBaselines.isEmpty() && !m_spatialBaselines.isEmpty() && m_masterIndex >= 1) {

        // Regenerate static JPG if it is missing
        QString dir = projectPath();
        if (!dir.isEmpty()) {
            QString jpgPath = dir + "/.temp/baseline_preview_" + QString::number(_nodeId) + ".jpg";
            if (!QFileInfo::exists(jpgPath)) {
                generateStaticPreviewJpg();
            }
        }

        QJsonObject blObj;
        blObj["master_index"] = m_masterIndex;
        
        QJsonArray tempArr;
        for (double t : m_temporalBaselines) tempArr.append(t);
        blObj["temporal_baseline"] = tempArr;

        QJsonArray spatArr;
        for (double s : m_spatialBaselines) spatArr.append(s);
        blObj["spatial_baseline"] = spatArr;

        QJsonDocument doc(blObj);
        m_outputData = std::make_shared<BaselineData>(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));

        if (m_showChartBtn) m_showChartBtn->setEnabled(true);
        setState(ExecutionState::Completed);
        setProgress(100);
        updateLabels();
        Q_EMIT dataUpdated(0);
        return true;
    }
    return false;
}

QJsonObject BaselinePreviewNode::save() const
{
    QJsonObject root = ExecutableNodeDelegateModel::save();
    root["useDefaultMaster"] = m_useDefaultMaster;
    root["masterIndex"] = m_masterIndex;

    // Cache temporal & spatial baselines to support offline restoration
    QJsonArray tempArr;
    for (double t : m_temporalBaselines) tempArr.append(t);
    root["temporalBaselines"] = tempArr;

    QJsonArray spatArr;
    for (double s : m_spatialBaselines) spatArr.append(s);
    root["spatialBaselines"] = spatArr;

    return root;
}

void BaselinePreviewNode::load(QJsonObject const& json)
{
    m_useDefaultMaster = json["useDefaultMaster"].toBool(true);
    m_masterIndex = json["masterIndex"].toInt(1);

    m_temporalBaselines.clear();
    if (json.contains("temporalBaselines")) {
        QJsonArray tempArr = json["temporalBaselines"].toArray();
        for (auto val : tempArr) {
            m_temporalBaselines.append(val.toDouble());
        }
    }

    m_spatialBaselines.clear();
    if (json.contains("spatialBaselines")) {
        QJsonArray spatArr = json["spatialBaselines"].toArray();
        for (auto val : spatArr) {
            m_spatialBaselines.append(val.toDouble());
        }
    }

    // SOP rule 15: Must set properties BEFORE calling base class load(),
    // since base class load() synchronously triggers validateAndRestoreOutput()
    ExecutableNodeDelegateModel::load(json);

    updateMasterImageCombo();
    updateLabels();
}

QStringList BaselinePreviewNode::previewImagePaths() const
{
    QString dir = projectPath();
    if (!dir.isEmpty()) {
        QString jpgPath = dir + "/.temp/baseline_preview_" + QString::number(_nodeId) + ".jpg";
        if (QFileInfo::exists(jpgPath)) {
            return QStringList() << jpgPath;
        }
    }
    return QStringList();
}

void BaselinePreviewNode::generateStaticPreviewJpg()
{
    if (m_temporalBaselines.isEmpty() || m_spatialBaselines.isEmpty()) return;

    // Generate chart offscreen
    Baseline_Preview* preview = new Baseline_Preview();
    preview->Paint(m_temporalBaselines, m_spatialBaselines, m_masterIndex);

    // Increase resolution to 1920x1440 for high-DPI crisp rendering
    QPixmap pixmap(1920, 1440);
    pixmap.fill(Qt::white);
    QPainter painter(&pixmap);

    // Enable high-quality anti-aliasing hints
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (preview->chart()) {
        // Adjust font sizes for high-resolution offscreen render
        QFont titleFont = preview->chart()->titleFont();
        titleFont.setPointSize(32); // Large title for 1920x1440
        titleFont.setBold(true);
        preview->chart()->setTitleFont(titleFont);

        QFont axisTitleFont = titleFont;
        axisTitleFont.setPointSize(22);
        axisTitleFont.setBold(true);

        QFont axisLabelsFont = titleFont;
        axisLabelsFont.setPointSize(18);
        axisLabelsFont.setBold(false);

        for (QAbstractAxis* axis : preview->chart()->axes()) {
            axis->setTitleFont(axisTitleFont);
            axis->setLabelsFont(axisLabelsFont);
        }

        // Scale up visual items (lines and scatter markers) proportionally
        for (QAbstractSeries* series : preview->chart()->series()) {
            QScatterSeries* scatter = qobject_cast<QScatterSeries*>(series);
            if (scatter) {
                // Scale marker size (e.g. from 12~20 to 24~40)
                scatter->setMarkerSize(scatter->markerSize() * 2.0);
            }
            QLineSeries* line = qobject_cast<QLineSeries*>(series);
            if (line) {
                // Scale line thickness to make paths clearly visible on 1920x1440
                QPen pen = line->pen();
                qreal newWidth = pen.widthF() > 0 ? pen.widthF() * 2.5 : 2.5;
                pen.setWidthF(newWidth);
                line->setPen(pen);
            }
        }

        preview->chart()->resize(QSizeF(1920, 1440));
        if (preview->chart()->scene()) {
            preview->chart()->scene()->setSceneRect(0, 0, 1920, 1440);
            preview->chart()->scene()->render(&painter, QRectF(0, 0, 1920, 1440), QRectF(0, 0, 1920, 1440));
        }
    }
    painter.end();

    QString dir = projectPath();
    if (!dir.isEmpty()) {
        QString tempDir = dir + "/.temp";
        QDir().mkpath(tempDir);
        QString jpgPath = tempDir + "/baseline_preview_" + QString::number(_nodeId) + ".jpg";
        // Save with 100% quality to avoid JPEG compression artifacts
        pixmap.save(jpgPath, "JPG", 100);
    }

    delete preview;
}

QString BaselinePreviewNode::projectPath() const
{
    IApplicationInterface* iface = nullptr;
    if (_widget) {
        iface = NodeUtils::getProjectContext(_widget);
    }
    if (!iface) {
        for (QWidget* topLevelWidget : QApplication::topLevelWidgets()) {
            MainWindow* mainWin = qobject_cast<MainWindow*>(topLevelWidget);
            if (mainWin) {
                if (mainWin->workspaceUI()) {
                    iface = mainWin->workspaceUI();
                    break;
                }
                if (mainWin->interfaceManager()) {
                    iface = mainWin->interfaceManager()->currentInterface();
                    if (iface) {
                        break;
                    }
                }
            }
        }
    }

    if (iface) {
        QString fullPath = iface->projectPath();
        if (fullPath.endsWith(".insar", Qt::CaseInsensitive)) {
            return QFileInfo(fullPath).absolutePath();
        }
        return fullPath;
    }
    return QString();
}

} // namespace QtNodes
