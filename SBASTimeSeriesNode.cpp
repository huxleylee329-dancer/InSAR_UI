#include "SBASTimeSeriesNode.h"
#include "NodeUtils.h"
#include "IApplicationInterface.h"
#include "InterfaceManager.h"
#include "MainWindow.h"
#include "WorkspaceUI.h"
#include "InSARLogManager.h"
#include "icon_source.h"
#include "tinyxml.h"
#include "Utils.h"
#include <QTimer>
#include <QJsonDocument>
#include <QMessageBox>
#include <QFileInfo>
#include <QDebug>
#include <QDir>
#include <QApplication>
#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>

namespace QtNodes {

namespace {
const char* const kSbasProvenanceDataset = "sbas_rebuild_provenance";

QJsonObject fingerprintFile(const QString& path)
{
    const QJsonArray fingerprints = NodeUtils::fingerprintInputPaths(QStringList() << path);
    return fingerprints.isEmpty() ? QJsonObject() : fingerprints.first().toObject();
}
}

SBASTimeSeriesNode::SBASTimeSeriesNode()
    : ExecutableNodeDelegateModel()
    , _widget(nullptr)
    , m_inputNodeLabel(nullptr)
    , m_spatialThreshEdit(nullptr)
    , m_temporalThreshEdit(nullptr)
    , m_temporalThreshLowEdit(nullptr)
    , m_multilookRgEdit(nullptr)
    , m_multilookAzEdit(nullptr)
    , m_unwrapMethodCombo(nullptr)
    , m_alphaEdit(nullptr)
    , m_coherenceThreshEdit(nullptr)
    , m_temporalCoherenceThreshEdit(nullptr)
    , m_refinementCohThreshEdit(nullptr)
    , m_refinementDefThreshEdit(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_resultLabel(nullptr)
    , m_temporalThreshLow(0.0)
    , m_temporalThresh(200.0)
    , m_spatialThresh(500.0)
    , m_multilookRg(4)
    , m_multilookAz(4)
    , m_unwrapMethod(1)
    , m_alpha(0.8)
    , m_coherenceThresh(0.5)
    , m_temporalCoherenceThresh(0.6)
    , m_refinementCohThresh(0.8)
    , m_refinementDefThresh(0.01)
    , m_outputNodeName(QStringLiteral("SBAS_Series"))
    , m_worker(nullptr)
    , m_thread(nullptr)
{
    qRegisterMetaType<SBASTimeSeriesResult>("SBASTimeSeriesResult");
    setExecutionMode(ExecutionMode::Automatic);
}

SBASTimeSeriesNode::~SBASTimeSeriesNode()
{
    ++m_executionGeneration;
    stopAndWaitForTrackedExecutions();
    rollbackOutputTransaction(QStringLiteral("node destroyed"));
}

ProductInputContract SBASTimeSeriesNode::productInputContract(PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    ProductInputContract contract;
    contract.semanticId = QStringLiteral("sbas_time_series.input.coregistered_complex_sar");
    contract.allowedProductTypes = QStringList() << QStringLiteral("coregistered_complex_sar");
    contract.requiredProvenanceFields = QStringList()
        << QStringLiteral("producer") << QStringLiteral("output_port");
    return contract;
}

ProductOutputContract SBASTimeSeriesNode::productOutputContract(PortIndex portIndex) const
{
    ProductOutputContract contract;
    contract.semanticId = portIndex == 0
        ? QStringLiteral("sbas_time_series.output.sbas_time_series")
        : QStringLiteral("sbas_time_series.output.preview");
    contract.publishedProductTypes = portIndex == 0
        ? QStringList() << QStringLiteral("sbas_time_series")
        : QStringList() << QStringLiteral("preview");
    return contract;
}

unsigned int SBASTimeSeriesNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 1;
    else
        return 2;
}

NodeDataType SBASTimeSeriesNode::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) {
        return NodeDataType{"imported_file", "Imported File"};
    } else {
        if (portIndex == 0)
            return NodeDataType{"imported_file", "Imported File"}; // H5 results
        else
            return NodeDataType{"image_info", "Image Info"}; // JPG preview
    }
}

bool SBASTimeSeriesNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return true;
}

QString SBASTimeSeriesNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In)
        return QStringLiteral("输入图像");
    else {
        if (portIndex == 0)
            return tr("成果 *");
        else
            return tr("预览 ?");
    }
}

bool SBASTimeSeriesNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out && portIndex == 1)
        return true;
    return false;
}

std::shared_ptr<NodeData> SBASTimeSeriesNode::outData(PortIndex port)
{
    if (executionState() != ExecutionState::Completed)
        return nullptr;
    if (port == 0)
        return m_outputData;
    else
        return m_previewData;
}

void SBASTimeSeriesNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    Q_UNUSED(port);
    m_inputData = std::dynamic_pointer_cast<ImportedFileData>(data);

    QStringList filePaths = m_inputData ? m_inputData->filePaths() : QStringList();
    if (filePaths.isEmpty()) {
        m_outputData.reset();
        m_previewData.reset();
        setOutputData(0, nullptr);
        setOutputData(1, nullptr);
    }

    updateLabels();
    ExecutableNodeDelegateModel::setInData(data, port);
}

::QWidget* SBASTimeSeriesNode::embeddedWidget()
{
    if (!_widget)
    {
        createWidget();
    }
    return _widget;
}

void SBASTimeSeriesNode::setExecutionMode(ExecutionMode mode)
{
    ExecutableNodeDelegateModel::setExecutionMode(mode);
}

void SBASTimeSeriesNode::createWidget()
{
    _widget = new QWidget();
    _widget->setObjectName("NodeEmbeddedWidget");
    _widget->setFixedWidth(300); // SOP: Lock width

    QVBoxLayout* mainLayout = new QVBoxLayout(_widget);
    mainLayout->setContentsMargins(5, 5, 5, 5);
    mainLayout->setSpacing(4);

    // Row 1: Input node display
    QHBoxLayout* inputLayout = new QHBoxLayout();
    QLabel* inputTitleLabel = new QLabel(QStringLiteral("输入节点:"));
    inputTitleLabel->setFixedWidth(80); // SOP: Fixed label width
    m_inputNodeLabel = new QLabel(QStringLiteral("等待输入"));
    m_inputNodeLabel->setStyleSheet("color: gray;");
    inputLayout->addWidget(inputTitleLabel);
    inputLayout->addWidget(m_inputNodeLabel);
    mainLayout->addLayout(inputLayout);

    // Parameters Grouping
    auto addParamRow = [&](const QString& labelText, QWidget* editWidget) {
        QHBoxLayout* layout = new QHBoxLayout();
        QLabel* label = new QLabel(labelText);
        label->setFixedWidth(80); // SOP: Fixed label width
        layout->addWidget(label);
        layout->addWidget(editWidget);
        mainLayout->addLayout(layout);
    };

    m_spatialThreshEdit = new QLineEdit(QString::number(m_spatialThresh));
    addParamRow(QStringLiteral("空间基线阈值:"), m_spatialThreshEdit);

    m_temporalThreshEdit = new QLineEdit(QString::number(m_temporalThresh));
    addParamRow(QStringLiteral("时间基线上限:"), m_temporalThreshEdit);

    m_temporalThreshLowEdit = new QLineEdit(QString::number(m_temporalThreshLow));
    addParamRow(QStringLiteral("时间基线下限:"), m_temporalThreshLowEdit);

    m_multilookRgEdit = new QLineEdit(QString::number(m_multilookRg));
    addParamRow(QStringLiteral("距离向视数:"), m_multilookRgEdit);

    m_multilookAzEdit = new QLineEdit(QString::number(m_multilookAz));
    addParamRow(QStringLiteral("方位向视数:"), m_multilookAzEdit);

    m_unwrapMethodCombo = new QComboBox();
    m_unwrapMethodCombo->addItem(QStringLiteral("三角网 MCF"), 1);
    m_unwrapMethodCombo->addItem(QStringLiteral("规则网 SNAPHU"), 2);
    m_unwrapMethodCombo->addItem(QStringLiteral("规则网 MCF"), 3);
    m_unwrapMethodCombo->setCurrentIndex(m_unwrapMethod - 1);
    addParamRow(QStringLiteral("相位解缠方法:"), m_unwrapMethodCombo);

    m_alphaEdit = new QLineEdit(QString::number(m_alpha));
    addParamRow(QStringLiteral("解缠权重α:"), m_alphaEdit);

    m_coherenceThreshEdit = new QLineEdit(QString::number(m_coherenceThresh));
    addParamRow(QStringLiteral("相干性阈值:"), m_coherenceThreshEdit);

    m_temporalCoherenceThreshEdit = new QLineEdit(QString::number(m_temporalCoherenceThresh));
    addParamRow(QStringLiteral("时序相干阈值:"), m_temporalCoherenceThreshEdit);

    m_refinementCohThreshEdit = new QLineEdit(QString::number(m_refinementCohThresh));
    addParamRow(QStringLiteral("精炼相干阈值:"), m_refinementCohThreshEdit);

    m_refinementDefThreshEdit = new QLineEdit(QString::number(m_refinementDefThresh));
    addParamRow(QStringLiteral("精炼形变阈值:"), m_refinementDefThreshEdit);

    m_outputNodeNameEdit = new QLineEdit(m_outputNodeName);
    m_outputNodeNameEdit->setPlaceholderText(QStringLiteral("自动生成或手动输入")); // SOP: standard placeholder
    addParamRow(QStringLiteral("目标节点名:"), m_outputNodeNameEdit);

    // Row: Result Display
    m_resultLabel = new QLabel(QStringLiteral("状态：等待计算"));
    m_resultLabel->setStyleSheet("QLabel { background-color: rgba(128, 128, 128, 20); border: 1px solid rgba(128, 128, 128, 80); border-radius: 4px; padding: 6px; font-size: 11px; line-height: 14px; }");
    m_resultLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_resultLabel->setWordWrap(true);
    mainLayout->addWidget(m_resultLabel);

    // Connect parameters changes
    connect(m_spatialThreshEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_spatialThresh = text.toDouble(); invalidateExecution();
    });
    connect(m_temporalThreshEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_temporalThresh = text.toDouble(); invalidateExecution();
    });
    connect(m_temporalThreshLowEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_temporalThreshLow = text.toDouble(); invalidateExecution();
    });
    connect(m_multilookRgEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_multilookRg = text.toInt(); invalidateExecution();
    });
    connect(m_multilookAzEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_multilookAz = text.toInt(); invalidateExecution();
    });
    connect(m_unwrapMethodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        m_unwrapMethod = m_unwrapMethodCombo->itemData(idx).toInt(); invalidateExecution();
    });
    connect(m_alphaEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_alpha = text.toDouble(); invalidateExecution();
    });
    connect(m_coherenceThreshEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_coherenceThresh = text.toDouble(); invalidateExecution();
    });
    connect(m_temporalCoherenceThreshEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_temporalCoherenceThresh = text.toDouble(); invalidateExecution();
    });
    connect(m_refinementCohThreshEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_refinementCohThresh = text.toDouble(); invalidateExecution();
    });
    connect(m_refinementDefThreshEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_refinementDefThresh = text.toDouble(); invalidateExecution();
    });
    connect(m_outputNodeNameEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_outputNodeName = text; invalidateExecution();
    });

    updateLabels();
}

