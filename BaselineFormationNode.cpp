#include "BaselineFormationNode.h"
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
#include <QDebug>
#include <QPainter>
#include <QDir>
#include <QApplication>
#include "Coordinate.h"

namespace QtNodes {

BaselineFormationNode::BaselineFormationNode()
    : ExecutableNodeDelegateModel()
    , _widget(nullptr)
    , m_inputNodeLabel(nullptr)
    , m_defaultMasterCheckBox(nullptr)
    , m_masterImageCombo(nullptr)
    , m_spatialThreshEdit(nullptr)
    , m_temporalThreshEdit(nullptr)
    , m_temporalThreshLowEdit(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_showChartBtn(nullptr)
    , m_resultLabel(nullptr)
    , m_masterIndex(1)
    , m_useDefaultMaster(true)
    , m_spatialThresh(500.0)
    , m_temporalThresh(200.0)
    , m_temporalThreshLow(0.0)
    , m_outputNodeName(QStringLiteral("Baseline_Est"))
    , m_worker(nullptr)
    , m_thread(nullptr)
{
    setExecutionMode(ExecutionMode::Automatic);
}

BaselineFormationNode::~BaselineFormationNode()
{
    stopExecution();
}

unsigned int BaselineFormationNode::nPorts(PortType portType) const
{
    return 1;
}

NodeDataType BaselineFormationNode::dataType(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    return NodeDataType{"imported_file", "Imported File"};
}

bool BaselineFormationNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return true;
}

QString BaselineFormationNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In)
        return QStringLiteral("输入图像");
    else
        return QStringLiteral("输出图像");
}

bool BaselineFormationNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out)
        return true;
    return false;
}

std::shared_ptr<NodeData> BaselineFormationNode::outData(PortIndex port)
{
    Q_UNUSED(port);
    return m_outputData;
}

void BaselineFormationNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
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
}

::QWidget* BaselineFormationNode::embeddedWidget()
{
    if (!_widget)
    {
        createWidget();
    }
    return _widget;
}

void BaselineFormationNode::setExecutionMode(ExecutionMode mode)
{
    ExecutableNodeDelegateModel::setExecutionMode(mode);
}

