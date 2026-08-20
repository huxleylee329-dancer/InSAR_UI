#include "UnwrapNode.h"
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "InterfaceManager.h"
#include "WorkspaceUI.h"
#include "NodeUtils.h"
#include "icon_source.h"
#include "InSARLogManager.h"
#include "FormatConversion.h"
#include "Utils.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QDir>
#include <QApplication>
#include <QDateTime>
#include <QHash>
#include <QRect>
#include <QStandardItemModel>
#include <QDebug>
#include <QMessageBox>
#include <QTimer>
#include <QSignalBlocker>
#include <QtConcurrent/QtConcurrent>
#include <Unwrap.h>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <vector>
#include "QtNodes/internal/NodeDetailWindow.hpp"

namespace QtNodes {

UnwrapNode::UnwrapNode()
    : ExecutableNodeDelegateModel()
    , _widget(nullptr)
    , m_methodCombo(nullptr)
    , m_coherenceLabel(nullptr)
    , m_coherenceEdit(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_snaphuOptionsGroup(nullptr)
    , m_snaphuStatusLabel(nullptr)
    , m_snaphuTileRowsSpin(nullptr)
    , m_snaphuTileColsSpin(nullptr)
    , m_snaphuRowOverlapSpin(nullptr)
    , m_snaphuColOverlapSpin(nullptr)
    , m_snaphuTimeoutSpin(nullptr)
    , m_snaphuKeepArtifactsCheck(nullptr)
    , m_outputNodeName("")
    , m_method(1) // default: SPD Guided
    , m_coherenceThreshold(0.2)
    , m_workerThread(nullptr)
    , m_thread(nullptr)
    , m_snaphuLastUiHeartbeatMilliseconds(0)
{
    qRegisterMetaType<UnwrapFileResult>("UnwrapFileResult");
    qRegisterMetaType<SnaphuUiOptions>("SnaphuUiOptions");
    qRegisterMetaType<SnaphuRunEventInfo>("SnaphuRunEventInfo");
    setExecutionMode(ExecutionMode::Automatic);
}

UnwrapNode::~UnwrapNode()
{
    stopExecution();
}

unsigned int UnwrapNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 1;
    else
        return 2;
}

NodeDataType UnwrapNode::dataType(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    if (portType == PortType::In)
        return NodeDataType{"imported_file", "Imported File"};
    else
    {
        if (portIndex == 0)
            return NodeDataType{"imported_file", "Imported File"};
        else
            return NodeDataType{"image_info", "Image Info"};
    }
}

bool UnwrapNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return true;
}

QString UnwrapNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) {
        return QStringLiteral("输入图像");
    } else {
        if (portIndex == 0)
            return QStringLiteral("成果 *");
        else if (portIndex == 1)
            return QStringLiteral("预览 ?");
    }
    return QString();
}

bool UnwrapNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out && portIndex == 1)
        return true;
    return false;
}

void UnwrapNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    Q_UNUSED(port);
    m_inputData = std::dynamic_pointer_cast<ImportedFileData>(data);

    if (m_inputData && m_outputNodeName.isEmpty()) {
        m_outputNodeName = generateDefaultOutputName();
        if (m_outputNodeNameEdit) {
            m_outputNodeNameEdit->setText(m_outputNodeName);
        }
    }

    ExecutableNodeDelegateModel::setInData(data, port);

    if (!m_inputData || m_inputData->filePaths().isEmpty()) {
        m_outputData.reset();
        m_imageInfoData.reset();
    }
}

std::shared_ptr<NodeData> UnwrapNode::outData(PortIndex port)
{
    return ExecutableNodeDelegateModel::outData(port);
}

::QWidget* UnwrapNode::embeddedWidget()
{
    if (!_widget)
    {
        createWidget();
    }
    return _widget;
}

QJsonObject UnwrapNode::save() const
{
    QJsonObject modelJson = ExecutableNodeDelegateModel::save();

    modelJson["outputNodeName"] = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : m_outputNodeName;
    modelJson["method"] = m_method;
    modelJson["coherenceThreshold"] = m_coherenceEdit ? m_coherenceEdit->text().toDouble() : m_coherenceThreshold;
    modelJson["snaphuTileRows"] = static_cast<int>(m_snaphuOptions.tileRows);
    modelJson["snaphuTileCols"] = static_cast<int>(m_snaphuOptions.tileCols);
    modelJson["snaphuRowOverlap"] = static_cast<int>(m_snaphuOptions.rowOverlap);
    modelJson["snaphuColOverlap"] = static_cast<int>(m_snaphuOptions.colOverlap);
    modelJson["snaphuTimeoutSeconds"] = static_cast<qint64>(m_snaphuOptions.wallTimeoutMilliseconds / 1000);
    modelJson["snaphuKeepArtifactsOnSuccess"] = m_snaphuOptions.keepArtifactsOnSuccess;

    return modelJson;
}

void UnwrapNode::load(QJsonObject const &json)
{
    QJsonValue vName = json["outputNodeName"];
    if (!vName.isUndefined()) m_outputNodeName = vName.toString();

    QJsonValue vMethod = json["method"];
    if (!vMethod.isUndefined()) m_method = vMethod.toInt();

    QJsonValue vCoh = json["coherenceThreshold"];
    if (!vCoh.isUndefined()) m_coherenceThreshold = vCoh.toDouble();
    if (!json["snaphuTileRows"].isUndefined()) m_snaphuOptions.tileRows = qBound(1, json["snaphuTileRows"].toInt(), 256);
    if (!json["snaphuTileCols"].isUndefined()) m_snaphuOptions.tileCols = qBound(1, json["snaphuTileCols"].toInt(), 256);
    if (!json["snaphuRowOverlap"].isUndefined()) m_snaphuOptions.rowOverlap = qMax(0, json["snaphuRowOverlap"].toInt());
    if (!json["snaphuColOverlap"].isUndefined()) m_snaphuOptions.colOverlap = qMax(0, json["snaphuColOverlap"].toInt());
    if (!json["snaphuTimeoutSeconds"].isUndefined()) {
        m_snaphuOptions.wallTimeoutMilliseconds = static_cast<quint64>(qMax<qint64>(0, json["snaphuTimeoutSeconds"].toVariant().toLongLong())) * 1000;
    }
    if (!json["snaphuKeepArtifactsOnSuccess"].isUndefined()) m_snaphuOptions.keepArtifactsOnSuccess = json["snaphuKeepArtifactsOnSuccess"].toBool();

    // SOP Rule 15: load parameters BEFORE triggering validateAndRestoreOutput in base load
    ExecutableNodeDelegateModel::load(json);

    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setText(m_outputNodeName);
    if (m_methodCombo) {
        m_methodCombo->setCurrentIndex(m_method - 1);
    }
    if (m_coherenceEdit) m_coherenceEdit->setText(QString::number(m_coherenceThreshold));

    updateSnaphuOptionWidgets();
    onMethodChanged(m_method - 1);
}

void UnwrapNode::setExecutionMode(ExecutionMode mode)
{
    ExecutableNodeDelegateModel::setExecutionMode(mode);
}

void UnwrapNode::createWidget()
{
    _widget = new QWidget();
    _widget->setObjectName("NodeEmbeddedWidget");
    _widget->setFixedWidth(300);
    auto* formLayout = new QFormLayout(_widget);
    formLayout->setContentsMargins(6, 6, 6, 6);
    formLayout->setSpacing(6);
    formLayout->setLabelAlignment(Qt::AlignLeft);
    formLayout->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);

    auto invalidateNodeData = [this]() {
        if (m_outputData) m_outputData.reset();
        if (m_imageInfoData) m_imageInfoData.reset();
        setOutputData(0, nullptr);
        setOutputData(1, nullptr);
        invalidateExecution();
        if (_scene) {
            Q_EMIT _scene->modified(_scene);
        }
    };

    // 3. 解缠方法
    m_methodCombo = new QComboBox();
    m_methodCombo->setEditable(false);
    m_methodCombo->addItem("SPD Guided");
    m_methodCombo->addItem("MCF");
    m_methodCombo->addItem("SNAPHU");
    m_methodCombo->addItem("Quality Guided MCF");
    m_methodCombo->setCurrentIndex(m_method - 1);
    connect(m_methodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, invalidateNodeData](int index) {
        int val = index + 1;
        if (m_method != val) {
            if (!confirmParameterChange()) {
                m_methodCombo->blockSignals(true);
                m_methodCombo->setCurrentIndex(m_method - 1);
                m_methodCombo->blockSignals(false);
                return;
            }
            m_method = val;
            onMethodChanged(index);
            invalidateNodeData();
        }
    });

    // 4. 相干系数阈值
    m_coherenceLabel = new QLabel("相干系数阈值");
    m_coherenceEdit = new QLineEdit();
    m_coherenceEdit->setText(QString::number(m_coherenceThreshold));
    connect(m_coherenceEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        double val = m_coherenceEdit->text().toDouble();
        if (qAbs(m_coherenceThreshold - val) > 1e-6) {
            if (!confirmParameterChange()) {
                m_coherenceEdit->setText(QString::number(m_coherenceThreshold));
                return;
            }
            m_coherenceThreshold = val;
            invalidateNodeData();
        }
    });

    m_snaphuOptionsGroup = new QGroupBox(QStringLiteral("SNAPHU 高级参数"));
    auto* snaphuForm = new QFormLayout(m_snaphuOptionsGroup);
    m_snaphuTileRowsSpin = new QSpinBox();
    m_snaphuTileColsSpin = new QSpinBox();
    m_snaphuRowOverlapSpin = new QSpinBox();
    m_snaphuColOverlapSpin = new QSpinBox();
    m_snaphuTimeoutSpin = new QSpinBox();
    m_snaphuKeepArtifactsCheck = new QCheckBox(QStringLiteral("成功后保留 SNAPHU 现场文件"));
    m_snaphuStatusLabel = new QLabel(QStringLiteral("状态: 未运行"));
    m_snaphuStatusLabel->setWordWrap(true);
    m_snaphuTileRowsSpin->setRange(1, 256);
    m_snaphuTileColsSpin->setRange(1, 256);
    m_snaphuRowOverlapSpin->setRange(0, 100000);
    m_snaphuColOverlapSpin->setRange(0, 100000);
    m_snaphuTimeoutSpin->setRange(0, 30 * 24 * 60 * 60);
    m_snaphuTimeoutSpin->setSpecialValueText(QStringLiteral("不超时"));
    m_snaphuTimeoutSpin->setSuffix(QStringLiteral(" 秒"));
    snaphuForm->addRow(QStringLiteral("分块行数"), m_snaphuTileRowsSpin);
    snaphuForm->addRow(QStringLiteral("分块列数"), m_snaphuTileColsSpin);
    snaphuForm->addRow(QStringLiteral("行重叠像素"), m_snaphuRowOverlapSpin);
    snaphuForm->addRow(QStringLiteral("列重叠像素"), m_snaphuColOverlapSpin);
    snaphuForm->addRow(QStringLiteral("最长运行时间"), m_snaphuTimeoutSpin);
    snaphuForm->addRow(m_snaphuKeepArtifactsCheck);
    snaphuForm->addRow(m_snaphuStatusLabel);

    const auto applySnaphuChange = [this, invalidateNodeData]() {
        if (!confirmParameterChange()) {
            updateSnaphuOptionWidgets();
            return;
        }
        m_snaphuOptions.tileRows = static_cast<quint32>(m_snaphuTileRowsSpin->value());
        m_snaphuOptions.tileCols = static_cast<quint32>(m_snaphuTileColsSpin->value());
        m_snaphuOptions.rowOverlap = static_cast<quint32>(m_snaphuRowOverlapSpin->value());
        m_snaphuOptions.colOverlap = static_cast<quint32>(m_snaphuColOverlapSpin->value());
        m_snaphuOptions.wallTimeoutMilliseconds = static_cast<quint64>(m_snaphuTimeoutSpin->value()) * 1000;
        m_snaphuOptions.keepArtifactsOnSuccess = m_snaphuKeepArtifactsCheck->isChecked();
        updateSnaphuOptionWidgets();
        invalidateNodeData();
    };
    connect(m_snaphuTileRowsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, applySnaphuChange);
    connect(m_snaphuTileColsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, applySnaphuChange);
    connect(m_snaphuRowOverlapSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, applySnaphuChange);
    connect(m_snaphuColOverlapSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, applySnaphuChange);
    connect(m_snaphuTimeoutSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, applySnaphuChange);
    connect(m_snaphuKeepArtifactsCheck, &QCheckBox::toggled, this, applySnaphuChange);
    updateSnaphuOptionWidgets();

    // 5. 目标节点
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setText(m_outputNodeName);
    m_outputNodeNameEdit->setPlaceholderText(QStringLiteral("自动生成或手动输入"));
    connect(m_outputNodeNameEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString text = m_outputNodeNameEdit->text().trimmed();
        if (m_outputNodeName != text) {
            if (!confirmParameterChange()) {
                m_outputNodeNameEdit->setText(m_outputNodeName);
                return;
            }
            NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), m_outputNodeName);
            m_outputNodeName = text;
            invalidateNodeData();
        }
    });

    formLayout->addRow("解缠方法", m_methodCombo);
    formLayout->addRow(m_coherenceLabel, m_coherenceEdit);
    formLayout->addRow(m_snaphuOptionsGroup);
    formLayout->addRow("目标节点", m_outputNodeNameEdit);

    onMethodChanged(m_method - 1);
}