void SBASTimeSeriesNode::updateLabels()
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
        QStringList h5Paths;
        if (NodeUtils::loadCommittedOutputManifest(projectPath(), m_outputNodeName, h5Paths) &&
            h5Paths.size() == 1) {
            m_resultLabel->setText(QStringLiteral("状态：时序分析计算完成\n输出：%1")
                .arg(QFileInfo(h5Paths.first()).fileName()));
        } else {
            m_resultLabel->setText(QStringLiteral("状态：等待计算"));
        }
    }
    updateWidgetSize();
}

void SBASTimeSeriesNode::updateWidgetSize()
{
    if (_widget) {
        _widget->setFixedWidth(300);
        _widget->adjustSize();
        Q_EMIT embeddedWidgetSizeUpdated();
    }
}

bool SBASTimeSeriesNode::validateInputs() const
{
    if (!m_inputData || m_inputData->filePaths().isEmpty()) return false;
    if (m_outputNodeName.isEmpty()) return false;
    return true;
}

bool SBASTimeSeriesNode::prepareToStart()
{
    if (!validateInputs()) {
        InSARLogManager::LogWarning("SBASTimeSeriesNode", "prepareToStart skipped: validateInputs failed.");
        return false;
    }

    const ProductValidationResult inputValidation = validateBoundDescriptor(
        productInputContract(0), m_inputData->productDescriptor());
    if (!inputValidation.accepted) {
        setLastErrorMessage(inputValidation.reason);
        setState(ExecutionState::Error);
        return false;
    }
    QString identityError;
    if (!NodeUtils::validateH5Identities(m_inputData->filePaths(),
                                         m_inputData->physicalProductDescriptor(), &identityError)) {
        setLastErrorMessage(identityError);
        setState(ExecutionState::Error);
        return false;
    }

    m_preparedProjectRoot = projectPath();
    m_preparedProjectName = projectName();
    m_preparedInputPaths = m_inputData->filePaths();
    QString h5Path = QDir(m_preparedProjectRoot).absoluteFilePath(
        m_outputNodeName + "/SBAS_time_series.h5");
    m_preparedOutputPaths = QStringList() << h5Path;
    QStringList pathsToCheck = QStringList() << h5Path;

    if (_isAutoTriggered) {
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
    } else {
        m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(
            NodeUtils::getProjectContext(_widget),
            m_outputNodeName,
            pathsToCheck,
            nullptr
        );
    }

    return m_preparedOverwriteResult != NodeUtils::OverwriteResult::Cancel;
}