void BaselineFormationNode::createWidget()
{
    _widget = new QWidget();
    _widget->setObjectName("NodeEmbeddedWidget");
    _widget->setFixedWidth(300); // SOP: lock width

    QVBoxLayout* mainLayout = new QVBoxLayout(_widget);
    mainLayout->setContentsMargins(5, 5, 5, 5);
    mainLayout->setSpacing(4);

    // Row 1: Input node display
    QHBoxLayout* inputLayout = new QHBoxLayout();
    QLabel* inputTitleLabel = new QLabel(QStringLiteral("输入节点:"));
    inputTitleLabel->setFixedWidth(80); // SOP: fixed label width
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

    // Row 3: Master Image ComboBox
    QHBoxLayout* masterLayout = new QHBoxLayout();
    QLabel* masterTitleLabel = new QLabel(QStringLiteral("主图像选择:"));
    masterTitleLabel->setFixedWidth(80); // SOP: fixed label width
    m_masterImageCombo = new QComboBox();
    m_masterImageCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    masterLayout->addWidget(masterTitleLabel);
    masterLayout->addWidget(m_masterImageCombo);
    mainLayout->addLayout(masterLayout);

    // Row 4: Spatial Threshold
    QHBoxLayout* spatialLayout = new QHBoxLayout();
    QLabel* spatialTitleLabel = new QLabel(QStringLiteral("空间基线阈值:"));
    spatialTitleLabel->setFixedWidth(80); // SOP: fixed label width
    m_spatialThreshEdit = new QLineEdit(QString::number(m_spatialThresh));
    spatialLayout->addWidget(spatialTitleLabel);
    spatialLayout->addWidget(m_spatialThreshEdit);
    mainLayout->addLayout(spatialLayout);

    // Row 5: Temporal Threshold
    QHBoxLayout* temporalLayout = new QHBoxLayout();
    QLabel* temporalTitleLabel = new QLabel(QStringLiteral("时间基线上限:"));
    temporalTitleLabel->setFixedWidth(80); // SOP: fixed label width
    m_temporalThreshEdit = new QLineEdit(QString::number(m_temporalThresh));
    temporalLayout->addWidget(temporalTitleLabel);
    temporalLayout->addWidget(m_temporalThreshEdit);
    mainLayout->addLayout(temporalLayout);

    // Row 6: Temporal Threshold Low
    QHBoxLayout* temporalLowLayout = new QHBoxLayout();
    QLabel* temporalLowTitleLabel = new QLabel(QStringLiteral("时间基线下限:"));
    temporalLowTitleLabel->setFixedWidth(80); // SOP: fixed label width
    m_temporalThreshLowEdit = new QLineEdit(QString::number(m_temporalThreshLow));
    temporalLowLayout->addWidget(temporalLowTitleLabel);
    temporalLowLayout->addWidget(m_temporalThreshLowEdit);
    mainLayout->addLayout(temporalLowLayout);

    // Row 7: Output Node Name
    QHBoxLayout* outNodeLayout = new QHBoxLayout();
    QLabel* outNodeTitleLabel = new QLabel(QStringLiteral("目标节点名:"));
    outNodeTitleLabel->setFixedWidth(80); // SOP: fixed label width
    m_outputNodeNameEdit = new QLineEdit(m_outputNodeName);
    m_outputNodeNameEdit->setPlaceholderText(QStringLiteral("自动生成或手动输入")); // SOP: standard placeholder
    outNodeLayout->addWidget(outNodeTitleLabel);
    outNodeLayout->addWidget(m_outputNodeNameEdit);
    mainLayout->addLayout(outNodeLayout);

    // Row 8: Result display label
    m_resultLabel = new QLabel(QStringLiteral("状态：等待计算"));
    m_resultLabel->setStyleSheet("QLabel { background-color: rgba(128, 128, 128, 20); border: 1px solid rgba(128, 128, 128, 80); border-radius: 4px; padding: 6px; font-size: 11px; line-height: 14px; }");
    m_resultLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_resultLabel->setWordWrap(true);
    mainLayout->addWidget(m_resultLabel);

    // Row 9: Show Chart Button
    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_showChartBtn = new QPushButton(QStringLiteral("查看网格图"));
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
            if (index > 0) {
                m_masterIndex = index;
            } else {
                m_masterIndex = -1;
            }
        }
        invalidateExecution();
    });

    connect(m_spatialThreshEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        bool ok;
        double val = text.toDouble(&ok);
        if (ok) {
            m_spatialThresh = val;
            invalidateExecution();
        }
    });

    connect(m_temporalThreshEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        bool ok;
        double val = text.toDouble(&ok);
        if (ok) {
            m_temporalThresh = val;
            invalidateExecution();
        }
    });

    connect(m_temporalThreshLowEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        bool ok;
        double val = text.toDouble(&ok);
        if (ok) {
            m_temporalThreshLow = val;
            invalidateExecution();
        }
    });

    connect(m_outputNodeNameEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_outputNodeName = text;
        invalidateExecution();
    });

    connect(m_showChartBtn, &QPushButton::clicked, this, &BaselineFormationNode::showChart);

    // Initialize list states
    updateMasterImageCombo();
    updateLabels(); // SOP initial update
}

void BaselineFormationNode::updateLabels()
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

void BaselineFormationNode::updateWidgetSize()
{
    if (_widget) {
        _widget->setFixedWidth(300);
        _widget->adjustSize();
        Q_EMIT embeddedWidgetSizeUpdated();
    }
}