void UnwrapNode::onMethodChanged(int index)
{
    bool isQualityMCF = (index == 3);
    bool isSnaphu = (index == 2);

    if (m_coherenceLabel) m_coherenceLabel->setVisible(isQualityMCF);
    if (m_coherenceEdit) m_coherenceEdit->setVisible(isQualityMCF);
    if (m_snaphuOptionsGroup) m_snaphuOptionsGroup->setVisible(isSnaphu);

    updateWidgetSize();
}

void UnwrapNode::updateSnaphuOptionWidgets()
{
    const bool tiled = m_snaphuOptions.tileRows > 1 || m_snaphuOptions.tileCols > 1;
    if (tiled) {
        m_snaphuOptions.rowOverlap = qMax<quint32>(400, m_snaphuOptions.rowOverlap);
        m_snaphuOptions.colOverlap = qMax<quint32>(400, m_snaphuOptions.colOverlap);
    } else {
        m_snaphuOptions.rowOverlap = 0;
        m_snaphuOptions.colOverlap = 0;
    }
    if (!m_snaphuTileRowsSpin) return;
    const QSignalBlocker rowsBlocker(m_snaphuTileRowsSpin);
    const QSignalBlocker colsBlocker(m_snaphuTileColsSpin);
    const QSignalBlocker rowOverlapBlocker(m_snaphuRowOverlapSpin);
    const QSignalBlocker colOverlapBlocker(m_snaphuColOverlapSpin);
    const QSignalBlocker timeoutBlocker(m_snaphuTimeoutSpin);
    const QSignalBlocker artifactsBlocker(m_snaphuKeepArtifactsCheck);
    m_snaphuRowOverlapSpin->setMinimum(tiled ? 400 : 0);
    m_snaphuColOverlapSpin->setMinimum(tiled ? 400 : 0);
    m_snaphuTileRowsSpin->setValue(static_cast<int>(m_snaphuOptions.tileRows));
    m_snaphuTileColsSpin->setValue(static_cast<int>(m_snaphuOptions.tileCols));
    m_snaphuRowOverlapSpin->setValue(static_cast<int>(m_snaphuOptions.rowOverlap));
    m_snaphuColOverlapSpin->setValue(static_cast<int>(m_snaphuOptions.colOverlap));
    m_snaphuTimeoutSpin->setValue(static_cast<int>(m_snaphuOptions.wallTimeoutMilliseconds / 1000));
    m_snaphuKeepArtifactsCheck->setChecked(m_snaphuOptions.keepArtifactsOnSuccess);
}

void UnwrapNode::updateWidgetSize()
{
    if (_widget)
    {
        _widget->setFixedWidth(300);
        _widget->adjustSize();
        Q_EMIT embeddedWidgetSizeUpdated();
    }
}

QString UnwrapNode::generateDefaultOutputName() const
{
    if (m_inputData) {
        return m_inputData->nodeName() + "_Unwrapped";
    }
    return "Unwrapped_Phase";
}

bool UnwrapNode::validateInputs() const
{
    if (projectName().isEmpty()) {
        return false;
    }
    if (!m_inputData || m_inputData->filePaths().isEmpty()) {
        return false;
    }

    QString dstNode = m_outputNodeNameEdit ? m_outputNodeNameEdit->text().trimmed() : m_outputNodeName;
    if (dstNode.isEmpty()) {
        return false;
    }

    // Alphanumeric and underscore validation
    bool bFlag = dstNode.contains(QRegularExpression("^\\w+$"));
    if (!bFlag) {
        return false;
    }

    // Check parameters
    if (m_method == 4) {
        double threshold = m_coherenceEdit ? m_coherenceEdit->text().toDouble(&bFlag) : true;
        if (!bFlag || threshold < 0 || threshold > 1) {
            return false;
        }
    }
    if (m_method == 3) {
        const bool tiled = m_snaphuOptions.tileRows > 1 || m_snaphuOptions.tileCols > 1;
        if (m_snaphuOptions.tileRows == 0 || m_snaphuOptions.tileCols == 0 ||
            (tiled && (m_snaphuOptions.rowOverlap < 400 || m_snaphuOptions.colOverlap < 400))) {
            return false;
        }
    }

    return true;
}

bool UnwrapNode::prepareToStart()
{
    if (!validateInputs())
        return false;

    m_preparedDstNode = m_outputNodeNameEdit->text().trimmed().isEmpty()
        ? generateDefaultOutputName()
        : m_outputNodeNameEdit->text().trimmed();

    m_preparedSavePath = projectPath();
    m_preparedProjectName = projectName();
    m_preparedSrcNode = m_inputData->nodeName();
    m_preparedPhasePaths = m_inputData->filePaths();
    QString identityError;
    if (!NodeUtils::validateH5Identities(m_preparedPhasePaths, m_inputData->physicalProductDescriptor(),
                                         &identityError)) {
        setStartFailureMessage(identityError);
        setLastErrorMessage(identityError);
        return false;
    }

    m_preparedMethod = m_method;
    m_preparedThreshold = m_coherenceEdit ? m_coherenceEdit->text().toDouble() : m_coherenceThreshold;
    m_preparedSnaphuOptions = m_snaphuOptions;

    // Precalculate output file paths for overwrite check
    m_preparedOutputPaths.clear();
    for (const QString& srcPath : m_preparedPhasePaths) {
        QFileInfo fi(srcPath);
        QString changeName = fi.baseName() + "_unwrapped";
        m_preparedOutputPaths.append(m_preparedSavePath + "/" + m_preparedDstNode + "/" + changeName + ".h5");
    }

    // 自动触发时（上游数据更新），强制覆盖，保证数据链路一致性
    if (_isAutoTriggered) {
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
    } else {
        m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(NodeUtils::getProjectContext(_widget), m_preparedDstNode, m_preparedOutputPaths, nullptr);
    }

    return m_preparedOverwriteResult != NodeUtils::OverwriteResult::Cancel;
}

ProductInputContract UnwrapNode::productInputContract(PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    ProductInputContract contract;
    contract.semanticId = QStringLiteral("unwrap.input.filtered_interferogram");
    contract.allowedProductTypes = QStringList() << QStringLiteral("filtered_interferogram");
    contract.requiredProvenanceFields = QStringList()
        << QStringLiteral("producer") << QStringLiteral("output_port");
    return contract;
}

ProductOutputContract UnwrapNode::productOutputContract(PortIndex portIndex) const
{
    ProductOutputContract contract;
    contract.semanticId = portIndex == 0 ? QStringLiteral("unwrap.output.unwrapped_phase")
                                         : QStringLiteral("unwrap.output.preview");
    contract.publishedProductTypes = portIndex == 0
        ? QStringList{QStringLiteral("unwrapped_phase")} : QStringList{QStringLiteral("preview")};
    return contract;
}