void SBASTimeSeriesNode::execute()
{
    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting) {
        if (!validateAndRestoreOutput()) {
            onError(QStringLiteral("Existing SBAS time-series output does not satisfy its semantic identity contract."));
        }
        return;
    }

    executeProcessing();
}

void SBASTimeSeriesNode::executeProcessing()
{
    InSARLogManager::LogInfo("SBASTimeSeriesNode", "executeProcessing started.");
    const quint64 executionGeneration = ++m_executionGeneration;
    stopExecution();
    m_xmlDirty = false;
    m_pendingResult = SBASTimeSeriesResult();
    m_provenanceWritten = false;

    QString transactionError;
    if (!NodeUtils::beginOutputTransaction(m_preparedProjectRoot, m_outputNodeName,
                                           m_preparedOutputPaths, m_preparedInputPaths,
                                           m_outputTransaction, &transactionError, nullptr,
                                           NodeUtils::getProjectFilePath(_widget))) {
        onError(transactionError);
        return;
    }
    m_outputTransaction.executionRevision = executionRevision();
    QMap<QString, QString> provenance;
    provenance.insert(QStringLiteral("producer"), name());
    provenance.insert(QStringLiteral("output_port"), productOutputContract(0).semanticId);
    if (!NodeUtils::setOutputTransactionProductDescriptor(m_outputTransaction,
            ProductDescriptor::create(QStringLiteral("sbas_time_series"),
                productOutputContract(0).schemaId, productOutputContract(0).schemaVersion,
                productOutputContract(0).publishedState, name(), provenance), &transactionError)) {
        onError(transactionError);
        return;
    }

    m_thread = new QThread(this);
    m_worker = new SBASTimeSeriesWorker();
    m_worker->moveToThread(m_thread);
    SBASTimeSeriesWorker* const worker = m_worker;
    QThread* const workerThread = m_thread;
    const QPointer<QThread> workerThreadGuard(workerThread);
    trackExecution(worker, workerThread);

    const QString projPath = m_preparedProjectRoot;
    const QString projName = m_preparedProjectName;
    const QString stagingNode = m_outputTransaction.stagingName;
    const QStringList filePaths = m_preparedInputPaths;
    const double temporalThreshLow = m_temporalThreshLow;
    const double temporalThresh = m_temporalThresh;
    const double spatialThresh = m_spatialThresh;
    const int multilookRg = m_multilookRg;
    const int multilookAz = m_multilookAz;
    const int unwrapMethod = m_unwrapMethod;
    const double alpha = m_alpha;
    const double coherenceThresh = m_coherenceThresh;
    const double temporalCoherenceThresh = m_temporalCoherenceThresh;
    const double refinementCohThresh = m_refinementCohThresh;
    const double refinementDefThresh = m_refinementDefThresh;

    connect(workerThread, &QThread::started, worker,
            [worker, temporalThreshLow, temporalThresh, spatialThresh, multilookRg, multilookAz,
             unwrapMethod, alpha, coherenceThresh, temporalCoherenceThresh, refinementCohThresh,
             refinementDefThresh, projPath, projName, stagingNode, filePaths]() {
        worker->SBAS_time_series(
            temporalThreshLow,
            temporalThresh,
            spatialThresh,
            multilookRg,
            multilookAz,
            unwrapMethod,
            alpha,
            coherenceThresh,
            temporalCoherenceThresh,
            refinementCohThresh,
            refinementDefThresh,
            projPath,
            projName,
            stagingNode,
            QString(),
            filePaths,
            true
        );
    });

    connect(worker, &SBASTimeSeriesWorker::sbasGenerated, this,
            [this, executionGeneration](const SBASTimeSeriesResult& result) {
        if (executionGeneration == m_executionGeneration) onSbasGenerated(result);
    });
    connect(worker, &SBASTimeSeriesWorker::updateProcess, this,
            [this, executionGeneration](int progress, const QString& message) {
        if (executionGeneration == m_executionGeneration) onProgressUpdate(progress, message);
    });
    connect(worker, &SBASTimeSeriesWorker::endProcess, this,
            [this, executionGeneration]() {
        if (executionGeneration == m_executionGeneration) onProcessingFinished();
    });
    connect(worker, &SBASTimeSeriesWorker::endProcess, workerThread, &QThread::quit);
    connect(worker, &SBASTimeSeriesWorker::errorProcess, this,
            [this, executionGeneration](const QString& error) {
        if (executionGeneration == m_executionGeneration) onError(error);
    });
    connect(worker, &SBASTimeSeriesWorker::errorProcess, workerThread, &QThread::quit);
    connect(worker, &SBASTimeSeriesWorker::cancelled, this,
            [this, executionGeneration]() {
        if (executionGeneration == m_executionGeneration) onCancelled();
    });
    connect(worker, &SBASTimeSeriesWorker::cancelled, workerThread, &QThread::quit);

    connect(workerThread, &QThread::finished, worker, &QObject::deleteLater);
    connect(workerThread, &QThread::finished, workerThread, &QObject::deleteLater);

    setState(ExecutionState::Running);
    setProgress(0);

    QTimer::singleShot(0, this, [this, executionGeneration, workerThreadGuard]() {
        if (executionGeneration == m_executionGeneration && workerThreadGuard &&
            workerThreadGuard->isRunning()) {
            setState(ExecutionState::Running);
        }
    });

    deferAutomaticCompletion();
    m_thread->start();
}