void BaselineFormationNode::updateMasterImageCombo()
{
    if (!m_masterImageCombo) return;

    m_masterImageCombo->disconnect(this);

    if (m_useDefaultMaster) {
        m_masterImageCombo->clear();
        m_masterImageCombo->addItem(QStringLiteral("自动选择首张图像..."));
        m_masterImageCombo->setDisabled(true);
        m_masterIndex = 1;
    } else {
        m_masterImageCombo->clear();
        m_masterImageCombo->setDisabled(false);
        m_masterImageCombo->addItem(QStringLiteral("请选择主图像..."));

        QStringList filePaths = m_inputData ? m_inputData->filePaths() : QStringList();
        if (m_inputData && !filePaths.isEmpty()) {
            for (const QString& path : filePaths) {
                m_masterImageCombo->addItem(QFileInfo(path).fileName());
            }
            if (m_masterIndex >= 1 && m_masterIndex <= filePaths.size()) {
                m_masterImageCombo->setCurrentIndex(m_masterIndex);
            } else {
                m_masterImageCombo->setCurrentIndex(0);
            }
        } else {
            m_masterImageCombo->setCurrentIndex(0);
        }
    }

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

bool BaselineFormationNode::validateInputs() const
{
    QStringList filePaths = m_inputData ? m_inputData->filePaths() : QStringList();
    if (filePaths.isEmpty()) {
        return false;
    }
    if (!m_useDefaultMaster && m_masterIndex < 1) {
        return false;
    }
    if (m_outputNodeName.isEmpty()) {
        return false;
    }
    return true;
}

void BaselineFormationNode::execute()
{
    if (!validateInputs()) {
        onError(QStringLiteral("参数校验未通过，请连接输入并正确选择主图像和目标节点名！"));
        return;
    }
    executeProcessing();
}

void BaselineFormationNode::executeProcessing()
{
    InSARLogManager::LogInfo("BaselineFormationNode", "executeProcessing started.");
    stopExecution();

    m_thread = new QThread(this);
    m_worker = new BaselineWorker();
    m_worker->moveToThread(m_thread);

    int finalMasterIndex = m_useDefaultMaster ? 1 : m_masterIndex;
    QStringList filePaths = m_inputData ? m_inputData->filePaths() : QStringList();

    connect(m_thread, &QThread::started, m_worker, [this, finalMasterIndex, filePaths]() {
        m_worker->Baseline_Estimate(finalMasterIndex, filePaths);
    });

    connect(m_worker, &BaselineWorker::updateProcess, this, &BaselineFormationNode::onProgressUpdate);
    connect(m_worker, &BaselineWorker::sendBL, this, &BaselineFormationNode::onProcessingFinished);
    connect(m_worker, &BaselineWorker::errorProcess, this, &BaselineFormationNode::onError);

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);

    setState(ExecutionState::Running);
    setProgress(0);

    QTimer::singleShot(0, this, [this]() {
        if (m_thread && m_thread->isRunning()) {
            setState(ExecutionState::Running);
        }
    });

    m_thread->start();
}

void BaselineFormationNode::stopExecution()
{
    if (m_thread && m_thread->isRunning()) {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
    }
    m_thread = nullptr;
    m_worker = nullptr;
}

void BaselineFormationNode::processAutomatically()
{
    if (validateInputs()) {
        executeProcessing();
    }
}

void BaselineFormationNode::onProgressUpdate(int progress, const QString& message)
{
    Q_UNUSED(message);
    setProgress(progress);
}

void BaselineFormationNode::onError(const QString& error)
{
    InSARLogManager::LogError("BaselineFormationNode", "Error during baseline formation: " + error);
    setState(ExecutionState::Error);
    if (m_showChartBtn) m_showChartBtn->setEnabled(false);
    finishExecution();
    stopExecution();
}

void BaselineFormationNode::onProcessingFinished(QList<double> temporal_baseline, QList<double> spatial_baseline, int index)
{
    InSARLogManager::LogInfo("BaselineFormationNode", "executeProcessing completed.");
    m_temporalBaselines = temporal_baseline;
    m_spatialBaselines = spatial_baseline;
    m_masterIndex = index;

    QStringList filePaths = m_inputData ? m_inputData->filePaths() : QStringList();
    m_outputData = std::make_shared<ImportedFileData>(filePaths, m_outputNodeName);

    setProgress(100);
    setState(ExecutionState::Completed);
    if (m_showChartBtn) m_showChartBtn->setEnabled(true);

    // SOP rule 7 & 17: Generate static preview graph asynchronously or using offscreen coordinate rendering
    // Generate static preview JPG using off-screen Coordinate chart
    QString dir = projectPath();
    if (!dir.isEmpty()) {
        QString tempDir = dir + "/.temp";
        QDir().mkpath(tempDir);
        QString jpgPath = tempDir + "/baseline_preview_" + QString::number(_nodeId) + ".jpg";

        Coordinate* map = new Coordinate();
        map->Paint2(m_temporalBaselines, m_spatialBaselines, m_masterIndex, m_temporalThresh, m_temporalThreshLow, m_spatialThresh);

        QPixmap pixmap(1920, 1440);
        pixmap.fill(Qt::white);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);

        if (map->chart()) {
            // scale up title & labels
            QFont titleFont = map->chart()->titleFont();
            titleFont.setPointSize(32);
            titleFont.setBold(true);
            map->chart()->setTitleFont(titleFont);

            QFont axisTitleFont = titleFont;
            axisTitleFont.setPointSize(22);
            axisTitleFont.setBold(true);

            QFont axisLabelsFont = titleFont;
            axisLabelsFont.setPointSize(18);
            axisLabelsFont.setBold(false);

            for (QAbstractAxis* axis : map->chart()->axes()) {
                axis->setTitleFont(axisTitleFont);
                axis->setLabelsFont(axisLabelsFont);
            }

            for (QAbstractSeries* series : map->chart()->series()) {
                QScatterSeries* scatter = qobject_cast<QScatterSeries*>(series);
                if (scatter) {
                    scatter->setMarkerSize(scatter->markerSize() * 2.0);
                }
                QLineSeries* line = qobject_cast<QLineSeries*>(series);
                if (line) {
                    QPen pen = line->pen();
                    pen.setWidthF(pen.widthF() > 0 ? pen.widthF() * 2.5 : 2.5);
                    line->setPen(pen);
                }
            }

            map->chart()->resize(QSizeF(1920, 1440));
            if (map->chart()->scene()) {
                map->chart()->scene()->setSceneRect(0, 0, 1920, 1440);
                map->chart()->scene()->render(&painter, QRectF(0, 0, 1920, 1440), QRectF(0, 0, 1920, 1440));
            }
        }
        painter.end();
        pixmap.save(jpgPath, "JPG", 100);
        delete map;
    }

    updateLabels();

    Q_EMIT dataUpdated(0);
    finishExecution();
    stopExecution();
}