void UnwrapNode::executeProcessing()
{
    InSARLogManager::LogDebug("UnwrapNode",
        QStringLiteral("executeProcessing started: method=%1, threshold=%2, inputs=%3")
            .arg(m_method).arg(m_preparedThreshold, 0, 'f', 2).arg(m_preparedPhasePaths.size()),
        "lifecycle.execute_processing");

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting) {
        m_outputNodeName = m_preparedDstNode;
        
        m_outputNodeNameEdit->setEnabled(true);
        m_methodCombo->setEnabled(true);
        onMethodChanged(m_method - 1);

        setState(ExecutionState::Running);
        setProgress(100);
        if (validateAndRestoreOutput()) {
            const QString warningMessage = m_outputData
                ? committedAmplitudeWarningMessage(m_outputData->filePaths()) : QString();
            if (!warningMessage.isEmpty()) {
                setLastWarningMessage(warningMessage);
                finishExecutionWithWarning();
            } else {
                finishExecution();
            }
        } else {
            setState(ExecutionState::Error);
        }
        return;
    }

    setProgress(0);
    setState(ExecutionState::Running);
    if (m_preparedMethod == 3 && m_snaphuStatusLabel) {
        m_snaphuLastUiHeartbeatMilliseconds = 0;
        m_snaphuStatusLabel->setText(QStringLiteral("SNAPHU 等待启动"));
    }
    QString transactionError;
    if (!NodeUtils::beginOutputTransaction(m_preparedSavePath, m_preparedDstNode,
                                           m_preparedOutputPaths, m_preparedPhasePaths,
                                           m_outputTransaction, &transactionError, nullptr,
                                           NodeUtils::getProjectFilePath(_widget))) {
        onError(transactionError);
        return;
    }
    m_outputTransaction.executionRevision = executionRevision();
    QMap<QString, QString> descriptorProvenance;
    descriptorProvenance.insert(QStringLiteral("producer"), name());
    descriptorProvenance.insert(QStringLiteral("output_port"),
                                QStringLiteral("unwrap.output.unwrapped_phase"));
    if (!NodeUtils::setOutputTransactionProductDescriptor(
            m_outputTransaction, ProductDescriptor::create(
                QStringLiteral("unwrapped_phase"), QStringLiteral("sat-explorer-product"), 1,
                ProductState::Committed, name(), descriptorProvenance), &transactionError)) {
        onError(transactionError);
        return;
    }
    m_pendingUnwrapResults.clear();
    m_pendingWarningMessage.clear();
    m_xmlDirty = false;
    m_outputData.reset();
    m_imageInfoData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);

    m_thread = new QThread();
    m_workerThread = new UnwrapWorker();
    m_workerThread->moveToThread(m_thread);

    connect(this, &UnwrapNode::startUnwrap, m_workerThread, &UnwrapWorker::Unwrap);
    const QString stagingNode = m_outputTransaction.stagingName;
    connect(m_thread, &QThread::started, [this, stagingNode]() {
        Q_EMIT startUnwrap(m_preparedMethod, m_preparedThreshold, m_preparedSavePath, stagingNode,
                           m_preparedPhasePaths, m_preparedSnaphuOptions);
    });
    connect(m_workerThread, &UnwrapWorker::unwrapFileGenerated, this, &UnwrapNode::onUnwrapFileGenerated);
    connect(m_workerThread, &UnwrapWorker::updateProcess, this, &UnwrapNode::onProgressUpdate);
    connect(m_workerThread, &UnwrapWorker::snaphuRunEvent, this, &UnwrapNode::onSnaphuRunEvent,
            Qt::QueuedConnection);
    connect(m_workerThread, &UnwrapWorker::endProcess, this, &UnwrapNode::onProcessingFinished);
    connect(m_workerThread, &UnwrapWorker::endProcess, m_thread, &QThread::quit);
    connect(m_workerThread, &UnwrapWorker::cancelled, this, &UnwrapNode::onCancelled);
    connect(m_workerThread, &UnwrapWorker::errorProcess, this, &UnwrapNode::onError);
    connect(m_workerThread, &UnwrapWorker::errorProcess, m_thread, &QThread::quit);
    connect(m_workerThread, &UnwrapWorker::destroyed, m_thread, &QThread::quit);
    connect(m_workerThread, &UnwrapWorker::cancelled, m_thread, &QThread::quit);
    connect(m_thread, &QThread::finished, m_workerThread, &UnwrapWorker::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);

    // Dynamic recovery of Running state for Automatic execution mode (SOP Rule 5)
    QTimer::singleShot(0, this, [this]() {
        if (m_thread && m_thread->isRunning())
            setState(ExecutionState::Running);
    });

    // Disable inputs during execution
    m_outputNodeNameEdit->setEnabled(false);
    m_methodCombo->setEnabled(false);
    if (m_coherenceEdit) m_coherenceEdit->setEnabled(false);
    if (m_snaphuOptionsGroup) m_snaphuOptionsGroup->setEnabled(false);

    deferAutomaticCompletion();
    m_thread->start();
}

void UnwrapNode::onProgressUpdate(int progress, const QString& message)
{
    Q_UNUSED(message);
    if (isAutomaticExecutionObsolete()) {
        return;
    }
    setProgress(progress);
}

void UnwrapNode::onSnaphuRunEvent(const SnaphuRunEventInfo& event)
{
if (event.type == SNAPHU_RUN_EVENT_PREPARED) {
        m_snaphuLastUiHeartbeatMilliseconds = 0;
        m_snaphuLastLogHeartbeatMilliseconds = 0;
        InSARLogManager::LogDiagnostic(InSARLogManager::LevelDebug, "UnwrapNode",
            QStringLiteral("SNAPHU staging: %1; config: %2").arg(event.taskDirectory, event.configPath),
            LogTargets(LogTarget::DebugConsole) | LogTarget::DiagnosticFile, QStringLiteral("snaphu.staging"));
    } else if (event.type == SNAPHU_RUN_EVENT_HEARTBEAT) {
        const bool shouldLog = m_snaphuLastLogHeartbeatMilliseconds == 0 ||
            event.elapsedMilliseconds >= m_snaphuLastLogHeartbeatMilliseconds + 60000;
        QStringList metrics;
        metrics.append(QStringLiteral("运行 %1 s").arg(event.elapsedMilliseconds / 1000));
        metrics.append((event.metricAvailability & SNAPHU_RUN_METRIC_CPU_TIME)
            ? QStringLiteral("CPU %1 s").arg(event.totalCpuMilliseconds / 1000) : QStringLiteral("CPU 未知"));
        metrics.append((event.metricAvailability & SNAPHU_RUN_METRIC_PEAK_JOB_MEMORY)
            ? QStringLiteral("内存 %1 MiB").arg(event.peakJobMemoryBytes / (1024 * 1024)) : QStringLiteral("内存未知"));
        metrics.append((event.metricAvailability & SNAPHU_RUN_METRIC_READ_BYTES)
            ? QStringLiteral("读取 %1 MiB").arg(event.readBytes / (1024 * 1024)) : QStringLiteral("读取未知"));
        metrics.append((event.metricAvailability & SNAPHU_RUN_METRIC_WRITE_BYTES)
            ? QStringLiteral("写入 %1 MiB").arg(event.writeBytes / (1024 * 1024)) : QStringLiteral("写入未知"));
        if (m_snaphuStatusLabel &&
            (m_snaphuLastUiHeartbeatMilliseconds == 0 ||
             event.elapsedMilliseconds >= m_snaphuLastUiHeartbeatMilliseconds + 5000)) {
            m_snaphuStatusLabel->setText(QStringLiteral("SNAPHU 正在运行，已用时 %1 s")
                .arg(event.elapsedMilliseconds / 1000));
            m_snaphuLastUiHeartbeatMilliseconds = event.elapsedMilliseconds;
        }
        if (shouldLog) {
            m_snaphuLastLogHeartbeatMilliseconds = event.elapsedMilliseconds;
            InSARLogManager::LogDiagnostic(InSARLogManager::LevelDebug, "UnwrapNode",
                QStringLiteral("SNAPHU running (%1): %2").arg(metrics.join(QStringLiteral(", ")), event.message),
                LogTargets(LogTarget::DebugConsole) | LogTarget::DiagnosticFile, QStringLiteral("snaphu.heartbeat"));
        }
    } else if (event.type == SNAPHU_RUN_EVENT_WARNING || event.type == SNAPHU_RUN_EVENT_LOG) {
        const InSARLogManager::LogLevel level = event.type == SNAPHU_RUN_EVENT_WARNING
            ? InSARLogManager::LevelWarning : InSARLogManager::LevelDebug;
        InSARLogManager::LogDiagnostic(level, "UnwrapNode", QStringLiteral("SNAPHU: %1").arg(event.message),
            LogTargets(LogTarget::DebugConsole) | LogTarget::DiagnosticFile, QStringLiteral("snaphu.output"));
    }
}