void SBASTimeSeriesNode::stopExecution()
{
    if (m_worker) {
        m_worker->StopProcess();
    }
    if (m_thread) {
        m_thread->requestInterruption();
    }
}

void SBASTimeSeriesNode::processAutomatically()
{
    if (prepareToStart()) {
        executeProcessing();
    } else {
        setState(ExecutionState::Idle);
    }
}

void SBASTimeSeriesNode::onProgressUpdate(int progress, const QString& message)
{
    Q_UNUSED(message);
    if (isAutomaticExecutionObsolete()) {
        return;
    }
    setProgress(progress);
}

void SBASTimeSeriesNode::onError(const QString& error)
{
    m_worker = nullptr;
    m_thread = nullptr;
    rollbackOutputTransaction(error);
    m_outputData.reset();
    m_previewData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);
    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    InSARLogManager::LogError("SBASTimeSeriesNode", "Error during SBAS analysis: " + error);
    setState(ExecutionState::Error);
    finishExecution();
}

void SBASTimeSeriesNode::onCancelled()
{
    InSARLogManager::LogInfo("SBASTimeSeriesNode", "SBAS time-series cancellation cleanup completed.");
    m_worker = nullptr;
    m_thread = nullptr;
    rollbackOutputTransaction(QStringLiteral("cancelled"));
    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    m_outputData.reset();
    m_previewData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);
    if (m_resultLabel) m_resultLabel->setText(QStringLiteral("已取消"));
    setState(ExecutionState::Stopped);
    Q_EMIT executionStopped();
    Q_EMIT computingFinished();
}

void SBASTimeSeriesNode::onProcessingFinished()
{
    m_worker = nullptr;
    m_thread = nullptr;
    if (discardObsoleteAutomaticExecution()) {
        rollbackOutputTransaction(QStringLiteral("obsolete automatic execution"));
        m_outputData.reset();
        m_previewData.reset();
        setOutputData(0, nullptr);
        setOutputData(1, nullptr);
        return;
    }

    QString transactionError;
    if (!commitOutputTransaction(&transactionError)) {
        onError(transactionError);
        return;
    }

    InSARLogManager::LogInfo("SBASTimeSeriesNode", "executeProcessing completed.");
    const QString h5Path = QDir(m_preparedProjectRoot).absoluteFilePath(
        m_outputNodeName + "/SBAS_time_series.h5");
    m_outputData = std::make_shared<ImportedFileData>(QStringList() << h5Path, m_outputNodeName);
    m_outputData->setProductDescriptor(ProductDescriptor::fromJson(m_outputTransaction.productDescriptor));
    setOutputData(0, m_outputData);

    // Refresh project tree (SOP Rule 14 helper)
    IApplicationInterface* iface = NodeUtils::getProjectContext(_widget);
    if (iface) {
        iface->refreshProjectTree();
    }

    setProgress(100);
    setState(ExecutionState::Running);

    // Asynchronously generate preview JPG (SOP rule 7)
    if (!generateStaticPreviewJpg(true)) {
        finishExecution();
    }

    updateLabels();
}