void BaselineFormationNode::showChart()
{
    if (m_temporalBaselines.isEmpty() || m_spatialBaselines.isEmpty()) {
        QMessageBox::warning(nullptr, "Warning", QStringLiteral("没有可用的基线计算数据，请先运行节点！"));
        return;
    }
    Coordinate* chartWindow = new Coordinate();
    chartWindow->setAttribute(Qt::WA_DeleteOnClose, true);
    chartWindow->show();
    chartWindow->Paint2(m_temporalBaselines, m_spatialBaselines, m_masterIndex, m_temporalThresh, m_temporalThreshLow, m_spatialThresh);
}

bool BaselineFormationNode::validateAndRestoreOutput()
{
    if (!m_temporalBaselines.isEmpty() && !m_spatialBaselines.isEmpty() && m_masterIndex >= 1 && !m_outputNodeName.isEmpty()) {
        QStringList filePaths = m_inputData ? m_inputData->filePaths() : QStringList();
        m_outputData = std::make_shared<ImportedFileData>(filePaths, m_outputNodeName);

        if (m_showChartBtn) m_showChartBtn->setEnabled(true);
        setState(ExecutionState::Completed);
        setProgress(100);
        updateLabels();
        Q_EMIT dataUpdated(0);
        return true;
    }
    return false;
}

QJsonObject BaselineFormationNode::save() const
{
    QJsonObject root = ExecutableNodeDelegateModel::save();
    root["useDefaultMaster"] = m_useDefaultMaster;
    root["masterIndex"] = m_masterIndex;
    root["spatialThresh"] = m_spatialThresh;
    root["temporalThresh"] = m_temporalThresh;
    root["temporalThreshLow"] = m_temporalThreshLow;
    root["outputNodeName"] = m_outputNodeName;

    QJsonArray tempArr;
    for (double t : m_temporalBaselines) tempArr.append(t);
    root["temporalBaselines"] = tempArr;

    QJsonArray spatArr;
    for (double s : m_spatialBaselines) spatArr.append(s);
    root["spatialBaselines"] = spatArr;

    return root;
}

void BaselineFormationNode::load(QJsonObject const& json)
{
    m_useDefaultMaster = json["useDefaultMaster"].toBool(true);
    m_masterIndex = json["masterIndex"].toInt(1);
    m_spatialThresh = json["spatialThresh"].toDouble(500.0);
    m_temporalThresh = json["temporalThresh"].toDouble(200.0);
    m_temporalThreshLow = json["temporalThreshLow"].toDouble(0.0);
    m_outputNodeName = json["outputNodeName"].toString(QStringLiteral("Baseline_Est"));

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

    ExecutableNodeDelegateModel::load(json);

    updateMasterImageCombo();
    updateLabels();
}

QStringList BaselineFormationNode::previewImagePaths() const
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

QString BaselineFormationNode::projectPath() const
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