void UnwrapNode::onProcessingFinished()
{
    QStringList h5Paths;
    QStringList jpgPaths;
    QStringList types;
    QList<UnwrapFileResult> committedResults;
    const QString dstNode = m_preparedDstNode;

    cleanUpThreadAndWorker();
    if (discardObsoleteAutomaticExecution()) {
        NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("obsolete automatic execution"), projectXml());
        m_outputData.reset();
        m_imageInfoData.reset();
        setOutputData(0, nullptr);
        setOutputData(1, nullptr);
        if (m_preparedMethod == 3) {
            if (m_snaphuStatusLabel) m_snaphuStatusLabel->setText(QStringLiteral("SNAPHU 已由输入变更取消"));
            if (m_snaphuOptionsGroup) m_snaphuOptionsGroup->setEnabled(true);
            onMethodChanged(m_method - 1);
        }
        return;
    }

    OutputCommitLease commitLease = acquireOutputCommitLease(m_outputTransaction.executionRevision);
    if (!commitLease) {
        NodeUtils::abandonOutputTransaction(m_outputTransaction,
                                            QStringLiteral("obsolete execution revision"), projectXml());
        return;
    }

    QString transactionError;
    if (!projectXml() ||
        !NodeUtils::validateStagedOutputTransaction(m_outputTransaction, &transactionError) ||
        !NodeUtils::validateStagedH5Datasets(m_outputTransaction, QStringList() << QStringLiteral("phase"), &transactionError)) {
        onError(transactionError.isEmpty() ? QStringLiteral("Project XML context is unavailable for unwrap output commit.") : transactionError);
        return;
    }
    QStringList workerPaths;
    for (const UnwrapFileResult& result : m_pendingUnwrapResults) workerPaths.append(result.absolutePath);
    if (!NodeUtils::workerOutputsMatchManifest(m_preparedOutputPaths, workerPaths, &transactionError) ||
        !NodeUtils::promoteOutputTransaction(m_outputTransaction, h5Paths, &transactionError) ||
        !NodeUtils::prepareOutputTransactionMetadataCommit(m_outputTransaction, projectXml(), NodeUtils::getProjectFilePath(_widget), &transactionError)) {
        onError(transactionError);
        return;
    }
    NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), dstNode, false, false);
    m_xmlDirty = false;
    for (UnwrapFileResult result : m_pendingUnwrapResults) {
        const QString fileName = QFileInfo(result.absolutePath).fileName();
        result.absolutePath = QDir(projectPath() + "/" + dstNode).absoluteFilePath(fileName);
        result.relativePath = QStringLiteral("/%1/%2").arg(dstNode, fileName);
        commitUnwrapResult(result);
        committedResults.append(result);
    }
    if (!m_xmlDirty || !NodeUtils::saveProjectXmlAtomically(projectXml(), NodeUtils::getProjectFilePath(_widget), &transactionError) ||
        !NodeUtils::markOutputTransactionMetadataCommitted(m_outputTransaction, &transactionError)) {
        onError(transactionError.isEmpty() ? QStringLiteral("Unwrap output metadata was not produced.") : transactionError);
        return;
    }
    NodeUtils::removeDataNodeFromProjectTree(NodeUtils::getProjectContext(_widget), dstNode);
    for (const UnwrapFileResult& result : committedResults) publishUnwrapResultToProjectTree(result);
    if (auto* iface = NodeUtils::getProjectContext(_widget)) iface->refreshProjectTree();

    QStringList amplitudeWarnings;
    for (const UnwrapFileResult& result : committedResults) {
        if (!result.amplitudeDegraded) {
            continue;
        }
        QString detail = QStringLiteral("%1: status=%2, reason=%3")
            .arg(result.unwrapName, result.amplitudeStatus, result.amplitudeReason);
        if (result.amplitudeExpectedRows > 0 && result.amplitudeExpectedCols > 0) {
            detail += QStringLiteral(", expected=%1x%2")
                .arg(result.amplitudeExpectedRows).arg(result.amplitudeExpectedCols);
        }
        if (result.amplitudeMasterRows > 0 && result.amplitudeMasterCols > 0) {
            detail += QStringLiteral(", master=%1x%2")
                .arg(result.amplitudeMasterRows).arg(result.amplitudeMasterCols);
        }
        if (result.amplitudeSlaveRows > 0 && result.amplitudeSlaveCols > 0) {
            detail += QStringLiteral(", slave=%1x%2")
                .arg(result.amplitudeSlaveRows).arg(result.amplitudeSlaveCols);
        }
        amplitudeWarnings.append(detail);
    }
    m_pendingWarningMessage = amplitudeWarnings.isEmpty()
        ? QString()
        : QStringLiteral("SNAPHU 解缠完成，但未使用幅度约束：\n%1")
              .arg(amplitudeWarnings.join('\n'));

    for (const QString& h5Path : h5Paths) {
        jpgPaths.append(QFileInfo(h5Path).absolutePath() + "/" + QFileInfo(h5Path).baseName() + ".jpg");
        types.append(QStringLiteral("phase"));
    }

    m_outputData = std::make_shared<ImportedFileData>(h5Paths, dstNode);
    setOutputData(0, m_outputData);

    if (!h5Paths.isEmpty())
    {
        startPreviewGeneration(h5Paths, jpgPaths, types, h5Paths, jpgPaths, true);
    }
    else
    {
        m_imageInfoData.reset();
        setOutputData(1, nullptr);

        m_outputNodeNameEdit->setEnabled(true);
        m_methodCombo->setEnabled(true);
        if (m_coherenceEdit) m_coherenceEdit->setEnabled(true);
        if (m_snaphuOptionsGroup) m_snaphuOptionsGroup->setEnabled(true);
        onMethodChanged(m_method - 1);

        setState(ExecutionState::Running);
        setProgress(100);
        if (m_preparedMethod == 3 && m_snaphuStatusLabel) {
            m_snaphuStatusLabel->setText(QStringLiteral("SNAPHU 已完成"));
        }
        InSARLogManager::LogInfo("UnwrapNode", "executeProcessing completed (empty output list).");
        finishExecution();
    }
}

void UnwrapNode::onCancelled()
{
    if (m_preparedMethod == 3 && m_snaphuStatusLabel) {
        m_snaphuStatusLabel->setText(QStringLiteral("SNAPHU 已取消"));
    }
    InSARLogManager::LogInfo("UnwrapNode", "Unwrap cancellation cleanup completed.");
    cleanUpThreadAndWorker();
    NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("cancelled"), projectXml());
    m_outputData.reset();
    m_imageInfoData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);
    if (discardObsoleteAutomaticExecution()) {
        if (m_preparedMethod == 3) {
            if (m_snaphuStatusLabel) m_snaphuStatusLabel->setText(QStringLiteral("SNAPHU 已由输入变更取消"));
            if (m_snaphuOptionsGroup) m_snaphuOptionsGroup->setEnabled(true);
            onMethodChanged(m_method - 1);
        }
        return;
    }

    setState(ExecutionState::Stopped);
    Q_EMIT executionStopped();
    Q_EMIT computingFinished();
    m_outputNodeNameEdit->setEnabled(true);
    m_methodCombo->setEnabled(true);
    if (m_coherenceEdit) m_coherenceEdit->setEnabled(true);
    if (m_snaphuOptionsGroup) m_snaphuOptionsGroup->setEnabled(true);
}

void UnwrapNode::onError(const QString& error)
{
    if (m_preparedMethod == 3 && m_snaphuStatusLabel) {
        m_snaphuStatusLabel->setText(QStringLiteral("SNAPHU 失败"));
    }
    cleanUpThreadAndWorker();
    NodeUtils::abandonOutputTransaction(m_outputTransaction, error, projectXml());
    m_outputData.reset();
    m_imageInfoData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);
    if (discardObsoleteAutomaticExecution()) {
        if (m_preparedMethod == 3) {
            if (m_snaphuStatusLabel) m_snaphuStatusLabel->setText(QStringLiteral("SNAPHU 已由输入变更取消"));
            if (m_snaphuOptionsGroup) m_snaphuOptionsGroup->setEnabled(true);
            onMethodChanged(m_method - 1);
        }
        return;
    }

    m_outputNodeNameEdit->setEnabled(true);
    m_methodCombo->setEnabled(true);
    if (m_coherenceEdit) m_coherenceEdit->setEnabled(true);
    if (m_snaphuOptionsGroup) m_snaphuOptionsGroup->setEnabled(true);
    onMethodChanged(m_method - 1);

    setLastErrorMessage(error);
    Q_EMIT executionError(error);
    setState(ExecutionState::Error);
}

void UnwrapNode::onModelUpdated(QStandardItemModel* model)
{
    if (isAutomaticExecutionObsolete()) {
        return;
    }

    Q_UNUSED(model);
    auto iface = NodeUtils::getProjectContext(_widget);
    if (iface) {
        iface->refreshProjectTree();
    }
}

void UnwrapNode::onUnwrapFileGenerated(const UnwrapFileResult& result)
{
    m_pendingUnwrapResults.append(result);
}

void UnwrapNode::commitUnwrapResult(const UnwrapFileResult& result)
{
    if (XMLFile* xml = projectXml()) {
        xml->XMLFile_add_unwrap(m_preparedDstNode.toStdString().c_str(), result.unwrapName.toStdString().c_str(),
                                result.relativePath.toStdString().c_str(), result.offsetRow, result.offsetCol,
                                result.method.toStdString().c_str(), 0);
        m_xmlDirty = true;
    }
}

void UnwrapNode::publishUnwrapResultToProjectTree(const UnwrapFileResult& result)
{
    QStandardItemModel* model = projectModel();
    if (!model) return;
    const QList<QStandardItem*> projects = model->findItems(m_preparedProjectName);
    if (projects.isEmpty()) return;
    QStandardItem* unwrapNode = NodeUtils::findOrCreateProjectNode(
        projects.first(), m_preparedDstNode, "phase-3.0", FOLDER_ICON);
    if (unwrapNode) {
        unwrapNode->setToolTip(m_preparedProjectName);
        NodeUtils::findOrCreateChildItem(unwrapNode, result.unwrapName, "phase", result.absolutePath, IMAGEDATA_ICON);
    }
}

bool UnwrapNode::validateAndRestoreOutput()
{
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty())
        return false;

    QStringList h5Paths;
    ProductDescriptor::Ptr descriptor;
    QString identityError;
    if (!NodeUtils::loadCommittedOutputManifest(projectPath(), dstNode, h5Paths) ||
        !NodeUtils::loadCommittedOutputProductDescriptor(projectPath(), dstNode, descriptor, &identityError) ||
        !validatePublishedDescriptor(productOutputContract(0), descriptor).accepted ||
        !NodeUtils::validateH5Identities(h5Paths, descriptor, &identityError)) return false;
    QStringList expectedJpgPaths;
    QStringList types;

    for (const QString& h5Path : h5Paths) {
        QFileInfo info(h5Path);
        expectedJpgPaths.append(info.absolutePath() + "/" + info.baseName() + ".jpg");
        types.append("phase");
    }

    m_outputData = std::make_shared<ImportedFileData>(h5Paths, dstNode);
    m_outputData->setProductDescriptor(descriptor);
    setOutputData(0, m_outputData);
    Q_EMIT dataUpdated(0);

    // Remedy missing JPG previews in background (SOP Rule 15)
    QStringList existingJpgPaths;
    QStringList missingH5s;
    QStringList missingJpgs;
    QStringList missingTypes;

    for (int i = 0; i < expectedJpgPaths.size(); ++i) {
        if (NodeUtils::isJpgPreviewCurrent(h5Paths[i], expectedJpgPaths[i])) {
            existingJpgPaths.append(expectedJpgPaths[i]);
        } else {
            missingH5s.append(h5Paths[i]);
            missingJpgs.append(expectedJpgPaths[i]);
            missingTypes.append(types[i]);
        }
    }

    if (missingH5s.isEmpty()) {
        m_imageInfoData = std::make_shared<ImageInfoData>(existingJpgPaths);
        setOutputData(1, m_imageInfoData);
        Q_EMIT dataUpdated(1);
    } else {
        startPreviewGeneration(missingH5s, missingJpgs, missingTypes, h5Paths, expectedJpgPaths, false);
    }

    return true;
}