void SBASTimeSeriesNode::onSbasGenerated(const SBASTimeSeriesResult& result)
{
    if (isAutomaticExecutionObsolete()) {
        return;
    }

    const QDir projectRoot(m_preparedProjectRoot);
    QJsonArray inputs;
    for (const QString& inputPath : m_preparedInputPaths) {
        const QString relativePath = projectRoot.relativeFilePath(QFileInfo(inputPath).absoluteFilePath());
        if (relativePath.isEmpty() || relativePath == QStringLiteral("..") || relativePath.startsWith(QStringLiteral("../"))) {
            onError(QStringLiteral("SBAS provenance requires project-relative input paths: %1").arg(inputPath));
            return;
        }
        QJsonObject input = fingerprintFile(QFileInfo(inputPath).absoluteFilePath());
        input.insert(QStringLiteral("path"), QDir::cleanPath(relativePath));
        inputs.append(input);
    }

    QJsonObject parameters;
    parameters.insert(QStringLiteral("temporalThreshLow"), m_temporalThreshLow);
    parameters.insert(QStringLiteral("temporalThresh"), m_temporalThresh);
    parameters.insert(QStringLiteral("spatialThresh"), m_spatialThresh);
    parameters.insert(QStringLiteral("multilookRg"), m_multilookRg);
    parameters.insert(QStringLiteral("multilookAz"), m_multilookAz);
    parameters.insert(QStringLiteral("unwrapMethod"), m_unwrapMethod);
    parameters.insert(QStringLiteral("alpha"), m_alpha);
    parameters.insert(QStringLiteral("coherenceThresh"), m_coherenceThresh);
    parameters.insert(QStringLiteral("temporalCoherenceThresh"), m_temporalCoherenceThresh);
    parameters.insert(QStringLiteral("refinementCohThresh"), m_refinementCohThresh);
    parameters.insert(QStringLiteral("refinementDefThresh"), m_refinementDefThresh);

    QJsonObject provenance;
    provenance.insert(QStringLiteral("version"), 2);
    provenance.insert(QStringLiteral("transactionRunId"), m_outputTransaction.runId);
    provenance.insert(QStringLiteral("inputs"), inputs);
    provenance.insert(QStringLiteral("parameters"), parameters);

    QString provenanceError;
    if (!NodeUtils::writeStringToH5(result.timesSeriesH5Path,
                                    QString::fromLatin1(kSbasProvenanceDataset),
                                    QJsonDocument(provenance).toJson(QJsonDocument::Compact).toStdString(),
                                    &provenanceError)) {
        onError(QStringLiteral("Failed to persist SBAS rebuild provenance: %1").arg(provenanceError));
        return;
    }
    m_pendingResult = result;
    m_provenanceWritten = true;
}

bool SBASTimeSeriesNode::commitOutputTransaction(QString* errorMessage)
{
    if (!m_provenanceWritten || m_pendingResult.dstNode != m_outputTransaction.stagingName ||
        m_pendingResult.timesSeriesH5Path.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("SBAS worker did not return a provenance-complete staging output.");
        return false;
    }

    OutputCommitLease commitLease = acquireOutputCommitLease(m_outputTransaction.executionRevision);
    if (!commitLease) {
        if (errorMessage) *errorMessage = QStringLiteral("SBAS execution revision is obsolete.");
        return false;
    }

    IApplicationInterface* iface = NodeUtils::getProjectContext(_widget);
    XMLFile* xml = iface ? iface->projectXml() : nullptr;
    const QString xmlPath = NodeUtils::getProjectFilePath(_widget);
    if (!xml || xmlPath.isEmpty() ||
        !NodeUtils::validateStagedOutputTransaction(m_outputTransaction, errorMessage) ||
        !NodeUtils::validateStagedH5Datasets(m_outputTransaction,
            QStringList() << QStringLiteral("mask") << QStringLiteral("defomation_velocity")
                          << QStringLiteral("deformation_time_series")
                          << QString::fromLatin1(kSbasProvenanceDataset), errorMessage) ||
        !NodeUtils::workerOutputsMatchManifest(m_preparedOutputPaths,
            QStringList() << m_pendingResult.timesSeriesH5Path, errorMessage)) {
        return false;
    }

    QStringList finalPaths;
    if (!NodeUtils::promoteOutputTransaction(m_outputTransaction, finalPaths, errorMessage) ||
        !NodeUtils::prepareOutputTransactionMetadataCommit(m_outputTransaction, xml, xmlPath, errorMessage)) {
        return false;
    }

    TiXmlElement* root = nullptr;
    if (xml->get_root(root) < 0 || !root) {
        if (errorMessage) *errorMessage = QStringLiteral("Unable to read project XML root for SBAS output.");
        return false;
    }
    for (TiXmlElement* element = root->FirstChildElement(); element != nullptr; ) {
        const char* name = element->Attribute("name");
        if (name && m_outputNodeName == QString::fromUtf8(name)) {
            TiXmlElement* toRemove = element;
            element = element->NextSiblingElement();
            root->RemoveChild(toRemove);
        } else {
            element = element->NextSiblingElement();
        }
    }
    const QString relativePath = QStringLiteral("/%1/SBAS_time_series.h5").arg(m_outputNodeName);
    if (xml->XMLFile_add_SBAS(m_outputNodeName.toStdString().c_str(), "SBAS_time_series",
                              relativePath.toStdString().c_str()) < 0 ||
        !NodeUtils::saveProjectXmlAtomically(xml, xmlPath, errorMessage) ||
        !NodeUtils::markOutputTransactionMetadataCommitted(m_outputTransaction, errorMessage)) {
        if (errorMessage && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("Unable to commit SBAS output metadata.");
        }
        return false;
    }

    if (iface && iface->projectModel() && !finalPaths.isEmpty()) {
        NodeUtils::removeDataNodeFromProjectTree(iface, m_outputNodeName);
        const QList<QStandardItem*> projects = iface->projectModel()->findItems(m_preparedProjectName);
        if (!projects.isEmpty()) {
            QStandardItem* outputNode = NodeUtils::findOrCreateProjectNode(
                projects.first(), m_outputNodeName, "SBAS-1.0", FOLDER_ICON);
            if (outputNode) {
                outputNode->setToolTip(m_preparedProjectName);
                NodeUtils::findOrCreateChildItem(outputNode, "SBAS_time_series", "SBAS",
                                                  finalPaths.first(), IMAGEDATA_ICON);
            }
        }
        iface->refreshProjectTree();
    }
    return true;
}

void SBASTimeSeriesNode::rollbackOutputTransaction(const QString& reason)
{
    IApplicationInterface* iface = NodeUtils::getProjectContext(_widget);
    NodeUtils::abandonOutputTransaction(m_outputTransaction, reason, iface ? iface->projectXml() : nullptr);
}

void SBASTimeSeriesNode::trackExecution(SBASTimeSeriesWorker* worker, QThread* thread)
{
    if (!worker || !thread) return;

    m_trackedExecutions.append({worker, thread});
    connect(thread, &QThread::finished, this, [this, thread]() {
        for (int i = m_trackedExecutions.size() - 1; i >= 0; --i) {
            const QPointer<QThread>& trackedThread = m_trackedExecutions.at(i).thread;
            if (!trackedThread || trackedThread.data() == thread) {
                m_trackedExecutions.removeAt(i);
            }
        }
    });
}

void SBASTimeSeriesNode::stopAndWaitForTrackedExecutions()
{
    for (const TrackedExecution& execution : m_trackedExecutions) {
        if (execution.worker) execution.worker->StopProcess();
        if (execution.thread) {
            execution.thread->requestInterruption();
            execution.thread->quit();
        }
    }
    for (const TrackedExecution& execution : m_trackedExecutions) {
        if (execution.thread && execution.thread->isRunning()) {
            execution.thread->wait();
        }
    }
    m_trackedExecutions.clear();
    m_worker = nullptr;
    m_thread = nullptr;
}

bool SBASTimeSeriesNode::validateAndRestoreOutput()
{
    QStringList h5Paths;
    ProductDescriptor::Ptr descriptor;
    QString identityError;
    if (NodeUtils::loadCommittedOutputManifest(projectPath(), m_outputNodeName, h5Paths) &&
        h5Paths.size() == 1 &&
        NodeUtils::loadCommittedOutputProductDescriptor(projectPath(), m_outputNodeName, descriptor, &identityError) &&
        validatePublishedDescriptor(productOutputContract(0), descriptor).accepted &&
        NodeUtils::validateH5Identities(h5Paths, descriptor, &identityError)) {
        const QString h5Path = h5Paths.first();
        m_outputData = std::make_shared<ImportedFileData>(QStringList() << h5Path, m_outputNodeName);
        m_outputData->setProductDescriptor(descriptor);
        setOutputData(0, m_outputData);
        
        // Asynchronously restore/generate JPG preview if missing (SOP rule 7)
        generateStaticPreviewJpg();

        setState(ExecutionState::Completed);
        setProgress(100);
        updateLabels();
        Q_EMIT dataUpdated(0);
        return true;
    }
    return false;
}