void UnwrapNode::startPreviewGeneration(const QStringList& h5Paths,
                                        const QStringList& generatedJpgPaths,
                                        const QStringList& types,
                                        const QStringList& resultH5Paths,
                                        const QStringList& resultJpgPaths,
                                        bool completeExecution)
{
    if (m_remedyWatcher.isRunning()) {
        m_remedyWatcher.cancel();
        m_remedyWatcher.disconnect(this);
        connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this,
                [this, h5Paths, generatedJpgPaths, types, resultH5Paths, resultJpgPaths, completeExecution]() {
            m_remedyWatcher.disconnect(this);
            startPreviewGeneration(h5Paths, generatedJpgPaths, types, resultH5Paths, resultJpgPaths, completeExecution);
        });
        return;
    }

    m_remedyWatcher.disconnect(this);
    connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this,
            [this, resultH5Paths, resultJpgPaths, completeExecution]() {
        QStringList currentJpgPaths;
        for (int i = 0; i < resultH5Paths.size() && i < resultJpgPaths.size(); ++i) {
            if (NodeUtils::isJpgPreviewCurrent(resultH5Paths[i], resultJpgPaths[i])) currentJpgPaths.append(resultJpgPaths[i]);
        }
        if (completeExecution) {
            if (discardObsoleteAutomaticExecution()) {
                m_pendingWarningMessage.clear();
                if (m_preparedMethod == 3) {
                    if (m_snaphuStatusLabel) m_snaphuStatusLabel->setText(QStringLiteral("SNAPHU 已由输入变更取消"));
                    if (m_snaphuOptionsGroup) m_snaphuOptionsGroup->setEnabled(true);
                    onMethodChanged(m_method - 1);
                }
                return;
            }

            m_imageInfoData = std::make_shared<ImageInfoData>(currentJpgPaths);
            setOutputData(1, m_imageInfoData);
            m_outputNodeNameEdit->setEnabled(true);
            m_methodCombo->setEnabled(true);
            if (m_coherenceEdit) m_coherenceEdit->setEnabled(true);
            if (m_snaphuOptionsGroup) m_snaphuOptionsGroup->setEnabled(true);
            onMethodChanged(m_method - 1);
            setState(ExecutionState::Running);
            setProgress(100);
            if (m_preparedMethod == 3 && m_snaphuStatusLabel) {
                m_snaphuStatusLabel->setText(QStringLiteral("SNAPHU 已完成"));
            }
            InSARLogManager::LogInfo("UnwrapNode", "executeProcessing completed.");
            if (!m_pendingWarningMessage.isEmpty()) {
                setLastWarningMessage(m_pendingWarningMessage);
                finishExecutionWithWarning();
            } else {
                finishExecution();
            }
            m_pendingWarningMessage.clear();
        } else {
            m_imageInfoData = std::make_shared<ImageInfoData>(currentJpgPaths);
            setOutputData(1, m_imageInfoData);
            Q_EMIT dataUpdated(1);
            InSARLogManager::LogInfo("UnwrapNode", "validateAndRestoreOutput background rendering completed.");
        }
    });
    m_remedyWatcher.setFuture(QtConcurrent::run([h5Paths, generatedJpgPaths, types]() {
        for (int i = 0; i < h5Paths.size(); ++i) {
            NodeUtils::generateJpgPreviewFromH5(h5Paths[i], generatedJpgPaths[i], types[i]);
        }
    }));
}

QStringList UnwrapNode::previewImagePaths() const
{
    QStringList list;
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty())
        return list;

    QStringList h5Paths;
    if (NodeUtils::loadCommittedOutputManifest(projectPath(), dstNode, h5Paths)) {
        for (const QString& h5Path : h5Paths) {
            QFileInfo info(h5Path);
            QString jpgPath = info.absolutePath() + "/" + info.baseName() + ".jpg";
            if (QFile::exists(jpgPath)) {
                list.append(jpgPath);
            }
        }
    }
    return list;
}

QStandardItemModel* UnwrapNode::projectModel() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectModel() : nullptr;
}