bool SBASTimeSeriesNode::generateStaticPreviewJpg(bool completeExecution)
{
    QString outDir = projectPath() + "/" + m_outputNodeName;
    QString h5Path = outDir + "/SBAS_time_series.h5";
    QString jpgPath = outDir + "/SBAS_time_series.jpg";

    if (!QFileInfo::exists(h5Path)) return false;

    // Use QFutureWatcher to do background JPG generation without blocking UI thread (SOP Pitfall 7)
    QFutureWatcher<void>* watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher, jpgPath, completeExecution]() {
        if (discardObsoleteAutomaticExecution()) {
            watcher->deleteLater();
            return;
        }

        if (QFileInfo::exists(jpgPath)) {
            m_previewData = std::make_shared<ImageInfoData>(jpgPath);
            QMap<QString, QString> previewProvenance;
            previewProvenance.insert(QStringLiteral("producer"), name());
            previewProvenance.insert(QStringLiteral("output_port"), productOutputContract(1).semanticId);
            m_previewData->setProductDescriptor(ProductDescriptor::create(QStringLiteral("preview"),
                productOutputContract(1).schemaId, productOutputContract(1).schemaVersion,
                productOutputContract(1).publishedState, name(), previewProvenance));
            setOutputData(1, m_previewData);
            if (!completeExecution) {
                Q_EMIT dataUpdated(1);
            }
        } else if (completeExecution) {
            m_previewData.reset();
            setOutputData(1, nullptr);
        }
        watcher->deleteLater();
        if (completeExecution) {
            finishExecution();
        }
    });

    watcher->setFuture(QtConcurrent::run([h5Path, jpgPath]() {
        NodeUtils::Hdf5Locker locker;
        FormatConversion FC;
        Utils util;
        Mat defomation_velocity, mask;
        int ret = (NodeUtils::readMatFromH5(h5Path, "defomation_velocity", defomation_velocity, CV_64F) &&
                   NodeUtils::readMatFromH5(h5Path, "mask", mask)) ? 0 : -1;
        if (ret == 0 && !defomation_velocity.empty()) {
            util.savephase_white(jpgPath.toStdString().c_str(), "jet", defomation_velocity, mask);
        }
    }));
    return true;
}

QJsonObject SBASTimeSeriesNode::save() const
{
    QJsonObject root = ExecutableNodeDelegateModel::save();
    root["temporalThreshLow"] = m_temporalThreshLow;
    root["temporalThresh"] = m_temporalThresh;
    root["spatialThresh"] = m_spatialThresh;
    root["multilookRg"] = m_multilookRg;
    root["multilookAz"] = m_multilookAz;
    root["unwrapMethod"] = m_unwrapMethod;
    root["alpha"] = m_alpha;
    root["coherenceThresh"] = m_coherenceThresh;
    root["temporalCoherenceThresh"] = m_temporalCoherenceThresh;
    root["refinementCohThresh"] = m_refinementCohThresh;
    root["refinementDefThresh"] = m_refinementDefThresh;
    root["outputNodeName"] = m_outputNodeName;
    return root;
}

void SBASTimeSeriesNode::load(QJsonObject const& json)
{
    m_temporalThreshLow = json["temporalThreshLow"].toDouble(0.0);
    m_temporalThresh = json["temporalThresh"].toDouble(200.0);
    m_spatialThresh = json["spatialThresh"].toDouble(500.0);
    m_multilookRg = json["multilookRg"].toInt(4);
    m_multilookAz = json["multilookAz"].toInt(4);
    m_unwrapMethod = json["unwrapMethod"].toInt(1);
    m_alpha = json["alpha"].toDouble(0.8);
    m_coherenceThresh = json["coherenceThresh"].toDouble(0.5);
    m_temporalCoherenceThresh = json["temporalCoherenceThresh"].toDouble(0.6);
    m_refinementCohThresh = json["refinementCohThresh"].toDouble(0.8);
    m_refinementDefThresh = json["refinementDefThresh"].toDouble(0.01);
    m_outputNodeName = json["outputNodeName"].toString(QStringLiteral("SBAS_Series"));

    // SOP rule 15: Must call base load last
    ExecutableNodeDelegateModel::load(json);

    updateLabels();
}

QStringList SBASTimeSeriesNode::previewImagePaths() const
{
    QStringList h5Paths;
    if (NodeUtils::loadCommittedOutputManifest(projectPath(), m_outputNodeName, h5Paths)) {
        QStringList jpgPaths;
        for (const QString& h5Path : h5Paths) {
            const QString jpgPath = QFileInfo(h5Path).absolutePath() + "/" +
                QFileInfo(h5Path).baseName() + ".jpg";
            if (QFileInfo::exists(jpgPath)) jpgPaths.append(jpgPath);
        }
        return jpgPaths;
    }
    return QStringList();
}

QString SBASTimeSeriesNode::projectPath() const
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

QString SBASTimeSeriesNode::projectName() const
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
            }
        }
    }

    if (iface) {
        return QFileInfo(iface->projectPath()).fileName();
    }
    return QString();
}

} // namespace QtNodes