QString UnwrapNode::projectPath() const
{
    IApplicationInterface* iface = nullptr;
    if (_widget) iface = NodeUtils::getProjectContext(_widget);
    if (!iface) {
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (auto* mainWin = qobject_cast<MainWindow*>(w)) {
                if (mainWin->workspaceUI()) { iface = mainWin->workspaceUI(); break; }
                if (mainWin->interfaceManager()) { iface = mainWin->interfaceManager()->currentInterface(); if (iface) break; }
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

QString UnwrapNode::projectName() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectName() : QString();
}

XMLFile* UnwrapNode::projectXml() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectXml() : nullptr;
}

QString UnwrapNode::committedAmplitudeWarningMessage(const QStringList& h5Paths) const
{
    QStringList warnings;
    for (const QString& h5Path : h5Paths) {
        std::string status;
        int degraded = 0;
        int method = 0;
        const bool hasMethod = NodeUtils::readScalarFromH5(h5Path, "unwrap_method", method);
        const bool hasStatus = NodeUtils::readStringFromH5(h5Path, "unwrap_amplitude_status", status);
        const bool hasDegraded = NodeUtils::readScalarFromH5(h5Path, "unwrap_amplitude_degraded", degraded);
        if (!hasStatus || !hasDegraded) {
            if (hasMethod && method == 3) {
                warnings.append(QStringLiteral("%1: 幅度约束元数据缺失")
                    .arg(QFileInfo(h5Path).baseName()));
            }
            continue;
        }
        if (degraded == 0) {
            continue;
        }
        std::string reason;
        NodeUtils::readStringFromH5(h5Path, "unwrap_amplitude_reason", reason);
        warnings.append(QStringLiteral("%1: status=%2, reason=%3")
            .arg(QFileInfo(h5Path).baseName(), QString::fromStdString(status), QString::fromStdString(reason)));
    }
    return warnings.isEmpty()
        ? QString()
        : QStringLiteral("SNAPHU 解缠完成，但未使用幅度约束：\n%1").arg(warnings.join('\n'));
}

void UnwrapNode::execute()
{
    executeProcessing();
}

void UnwrapNode::stopExecution()
{
    if (m_workerThread)
    {
        m_workerThread->StopProcess();
    }
}

void UnwrapNode::cleanUpThreadAndWorker()
{
    QThread* thread = m_thread;
    m_thread = nullptr;
    m_workerThread = nullptr;
    if (thread && thread->isRunning())
        thread->quit();
}

void UnwrapNode::processAutomatically()
{
    if (prepareToStart())
    {
        executeProcessing();
    }
    else
    {
        setState(ExecutionState::Idle);
    }
}

namespace {

QString unwrapMethodName(int method)
{
    switch (method) {
    case 1: return QStringLiteral("SPD Guided");
    case 2: return QStringLiteral("MCF");
    case 3: return QStringLiteral("SNAPHU");
    case 4: return QStringLiteral("Quality Guided MCF");
    default: return QObject::tr("未知");
    }
}

struct UnwrapImageDiagnostics
{
    QString name;
    bool outputFound = false;
    bool dimensionsMatch = false;
    int inputRows = 0;
    int inputCols = 0;
    int outputRows = 0;
    int outputCols = 0;
    qint64 inputFinite = 0;
    qint64 pairedFinite = 0;
    qint64 outputFinite = 0;
    double rewrapRmse = 0.0;
    double rewrapP95 = 0.0;
    bool hasRewrapMetrics = false;
    int validComponentCount = 0;
    qint64 largestValidComponentPixels = 0;
    double largestValidComponentRatio = 0.0;
    qint64 gradientComparedEdges = 0;
    qint64 gradientRiskEdges = 0;
    qint64 candidateJumpPoints = 0;
    int gradientRiskComponentCount = 0;
    qint64 largestGradientRiskPixels = 0;
    QRect largestGradientRiskBounds;
    bool hasSpatialMetrics = false;
    bool amplitudeMetadataPresent = false;
    bool amplitudeDegraded = false;
    QString amplitudeStatus;
    QString amplitudeReason;
    int amplitudeMasterRows = 0;
    int amplitudeMasterCols = 0;
    int amplitudeSlaveRows = 0;
    int amplitudeSlaveCols = 0;
    int amplitudeExpectedRows = 0;
    int amplitudeExpectedCols = 0;
};

struct UnwrapValidationResults
{
    bool success = false;
    QString errorMessage;
    int expectedMethod = 1;
    double expectedThreshold = 0.2;
    bool hasRecordedMethod = false;
    int recordedMethod = 0;
    bool hasRecordedThreshold = false;
    double recordedThreshold = 0.0;
    bool outputMetadataConsistent = true;
    QList<UnwrapImageDiagnostics> images;
};

// 为表格生成简短的影像标识（编号 + 主辅日期对），完整文件名放在 hover 提示中展示。
QString shortImageLabel(int displayIndex, const QString& baseName)
{
    static const QRegularExpression datePattern(QStringLiteral("\\b\\d{8}\\b"));
    QStringList dates;
    QRegularExpressionMatchIterator dateIt = datePattern.globalMatch(baseName);
    while (dateIt.hasNext() && dates.size() < 2) {
        dates.append(dateIt.next().captured());
    }
    if (dates.size() >= 2) {
        return QObject::tr("影像 %1 (%2-%3)").arg(displayIndex + 1)
            .arg(dates.value(0), dates.value(1));
    }
    return QObject::tr("影像 %1").arg(displayIndex + 1);
}

// SNAPHU 幅度约束状态与原因的本地化文案（与 UnwrapWorker 写入的枚举镜像对齐）。
QString amplitudeStatusText(const QString& status)
{
    if (status == QStringLiteral("used")) return QObject::tr("已使用幅度约束");
    if (status == QStringLiteral("omitted_dimension_mismatch")) return QObject::tr("未使用幅度约束");
    if (status == QStringLiteral("unavailable")) return QObject::tr("未使用幅度约束");
    if (status == QStringLiteral("unknown")) return QObject::tr("幅度约束状态未知");
    if (status == QStringLiteral("not_applicable")) return QObject::tr("不适用（非 SNAPHU）");
    return status;
}

QString amplitudeReasonText(const QString& reason)
{
    if (reason == QStringLiteral("none")) return QString();
    if (reason == QStringLiteral("source_amplitude_dimensions_do_not_match_phase"))
        return QObject::tr("主/辅幅度尺寸与相位不匹配");
    if (reason == QStringLiteral("source_amplitude_unavailable")) return QObject::tr("源幅度不可用");
    if (reason == QStringLiteral("missing_amplitude_diagnostic")) return QObject::tr("缺少幅度诊断信息");
    return reason;
}

struct UnwrapAmplitudeDisplay
{
    QString value;
    QString conclusion;
    bool warning = false;
};

// 构建单景幅度约束的展示文本与结论（降级时附原因与尺寸明细，正常时附约束网格）。
UnwrapAmplitudeDisplay amplitudeDisplayInfo(const UnwrapImageDiagnostics& image)
{
    UnwrapAmplitudeDisplay display;
    QString value = amplitudeStatusText(image.amplitudeStatus);
    if (image.amplitudeDegraded) {
        const QString reason = amplitudeReasonText(image.amplitudeReason);
        if (!reason.isEmpty()) {
            value += QObject::tr("：%1").arg(reason);
        }
        if (image.amplitudeExpectedRows > 0 && image.amplitudeExpectedCols > 0) {
            value += QObject::tr("（期望 %1x%2")
                .arg(image.amplitudeExpectedRows).arg(image.amplitudeExpectedCols);
            if (image.amplitudeMasterRows > 0 && image.amplitudeMasterCols > 0) {
                value += QObject::tr("，主幅度 %1x%2")
                    .arg(image.amplitudeMasterRows).arg(image.amplitudeMasterCols);
            }
            if (image.amplitudeSlaveRows > 0 && image.amplitudeSlaveCols > 0) {
                value += QObject::tr("，辅幅度 %1x%2")
                    .arg(image.amplitudeSlaveRows).arg(image.amplitudeSlaveCols);
            }
            value += QStringLiteral(")");
        }
        display.conclusion = QObject::tr("需复查");
        display.warning = true;
    } else {
        if (image.amplitudeExpectedRows > 0 && image.amplitudeExpectedCols > 0) {
            value += QObject::tr("（约束网格 %1x%2）")
                .arg(image.amplitudeExpectedRows).arg(image.amplitudeExpectedCols);
        }
        display.conclusion = QObject::tr("正常");
    }
    display.value = value;
    return display;
}

class UnwrapValidationWidget : public BaseValidationWidget
{
public:
    UnwrapValidationWidget(UnwrapNode* node, QWidget* parent)
        : BaseValidationWidget(node, parent)
        , m_node(node)
    {
        setupUI();
        startAsyncValidation();
    }

private:
    void setupUI()
    {
        setupBaseUI(QObject::tr("正在诊断解缠结果..."),
                    QObject::tr("正在读取全部输入与输出 H5 数据，检查完整性、重新缠绕一致性和空间风险线索。"),
                    QObject::tr("解缠诊断汇总"),
                    QObject::tr("解缠参数与结果诊断"), true, true);

        m_coverageLabel = createFeatureLabel();
        m_rewrapRmseLabel = createFeatureLabel();
        m_rewrapP95Label = createFeatureLabel();
        m_componentLabel = createFeatureLabel();
        m_largestComponentLabel = createFeatureLabel();
        m_candidateJumpEdgeLabel = createFeatureLabel();
        m_candidateJumpPointLabel = createFeatureLabel();
        m_riskRegionLabel = createFeatureLabel();
        m_amplitudeLabel = createFeatureLabel();
        m_missingLabel = createFeatureLabel();
        QGridLayout* featureGrid = replaceFeatureFormWithGrid();
        const auto addMetric = [this, featureGrid](int row, int column, int columnSpan,
            const QString& title, QLabel* value) {
            QWidget* metric = new QWidget(m_featureCard);
            QHBoxLayout* metricLayout = new QHBoxLayout(metric);
            metricLayout->setContentsMargins(0, 0, 0, 0);
            metricLayout->setSpacing(8);
            QLabel* titleLabel = createHeaderLabel(title);
            titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            metricLayout->addWidget(titleLabel, 2);
            metricLayout->addWidget(value, 1);
            featureGrid->addWidget(metric, row, column, 1, columnSpan);
        };

        addMetric(0, 0, 1, QObject::tr("配对有效像元覆盖率:"), m_coverageLabel);
        addMetric(0, 1, 1, QObject::tr("重新缠绕圆差 RMSE (rad):"), m_rewrapRmseLabel);
        addMetric(1, 0, 1, QObject::tr("重新缠绕圆差 P95 估计 (rad):"), m_rewrapP95Label);
        addMetric(1, 1, 1, QObject::tr("有效输出连通域:"), m_componentLabel);
        addMetric(2, 0, 1, QObject::tr("最大连通域占比:"), m_largestComponentLabel);
        addMetric(2, 1, 1, QObject::tr("候选跳变边密度 (|delta| > pi，候选边/有效相邻边):"), m_candidateJumpEdgeLabel);
        addMetric(3, 0, 1, QObject::tr("候选跳变点密度 (候选点/有效输出像元):"), m_candidateJumpPointLabel);
        addMetric(3, 1, 1, QObject::tr("最大候选跳变区域:"), m_riskRegionLabel);
        addMetric(4, 0, 1, QObject::tr("SNAPHU 幅度约束:"), m_amplitudeLabel);
        addMetric(4, 1, 1, QObject::tr("缺失或尺寸异常结果:"), m_missingLabel);
    }

    void setLabelsState(const QString& stateText)
    {
        m_coverageLabel->setText(stateText);
        m_rewrapRmseLabel->setText(stateText);
        m_rewrapP95Label->setText(stateText);
        m_componentLabel->setText(stateText);
        m_largestComponentLabel->setText(stateText);
        m_candidateJumpEdgeLabel->setText(stateText);
        m_candidateJumpPointLabel->setText(stateText);
        m_riskRegionLabel->setText(stateText);
        m_amplitudeLabel->setText(stateText);
        m_missingLabel->setText(stateText);
    }

    void setNotExecutedState()
    {
        m_statusTitle->setText(QObject::tr("诊断不可用"));
        m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
        m_statusDesc->setText(QObject::tr("请先成功执行解缠节点，再查看诊断结果。"));
        m_compTable->clearComparison();
        m_compTable->setEnabled(false);
        setLabelsState(QObject::tr("未执行"));
    }

    void startAsyncValidation() override
    {
        const quint64 currentEpoch = ++m_validationEpoch;
        if (m_cancelToken) {
            m_cancelToken->store(true);
        }
        m_cancelToken = std::make_shared<std::atomic_bool>(false);
        auto cancelToken = m_cancelToken;

        m_isTimedOut = false;
        if (m_node->executionState() != ExecutionState::Completed) {
            setNotExecutedState();
            return;
        }

        const auto inputData = std::dynamic_pointer_cast<ImportedFileData>(m_node->getInputData(0));
        const auto outputData = std::dynamic_pointer_cast<ImportedFileData>(m_node->outData(0));
        if (!inputData || inputData->filePaths().isEmpty() || !outputData || outputData->filePaths().isEmpty()) {
            m_statusTitle->setText(QObject::tr("诊断失败"));
            m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
            m_statusDesc->setText(QObject::tr("未找到完整的输入或输出 H5 文件列表。"));
            m_compTable->clearComparison();
            m_compTable->setEnabled(false);
            setLabelsState(QObject::tr("诊断失败"));
            return;
        }

        const QStringList inputPaths = inputData->filePaths();
        const QStringList outputPaths = outputData->filePaths();
        const QJsonObject settings = m_node->save();
        const int expectedMethod = settings.value("method").toInt(1);
        const double expectedThreshold = settings.value("coherenceThreshold").toDouble(0.2);
        m_loadingOverlay->startLoading(QObject::tr("正在计算全部影像的解缠诊断..."));

        QFuture<UnwrapValidationResults> future = QtConcurrent::run([inputPaths, outputPaths, expectedMethod, expectedThreshold, cancelToken]() {
            UnwrapValidationResults result;
            result.expectedMethod = expectedMethod;
            result.expectedThreshold = expectedThreshold;

            QHash<QString, QString> fallbackOutputsByBaseName;
            for (const QString& outputPath : outputPaths) {
                const QString base = QFileInfo(outputPath).baseName();
                if (!fallbackOutputsByBaseName.contains(base)) {
                    fallbackOutputsByBaseName.insert(base, outputPath);
                }
            }

            bool firstMetadata = true;

            for (int i = 0; i < inputPaths.size(); ++i) {
                if (cancelToken && cancelToken->load()) {
                    result.images.clear();
                    result.success = false;
                    result.errorMessage = QObject::tr("诊断任务已取消。");
                    return result;
                }

                const QString& inputPath = inputPaths[i];
                UnwrapImageDiagnostics image;
                image.name = QFileInfo(inputPath).baseName();
                const QString expectedOutputBase = image.name + QStringLiteral("_unwrapped");

                QString outputPath;
                if (i < outputPaths.size() && QFileInfo(outputPaths[i]).baseName() == expectedOutputBase) {
                    outputPath = outputPaths[i];
                } else {
                    outputPath = fallbackOutputsByBaseName.value(expectedOutputBase);
                }
                image.outputFound = !outputPath.isEmpty();

                if (!image.outputFound) {
                    result.images.append(image);
                    continue;
                }

                int method = 0;
                double threshold = 0.0;
                const bool hasMethod = NodeUtils::readScalarFromH5(outputPath, "unwrap_method", method);
                const bool hasThreshold = NodeUtils::readScalarFromH5(outputPath, "unwrap_coherence_threshold", threshold);

                const bool methodMismatch = (!hasMethod || !result.hasRecordedMethod || method != result.recordedMethod);
                const bool thresholdMismatch = (expectedMethod == 4) &&
                    (!hasThreshold || !result.hasRecordedThreshold || std::abs(threshold - result.recordedThreshold) > 1e-9);

                if (firstMetadata) {
                    result.hasRecordedMethod = hasMethod;
                    result.recordedMethod = method;
                    result.hasRecordedThreshold = hasThreshold;
                    result.recordedThreshold = threshold;
                    firstMetadata = false;
                } else if (methodMismatch || thresholdMismatch) {
                    result.outputMetadataConsistent = false;
                }

                std::string amplitudeStatus;
                std::string amplitudeReason;
                int amplitudeDegraded = 0;
                image.amplitudeMetadataPresent =
                    NodeUtils::readStringFromH5(outputPath, "unwrap_amplitude_status", amplitudeStatus) &&
                    NodeUtils::readStringFromH5(outputPath, "unwrap_amplitude_reason", amplitudeReason) &&
                    NodeUtils::readScalarFromH5(outputPath, "unwrap_amplitude_degraded", amplitudeDegraded);
                if (image.amplitudeMetadataPresent) {
                    image.amplitudeStatus = QString::fromStdString(amplitudeStatus);
                    image.amplitudeReason = QString::fromStdString(amplitudeReason);
                    image.amplitudeDegraded = amplitudeDegraded != 0;
                    NodeUtils::readScalarFromH5(outputPath, "unwrap_amplitude_master_rows", image.amplitudeMasterRows);
                    NodeUtils::readScalarFromH5(outputPath, "unwrap_amplitude_master_cols", image.amplitudeMasterCols);
                    NodeUtils::readScalarFromH5(outputPath, "unwrap_amplitude_slave_rows", image.amplitudeSlaveRows);
                    NodeUtils::readScalarFromH5(outputPath, "unwrap_amplitude_slave_cols", image.amplitudeSlaveCols);
                    NodeUtils::readScalarFromH5(outputPath, "unwrap_amplitude_expected_rows", image.amplitudeExpectedRows);
                    NodeUtils::readScalarFromH5(outputPath, "unwrap_amplitude_expected_cols", image.amplitudeExpectedCols);
                }

                cv::Mat inputPhase;
                cv::Mat outputPhase;
                if (!NodeUtils::readMatFromH5(inputPath, "phase", inputPhase, CV_32F)
                    || !NodeUtils::readMatFromH5(outputPath, "phase", outputPhase, CV_32F)
                    || inputPhase.empty() || outputPhase.empty()) {
                    result.images.append(image);
                    continue;
                }

                image.inputRows = inputPhase.rows;
                image.inputCols = inputPhase.cols;
                image.outputRows = outputPhase.rows;
                image.outputCols = outputPhase.cols;
                image.dimensionsMatch = inputPhase.size() == outputPhase.size();

                double squaredResidualSum = 0.0;
                const qint64 pixelCount = static_cast<qint64>(inputPhase.total());
                constexpr qint64 maxResidualSamples = 200000;
                const qint64 sampleStride = std::max<qint64>(1, (pixelCount + maxResidualSamples - 1) / maxResidualSamples);
                std::vector<double> residualSamples;
                residualSamples.reserve(static_cast<size_t>(std::min<qint64>((pixelCount + sampleStride - 1) / sampleStride, maxResidualSamples)));
                for (int row = 0; row < inputPhase.rows; ++row) {
                    if (row % 128 == 0 && cancelToken && cancelToken->load()) {
                        result.images.clear();
                        result.success = false;
                        result.errorMessage = QObject::tr("诊断任务已取消。");
                        return result;
                    }
                    const float* inputValues = inputPhase.ptr<float>(row);
                    const float* outputValues = image.dimensionsMatch ? outputPhase.ptr<float>(row) : nullptr;
                    for (int col = 0; col < inputPhase.cols; ++col) {
                        const double inputValue = inputValues[col];
                        if (!std::isfinite(inputValue)) {
                            continue;
                        }
                        ++image.inputFinite;
                        if (!outputValues || !std::isfinite(outputValues[col])) {
                            continue;
                        }

                        const double residual = std::abs(std::atan2(std::sin(outputValues[col] - inputValue),
                                                                    std::cos(outputValues[col] - inputValue)));
                        squaredResidualSum += residual * residual;
                        if (image.pairedFinite % sampleStride == 0) {
                            residualSamples.push_back(residual);
                        }
                        ++image.pairedFinite;
                    }
                }

                if (image.pairedFinite > 0) {
                    image.hasRewrapMetrics = true;
                    image.rewrapRmse = std::sqrt(squaredResidualSum / image.pairedFinite);
                    if (!residualSamples.empty()) {
                        const size_t p95Index = static_cast<size_t>(std::ceil(residualSamples.size() * 0.95)) - 1;
                        std::nth_element(residualSamples.begin(), residualSamples.begin() + p95Index, residualSamples.end());
                        image.rewrapP95 = residualSamples[p95Index];
                    }
                }

                constexpr double highGradientThreshold = 3.14159265358979323846;
                cv::Mat validOutputMask = cv::Mat::zeros(outputPhase.size(), CV_8U);
                cv::Mat gradientRiskMask = cv::Mat::zeros(outputPhase.size(), CV_8U);
                for (int row = 0; row < outputPhase.rows; ++row) {
                    if (row % 128 == 0 && cancelToken && cancelToken->load()) {
                        result.images.clear();
                        result.success = false;
                        result.errorMessage = QObject::tr("诊断任务已取消。");
                        return result;
                    }
                    const float* outputValues = outputPhase.ptr<float>(row);
                    uchar* validMaskValues = validOutputMask.ptr<uchar>(row);
                    for (int col = 0; col < outputPhase.cols; ++col) {
                        if (std::isfinite(outputValues[col])) {
                            validMaskValues[col] = 255;
                            ++image.outputFinite;
                        }
                    }
                }

                if (image.outputFinite > 0) {
                    cv::Mat labels;
                    cv::Mat stats;
                    cv::Mat centroids;
                    const int labelCount = cv::connectedComponentsWithStats(
                        validOutputMask, labels, stats, centroids, 8, CV_32S);
                    image.validComponentCount = std::max(0, labelCount - 1);
                    for (int label = 1; label < labelCount; ++label) {
                        const qint64 area = stats.at<int>(label, cv::CC_STAT_AREA);
                        image.largestValidComponentPixels = std::max(image.largestValidComponentPixels, area);
                    }
                    image.largestValidComponentRatio = 100.0 * image.largestValidComponentPixels / image.outputFinite;
                }

                for (int row = 0; row < outputPhase.rows; ++row) {
                    if (row % 128 == 0 && cancelToken && cancelToken->load()) {
                        result.images.clear();
                        result.success = false;
                        result.errorMessage = QObject::tr("诊断任务已取消。");
                        return result;
                    }
                    const float* currentValues = outputPhase.ptr<float>(row);
                    const float* nextRowValues = row + 1 < outputPhase.rows ? outputPhase.ptr<float>(row + 1) : nullptr;
                    uchar* riskValues = gradientRiskMask.ptr<uchar>(row);
                    uchar* nextRiskValues = row + 1 < outputPhase.rows ? gradientRiskMask.ptr<uchar>(row + 1) : nullptr;
                    for (int col = 0; col < outputPhase.cols; ++col) {
                        if (!std::isfinite(currentValues[col])) {
                            continue;
                        }

                        if (col + 1 < outputPhase.cols && std::isfinite(currentValues[col + 1])) {
                            ++image.gradientComparedEdges;
                            if (std::abs(static_cast<double>(currentValues[col + 1]) - currentValues[col]) > highGradientThreshold) {
                                ++image.gradientRiskEdges;
                                riskValues[col] = 255;
                                riskValues[col + 1] = 255;
                            }
                        }

                        if (nextRowValues && std::isfinite(nextRowValues[col])) {
                            ++image.gradientComparedEdges;
                            if (std::abs(static_cast<double>(nextRowValues[col]) - currentValues[col]) > highGradientThreshold) {
                                ++image.gradientRiskEdges;
                                riskValues[col] = 255;
                                nextRiskValues[col] = 255;
                            }
                        }
                    }
                }
                image.candidateJumpPoints = static_cast<qint64>(cv::countNonZero(gradientRiskMask));

                if (image.gradientRiskEdges > 0) {
                    cv::Mat labels;
                    cv::Mat stats;
                    cv::Mat centroids;
                    const int labelCount = cv::connectedComponentsWithStats(
                        gradientRiskMask, labels, stats, centroids, 8, CV_32S);
                    image.gradientRiskComponentCount = std::max(0, labelCount - 1);
                    for (int label = 1; label < labelCount; ++label) {
                        const qint64 area = stats.at<int>(label, cv::CC_STAT_AREA);
                        if (area > image.largestGradientRiskPixels) {
                            image.largestGradientRiskPixels = area;
                            image.largestGradientRiskBounds = QRect(
                                stats.at<int>(label, cv::CC_STAT_LEFT),
                                stats.at<int>(label, cv::CC_STAT_TOP),
                                stats.at<int>(label, cv::CC_STAT_WIDTH),
                                stats.at<int>(label, cv::CC_STAT_HEIGHT));
                        }
                    }
                }
                image.hasSpatialMetrics = image.outputFinite > 0;
                result.images.append(image);
            }

            result.success = !result.images.isEmpty();
            if (!result.success) {
                result.errorMessage = QObject::tr("没有可用于诊断的解缠影像。");
            }
            return result;
        });

        auto* watcher = new QFutureWatcher<UnwrapValidationResults>(this);
        connect(watcher, &QFutureWatcher<UnwrapValidationResults>::finished, this, [this, watcher, currentEpoch]() {
            if (currentEpoch != m_validationEpoch || m_isTimedOut) {
                watcher->deleteLater();
                return;
            }

            const UnwrapValidationResults result = watcher->result();
            m_loadingOverlay->stopLoading();
            if (!result.success) {
                m_statusTitle->setText(QObject::tr("诊断失败"));
                m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #EF4444;");
                m_statusDesc->setText(result.errorMessage);
                m_compTable->clearComparison();
                m_compTable->setEnabled(false);
                setLabelsState(QObject::tr("诊断失败"));
                watcher->deleteLater();
                return;
            }


            m_compTable->clearComparison();
            m_compTable->setEnabled(true);
            const QString actualMethod = result.hasRecordedMethod ? unwrapMethodName(result.recordedMethod) : QObject::tr("未记录（旧结果）");
            m_compTable->addComparison(QObject::tr("解缠算法"), unwrapMethodName(result.expectedMethod), actualMethod);
            int outputCount = 0;
            for (const UnwrapImageDiagnostics& image : result.images) {
                outputCount += image.outputFound ? 1 : 0;
            }
            m_compTable->addComparison(QObject::tr("输入/输出影像数"), QString::number(result.images.size()), QString::number(outputCount));
            if (result.expectedMethod == 4) {
                const QString actualThreshold = result.hasRecordedThreshold
                    ? QString::number(result.recordedThreshold, 'f', 3)
                    : QObject::tr("未记录（旧结果）");
                m_compTable->addComparison(QObject::tr("相干系数阈值"), QString::number(result.expectedThreshold, 'f', 3), actualThreshold);
            }

            qint64 inputFinite = 0;
            qint64 pairedFinite = 0;
            double sumSquaredResidual = 0.0;
            qint64 gradientComparedEdges = 0;
            qint64 gradientRiskEdges = 0;
            qint64 outputFinite = 0;
            qint64 candidateJumpPoints = 0;
            int totalValidComponents = 0;
            int maxValidComponents = 0;
            double smallestLargestComponentRatio = 100.0;
const UnwrapImageDiagnostics* largestRiskImage = nullptr;
            int largestRiskImageIndex = -1;
            int missingOrInvalid = 0;
            int amplitudeDegradedCount = 0;
int amplitudeMetadataMissingCount = 0;
            const bool singleImage = (result.images.size() == 1);
            for (int imageIndex = 0; imageIndex < result.images.size(); ++imageIndex) {
                const UnwrapImageDiagnostics& image = result.images.at(imageIndex);
                const QString shortLabel = shortImageLabel(imageIndex, image.name);
                const QString expected = QStringLiteral("%1 x %2").arg(image.inputCols).arg(image.inputRows);
                QString actual;
                if (!image.outputFound) {
                    actual = QObject::tr("缺失输出");
                    ++missingOrInvalid;
                } else if (!image.dimensionsMatch) {
                    actual = QObject::tr("尺寸不匹配: %1 x %2").arg(image.outputCols).arg(image.outputRows);
                    ++missingOrInvalid;
                } else if (!image.hasRewrapMetrics) {
                    actual = QObject::tr("无有效配对像元");
                    ++missingOrInvalid;
                } else {
                    actual = QStringLiteral("%1 x %2").arg(image.outputCols).arg(image.outputRows);
                }
m_compTable->addComparison(shortLabel, expected, actual, true, image.name);

                if (!singleImage) {
                    if (image.amplitudeMetadataPresent) {
                        const UnwrapAmplitudeDisplay amplitudeDisplay = amplitudeDisplayInfo(image);
                        m_compTable->addDiagnostic(shortLabel + QObject::tr(" 幅度约束"),
                            amplitudeDisplay.value, image.name,
                            amplitudeDisplay.conclusion, amplitudeDisplay.warning);
                        if (image.amplitudeDegraded) {
                            ++amplitudeDegradedCount;
                        }
                    } else if (result.expectedMethod == 3) {
                        ++amplitudeMetadataMissingCount;
                        m_compTable->addDiagnostic(shortLabel + QObject::tr(" 幅度约束"),
                            QObject::tr("未记录（旧结果或元数据不完整）"), image.name,
                            QObject::tr("需复查"), true);
                    }
                } else if (image.amplitudeMetadataPresent) {
                    if (image.amplitudeDegraded) {
                        ++amplitudeDegradedCount;
                    }
                } else if (result.expectedMethod == 3) {
                    ++amplitudeMetadataMissingCount;
                }

                if (image.hasRewrapMetrics) {
                    const double imageCoverage = image.inputFinite > 0 ? 100.0 * image.pairedFinite / image.inputFinite : 0.0;
                    const double riskRatio = image.gradientComparedEdges > 0
                        ? 100.0 * image.gradientRiskEdges / image.gradientComparedEdges : 0.0;
                    const double pointRatio = image.outputFinite > 0
                        ? 100.0 * image.candidateJumpPoints / image.outputFinite : 0.0;
                    if (!singleImage) {
                        m_compTable->addDiagnostic(shortLabel + QObject::tr(" 诊断"),
                            QObject::tr("覆盖 %1%, RMSE %2 rad, P95 %3 rad, 连通域 %4, 最大占比 %5%, 候选跳变边 %6%, 候选跳变点 %7%")
                                .arg(QString::number(imageCoverage, 'f', 2),
                                     QString::number(image.rewrapRmse, 'g', 4),
                                     QString::number(image.rewrapP95, 'g', 4),
                                     QString::number(image.validComponentCount),
                                     QString::number(image.largestValidComponentRatio, 'f', 2),
                                     QString::number(riskRatio, 'f', 4),
                                     QString::number(pointRatio, 'f', 4)),
                            image.name);
                    }
                }

                inputFinite += image.inputFinite;
                pairedFinite += image.pairedFinite;
                if (image.hasRewrapMetrics) {
                    sumSquaredResidual += image.rewrapRmse * image.rewrapRmse * image.pairedFinite;
                }
                if (image.hasSpatialMetrics) {
                    totalValidComponents += image.validComponentCount;
                    maxValidComponents = std::max(maxValidComponents, image.validComponentCount);
                    smallestLargestComponentRatio = std::min(smallestLargestComponentRatio, image.largestValidComponentRatio);
                    gradientComparedEdges += image.gradientComparedEdges;
                    gradientRiskEdges += image.gradientRiskEdges;
                    outputFinite += image.outputFinite;
                    candidateJumpPoints += image.candidateJumpPoints;
                    if (image.largestGradientRiskPixels > 0
                        && (!largestRiskImage || image.largestGradientRiskPixels > largestRiskImage->largestGradientRiskPixels)) {
                        largestRiskImage = &image;
                        largestRiskImageIndex = imageIndex;
                    }
                }
            }

            const double coverage = inputFinite > 0 ? 100.0 * pairedFinite / inputFinite : 0.0;
            m_coverageLabel->setText(inputFinite > 0 ? QString::number(coverage, 'f', 2) + QStringLiteral("%") : QObject::tr("无有效输入像元"));
            m_rewrapRmseLabel->setText(pairedFinite > 0 ? QString::number(std::sqrt(sumSquaredResidual / pairedFinite), 'g', 5) : QObject::tr("无有效配对像元"));

            double worstP95 = 0.0;
            for (const UnwrapImageDiagnostics& image : result.images) {
                if (image.hasRewrapMetrics) {
                    worstP95 = std::max(worstP95, image.rewrapP95);
                }
            }
            m_rewrapP95Label->setText(pairedFinite > 0
                ? QString::number(worstP95, 'g', 5)
                    + (singleImage ? QString() : QObject::tr("（逐幅最大估计值）"))
                : QObject::tr("无有效配对像元"));
            m_componentLabel->setText(totalValidComponents > 0
                ? singleImage
                    ? QString::number(totalValidComponents)
                    : QObject::tr("%1（逐幅最大 %2）").arg(totalValidComponents).arg(maxValidComponents)
                : QObject::tr("无有效输出"));
            m_largestComponentLabel->setText(totalValidComponents > 0
                ? QString::number(smallestLargestComponentRatio, 'f', 2)
                    + (singleImage ? QStringLiteral("%") : QObject::tr("%（逐幅最小值）"))
                : QObject::tr("无有效输出"));
            m_candidateJumpEdgeLabel->setText(gradientComparedEdges > 0
                ? QObject::tr("%1 / %2（%3%）")
                    .arg(gradientRiskEdges)
                    .arg(gradientComparedEdges)
                    .arg(QString::number(100.0 * gradientRiskEdges / gradientComparedEdges, 'f', 4))
                : QObject::tr("无可比较边"));
            m_candidateJumpPointLabel->setText(outputFinite > 0
                ? QObject::tr("%1 / %2（%3%）")
                    .arg(candidateJumpPoints)
                    .arg(outputFinite)
                    .arg(QString::number(100.0 * candidateJumpPoints / outputFinite, 'f', 4))
                : QObject::tr("无有效输出像元"));
if (largestRiskImage) {
                const QRect& bounds = largestRiskImage->largestGradientRiskBounds;
                const QString imageName = shortImageLabel(largestRiskImageIndex, largestRiskImage->name);
                m_riskRegionLabel->setText(QObject::tr("%1\nx=%2, y=%3, %4 x %5 (%6 像元)")
                    .arg(imageName)
                    .arg(bounds.x()).arg(bounds.y()).arg(bounds.width()).arg(bounds.height())
                    .arg(largestRiskImage->largestGradientRiskPixels));
            } else {
                m_riskRegionLabel->setText(QObject::tr("未发现候选跳变区域"));
            }
            m_missingLabel->setText(QString::number(missingOrInvalid) + QStringLiteral(" / ") + QString::number(result.images.size()));
            if (singleImage) {
                const UnwrapImageDiagnostics& only = result.images.first();
                m_amplitudeLabel->setText(result.expectedMethod == 3
                    ? (only.amplitudeMetadataPresent ? amplitudeDisplayInfo(only).value
                                                     : QObject::tr("未记录（旧结果或元数据不完整）"))
                    : QObject::tr("不适用（非 SNAPHU）"));
            } else if (amplitudeDegradedCount > 0) {
                m_amplitudeLabel->setText(QObject::tr("%1 / %2 幅降级")
                    .arg(amplitudeDegradedCount).arg(result.images.size()));
            } else if (amplitudeMetadataMissingCount > 0) {
                m_amplitudeLabel->setText(QObject::tr("未记录（%1 幅）").arg(amplitudeMetadataMissingCount));
            } else if (result.expectedMethod == 3) {
                m_amplitudeLabel->setText(QObject::tr("全部使用或明确记录"));
            } else {
                m_amplitudeLabel->setText(QObject::tr("不适用（非 SNAPHU）"));
            }

            const bool metadataMatch = result.outputMetadataConsistent
                && (!result.hasRecordedMethod || result.recordedMethod == result.expectedMethod)
                && (result.expectedMethod != 4 || !result.hasRecordedThreshold || std::abs(result.recordedThreshold - result.expectedThreshold) <= 1e-9);
            const bool amplitudeReview = amplitudeDegradedCount > 0 || amplitudeMetadataMissingCount > 0;
            if (missingOrInvalid == 0 && metadataMatch && !amplitudeReview) {
                m_statusTitle->setText(QObject::tr("诊断完成"));
                m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #10B981;");
                m_statusDesc->setText(QObject::tr("全部结果已配对。重新缠绕一致性、连通域和候选跳变仅用于诊断数值完整性与候选风险，不能单独证明不存在整数周模糊。"));
            } else {
                m_statusTitle->setText(QObject::tr("需要复查"));
                m_statusTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #F59E0B;");
                m_statusDesc->setText(amplitudeReview
                    ? QObject::tr("发现 SNAPHU 幅度约束降级或元数据缺失；结果仍可用，但质量不等同于使用幅度约束的运行。")
                    : QObject::tr("发现缺失、尺寸异常或记录参数不一致的结果。请先检查对应影像和处理日志。"));
            }
            watcher->deleteLater();
        });
        watcher->setFuture(future);
    }

    UnwrapNode* m_node = nullptr;
    QLabel* m_coverageLabel = nullptr;
    QLabel* m_rewrapRmseLabel = nullptr;
    QLabel* m_rewrapP95Label = nullptr;
    QLabel* m_componentLabel = nullptr;
    QLabel* m_largestComponentLabel = nullptr;
    QLabel* m_candidateJumpEdgeLabel = nullptr;
    QLabel* m_candidateJumpPointLabel = nullptr;
    QLabel* m_riskRegionLabel = nullptr;
    QLabel* m_amplitudeLabel = nullptr;
    QLabel* m_missingLabel = nullptr;
};

} // namespace

::QWidget* UnwrapNode::createValidationWidget(::QWidget* parent)
{
    return new UnwrapValidationWidget(this, parent);
}

} // namespace QtNodes

