#include "DeformationRateFieldNode.h"
#include "NodeUtils.h"
#include "IApplicationInterface.h"
#include "InterfaceManager.h"
#include "MainWindow.h"
#include "WorkspaceUI.h"
#include "InSARLogManager.h"
#include "icon_source.h"
#include <QTimer>
#include <QJsonDocument>
#include <QMessageBox>
#include <QFileInfo>
#include <QFile>
#include <QDebug>
#include <QDir>
#include <QApplication>
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>
#include <QMap>
#include <QUuid>
#include <opencv2/opencv.hpp>
#include "FormatConversion.h"

namespace QtNodes {

DeformationRateFieldNode::DeformationRateFieldNode()
    : ExecutableNodeDelegateModel()
    , _widget(nullptr)
    , m_inputNodeLabel(nullptr)
    , m_modelTypeCombo(nullptr)
    , m_confidenceLevelCombo(nullptr)
    , m_cohThreshHighEdit(nullptr)
    , m_cohThreshMidEdit(nullptr)
    , m_uncertaintyThreshHighEdit(nullptr)
    , m_uncertaintyThreshMidEdit(nullptr)
    , m_colorMapCombo(nullptr)
    , m_showContourCheck(nullptr)
    , m_contourIntervalEdit(nullptr)
    , m_showArrowCheck(nullptr)
    , m_arrowSpacingEdit(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_resultLabel(nullptr)
    , m_modelType(1)
    , m_confidenceLevel(0.95)
    , m_coherenceThreshHigh(0.5)
    , m_coherenceThreshMid(0.3)
    , m_uncertaintyThreshHigh(2.0)
    , m_uncertaintyThreshMid(5.0)
    , m_colorMap(0)
    , m_showContour(true)
    , m_contourInterval(5)
    , m_showArrow(false)
    , m_arrowSpacing(10)
    , m_outputNodeName(QStringLiteral("RateField"))
    , m_worker(nullptr)
    , m_thread(nullptr)
{
    setExecutionMode(ExecutionMode::Automatic);
}

DeformationRateFieldNode::~DeformationRateFieldNode()
{
    stopExecution();
}

ProductInputContract DeformationRateFieldNode::productInputContract(PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    ProductInputContract contract;
    contract.semanticId = QStringLiteral("deformation_rate_field.input.sbas_time_series");
    contract.allowedProductTypes = QStringList()
        << QStringLiteral("sbas_time_series")
        << QStringLiteral("referenced_sbas_time_series");
    contract.requiredProvenanceFields = QStringList()
        << QStringLiteral("producer") << QStringLiteral("output_port");
    return contract;
}

ProductOutputContract DeformationRateFieldNode::productOutputContract(PortIndex portIndex) const
{
    ProductOutputContract contract;
    contract.semanticId = portIndex == 0
        ? QStringLiteral("deformation_rate_field.output.deformation_rate_field")
        : QStringLiteral("deformation_rate_field.output.preview");
    contract.publishedProductTypes = portIndex == 0
        ? QStringList() << QStringLiteral("deformation_rate_field")
        : QStringList() << QStringLiteral("preview");
    return contract;
}

unsigned int DeformationRateFieldNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 1;
    else
        return 2;
}

NodeDataType DeformationRateFieldNode::dataType(PortType portType, PortIndex portIndex) const
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

bool DeformationRateFieldNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return true;
}

QString DeformationRateFieldNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In)
        return QStringLiteral("SBAS成果");
    else {
        if (portIndex == 0)
            return tr("成果 *");
        else
            return tr("预览 ?");
    }
}

bool DeformationRateFieldNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out && portIndex == 1)
        return true;
    return false;
}

std::shared_ptr<NodeData> DeformationRateFieldNode::outData(PortIndex port)
{
    if (executionState() != ExecutionState::Completed)
        return nullptr;
    if (port == 0)
        return m_outputData;
    else
        return m_previewData;
}

void DeformationRateFieldNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
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

::QWidget* DeformationRateFieldNode::embeddedWidget()
{
    if (!_widget)
    {
        createWidget();
    }
    return _widget;
}

void DeformationRateFieldNode::setExecutionMode(ExecutionMode mode)
{
    ExecutableNodeDelegateModel::setExecutionMode(mode);
}

void DeformationRateFieldNode::createWidget()
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
    inputTitleLabel->setFixedWidth(80);
    m_inputNodeLabel = new QLabel(QStringLiteral("等待输入"));
    m_inputNodeLabel->setStyleSheet("color: gray;");
    inputLayout->addWidget(inputTitleLabel);
    inputLayout->addWidget(m_inputNodeLabel);
    mainLayout->addLayout(inputLayout);

    // Helper to add param row
    auto addParamRow = [&](const QString& labelText, QWidget* editWidget) {
        QHBoxLayout* layout = new QHBoxLayout();
        QLabel* label = new QLabel(labelText);
        label->setFixedWidth(80);
        layout->addWidget(label);
        layout->addWidget(editWidget);
        mainLayout->addLayout(layout);
    };

    m_modelTypeCombo = new QComboBox();
    m_modelTypeCombo->addItem(QStringLiteral("线性拟合"), 1);
    m_modelTypeCombo->addItem(QStringLiteral("二次多项式"), 2);
    m_modelTypeCombo->setCurrentIndex(m_modelType - 1);
    addParamRow(QStringLiteral("速率模型:"), m_modelTypeCombo);

    m_confidenceLevelCombo = new QComboBox();
    m_confidenceLevelCombo->addItem("90%", 0.90);
    m_confidenceLevelCombo->addItem("95%", 0.95);
    m_confidenceLevelCombo->addItem("99%", 0.99);
    int confIndex = m_confidenceLevelCombo->findData(m_confidenceLevel);
    if (confIndex >= 0) m_confidenceLevelCombo->setCurrentIndex(confIndex);
    else m_confidenceLevelCombo->setCurrentIndex(1); // default 95%
    addParamRow(QStringLiteral("置信水平:"), m_confidenceLevelCombo);

    m_cohThreshHighEdit = new QLineEdit(QString::number(m_coherenceThreshHigh));
    addParamRow(QStringLiteral("高质相干阈值:"), m_cohThreshHighEdit);

    m_cohThreshMidEdit = new QLineEdit(QString::number(m_coherenceThreshMid));
    addParamRow(QStringLiteral("中质相干阈值:"), m_cohThreshMidEdit);

    m_uncertaintyThreshHighEdit = new QLineEdit(QString::number(m_uncertaintyThreshHigh));
    addParamRow(QStringLiteral("高质不确阈值:"), m_uncertaintyThreshHighEdit);

    m_uncertaintyThreshMidEdit = new QLineEdit(QString::number(m_uncertaintyThreshMid));
    addParamRow(QStringLiteral("中质不确阈值:"), m_uncertaintyThreshMidEdit);

    m_colorMapCombo = new QComboBox();
    m_colorMapCombo->addItem(QStringLiteral("蓝-白-红"), 0);
    m_colorMapCombo->addItem(QStringLiteral("热力图"), 1);
    m_colorMapCombo->addItem(QStringLiteral("彩虹色"), 2);
    m_colorMapCombo->setCurrentIndex(m_colorMap);
    addParamRow(QStringLiteral("可视化色带:"), m_colorMapCombo);

    m_showContourCheck = new QCheckBox(QStringLiteral("显示等值线"));
    m_showContourCheck->setChecked(m_showContour);
    m_contourIntervalEdit = new QLineEdit(QString::number(m_contourInterval));
    QHBoxLayout* contourLayout = new QHBoxLayout();
    contourLayout->addWidget(m_showContourCheck);
    contourLayout->addWidget(new QLabel(QStringLiteral("间隔:")));
    contourLayout->addWidget(m_contourIntervalEdit);
    mainLayout->addLayout(contourLayout);

    m_showArrowCheck = new QCheckBox(QStringLiteral("显示矢量箭头"));
    m_showArrowCheck->setChecked(m_showArrow);
    m_arrowSpacingEdit = new QLineEdit(QString::number(m_arrowSpacing));
    QHBoxLayout* arrowLayout = new QHBoxLayout();
    arrowLayout->addWidget(m_showArrowCheck);
    arrowLayout->addWidget(new QLabel(QStringLiteral("间隔:")));
    arrowLayout->addWidget(m_arrowSpacingEdit);
    mainLayout->addLayout(arrowLayout);

    m_outputNodeNameEdit = new QLineEdit(m_outputNodeName);
    m_outputNodeNameEdit->setPlaceholderText(QStringLiteral("自动生成或手动输入"));
    addParamRow(QStringLiteral("目标节点名:"), m_outputNodeNameEdit);

    m_resultLabel = new QLabel(QStringLiteral("状态：等待分析"));
    m_resultLabel->setStyleSheet("QLabel { background-color: rgba(128, 128, 128, 20); border: 1px solid rgba(128, 128, 128, 80); border-radius: 4px; padding: 6px; font-size: 11px; line-height: 14px; }");
    m_resultLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_resultLabel->setWordWrap(true);
    mainLayout->addWidget(m_resultLabel);

    // Connections
    connect(m_modelTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        m_modelType = m_modelTypeCombo->itemData(idx).toInt(); invalidateExecution();
    });
    connect(m_confidenceLevelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        m_confidenceLevel = m_confidenceLevelCombo->itemData(idx).toDouble(); invalidateExecution();
    });
    connect(m_cohThreshHighEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_coherenceThreshHigh = text.toDouble(); invalidateExecution();
    });
    connect(m_cohThreshMidEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_coherenceThreshMid = text.toDouble(); invalidateExecution();
    });
    connect(m_uncertaintyThreshHighEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_uncertaintyThreshHigh = text.toDouble(); invalidateExecution();
    });
    connect(m_uncertaintyThreshMidEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_uncertaintyThreshMid = text.toDouble(); invalidateExecution();
    });
    connect(m_colorMapCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        m_colorMap = m_colorMapCombo->itemData(idx).toInt(); invalidateExecution();
    });
    connect(m_showContourCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_showContour = checked; invalidateExecution();
    });
    connect(m_contourIntervalEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_contourInterval = text.toInt(); invalidateExecution();
    });
    connect(m_showArrowCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_showArrow = checked; invalidateExecution();
    });
    connect(m_arrowSpacingEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_arrowSpacing = text.toInt(); invalidateExecution();
    });
    connect(m_outputNodeNameEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_outputNodeName = text; invalidateExecution();
    });

    updateLabels();
}

void DeformationRateFieldNode::updateLabels()
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
        QString h5Path = projectPath() + "/" + m_outputNodeName + "/DeformationRateField.h5";
        if (QFileInfo::exists(h5Path)) {
            m_resultLabel->setText(QStringLiteral("状态：速率场分析完成\n输出：%1").arg(QFileInfo(h5Path).fileName()));
        } else {
            m_resultLabel->setText(QStringLiteral("状态：等待分析"));
        }
    }
    updateWidgetSize();
}

void DeformationRateFieldNode::updateWidgetSize()
{
    if (_widget) {
        _widget->setFixedWidth(300);
        _widget->adjustSize();
        Q_EMIT embeddedWidgetSizeUpdated();
    }
}

bool DeformationRateFieldNode::validateInputs() const
{
    if (!m_inputData || m_inputData->filePaths().isEmpty()) return false;
    if (m_outputNodeName.isEmpty()) return false;
    return true;
}

bool DeformationRateFieldNode::prepareToStart()
{
    if (!validateInputs()) {
        if (m_resultLabel) {
            m_resultLabel->setText(QStringLiteral("状态：参数校验未通过，请连接输入并指定目标节点名！"));
        }
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;
        setState(ExecutionState::Error);
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
    m_preparedDstNode = m_outputNodeName.trimmed();
    m_preparedModelType = m_modelType;
    m_preparedInputPaths = m_inputData ? m_inputData->filePaths() : QStringList();
    m_preparedOutputPaths = QStringList()
        << QDir(m_preparedProjectRoot).absoluteFilePath(
            m_preparedDstNode + "/DeformationRateField.h5");
    if (m_preparedProjectRoot.isEmpty() || m_preparedProjectName.isEmpty() ||
        m_preparedDstNode.isEmpty() || m_preparedInputPaths.isEmpty()) {
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;
        setState(ExecutionState::Error);
        return false;
    }

    if (isAutoTriggered()) {
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
        return true;
    }

    m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(
        NodeUtils::getProjectContext(_widget),
        m_preparedDstNode,
        m_preparedOutputPaths,
        _widget
    );

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::Cancel) {
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;
        return false;
    }

    return true;
}

void DeformationRateFieldNode::execute()
{
    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting) {
        if (!validateAndRestoreOutput()) {
            onError(QStringLiteral("Existing deformation rate-field output does not satisfy its semantic identity contract."));
        }
        return;
    }

    executeProcessing();
}

void DeformationRateFieldNode::executeProcessing()
{
    InSARLogManager::LogInfo("DeformationRateFieldNode", "executeProcessing started.");
    stopExecution();
    m_generatedOutputPath.clear();
    m_workerOutputPaths.clear();
    m_resultPublishingFailed = false;

    QString transactionError;
    if (!NodeUtils::beginOutputTransaction(m_preparedProjectRoot, m_preparedDstNode,
                                            m_preparedOutputPaths, m_preparedInputPaths,
                                            m_outputTransaction, &transactionError)) {
        onError(transactionError);
        return;
    }
    m_outputTransaction.executionRevision = executionRevision();
    QMap<QString, QString> provenance;
    provenance.insert(QStringLiteral("producer"), name());
    provenance.insert(QStringLiteral("output_port"), productOutputContract(0).semanticId);
    if (!NodeUtils::setOutputTransactionProductDescriptor(m_outputTransaction,
            ProductDescriptor::create(QStringLiteral("deformation_rate_field"),
                productOutputContract(0).schemaId, productOutputContract(0).schemaVersion,
                productOutputContract(0).publishedState, name(), provenance), &transactionError)) {
        onError(transactionError);
        return;
    }

    m_thread = new QThread(this);
    m_worker = new DeformationRateFieldWorker();
    m_worker->moveToThread(m_thread);

    const QString stagingOutputDir = QDir(m_preparedProjectRoot)
        .absoluteFilePath(m_outputTransaction.stagingName);
    const QString projName = m_preparedProjectName;
    const QString dstNode = m_preparedDstNode;
    const QStringList filePaths = m_preparedInputPaths;
    const int modelType = m_preparedModelType;
    const double confidenceLevel = m_confidenceLevel;
    const double coherenceThreshHigh = m_coherenceThreshHigh;
    const double coherenceThreshMid = m_coherenceThreshMid;
    const double uncertaintyThreshHigh = m_uncertaintyThreshHigh;
    const double uncertaintyThreshMid = m_uncertaintyThreshMid;
    const int colorMap = m_colorMap;
    const bool showContour = m_showContour;
    const int contourInterval = m_contourInterval;
    const bool showArrow = m_showArrow;
    const int arrowSpacing = m_arrowSpacing;

    DeformationRateFieldWorker* worker = m_worker;
    connect(m_thread, &QThread::started, m_worker, [worker, stagingOutputDir, projName, dstNode,
                                                      filePaths, modelType, confidenceLevel,
                                                      coherenceThreshHigh, coherenceThreshMid,
                                                      uncertaintyThreshHigh, uncertaintyThreshMid,
                                                      colorMap, showContour, contourInterval,
                                                      showArrow, arrowSpacing]() {
        worker->analyze_rate_field(
            stagingOutputDir,
            projName,
            dstNode,
            filePaths,
            modelType,
            confidenceLevel,
            coherenceThreshHigh,
            coherenceThreshMid,
            uncertaintyThreshHigh,
            uncertaintyThreshMid,
            colorMap,
            showContour,
            contourInterval,
            showArrow,
            arrowSpacing,
            true
        );
    });

    connect(m_worker, &DeformationRateFieldWorker::updateProcess, this, &DeformationRateFieldNode::onProgressUpdate);
    connect(m_worker, &DeformationRateFieldWorker::endProcess, this, &DeformationRateFieldNode::onProcessingFinished);
    connect(m_worker, &DeformationRateFieldWorker::errorProcess, this, &DeformationRateFieldNode::onError);
    connect(m_worker, &DeformationRateFieldWorker::cancelled, this, &DeformationRateFieldNode::onCancelled);
    connect(m_worker, &DeformationRateFieldWorker::outputsGenerated,
            this, &DeformationRateFieldNode::onResultsGenerated);
    connect(m_worker, &DeformationRateFieldWorker::endProcess, m_thread, &QThread::quit);
    connect(m_worker, &DeformationRateFieldWorker::errorProcess, m_thread, &QThread::quit);
    connect(m_worker, &DeformationRateFieldWorker::cancelled, m_thread, &QThread::quit);

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);

    setState(ExecutionState::Running);
    setProgress(0);
    deferAutomaticCompletion();

    QTimer::singleShot(0, this, [this]() {
        if (m_thread && m_thread->isRunning()) {
            setState(ExecutionState::Running);
        }
    });

    m_thread->start();
}

void DeformationRateFieldNode::stopExecution()
{
    if (m_worker) {
        m_worker->StopProcess();
    }
    if (m_thread && m_thread->isRunning()) {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
    }
    m_thread = nullptr;
    m_worker = nullptr;
    IApplicationInterface* iface = NodeUtils::getProjectContext(_widget);
    NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("stopped"),
                                        iface ? iface->projectXml() : nullptr);
    m_generatedOutputPath.clear();
    m_workerOutputPaths.clear();
    m_outputData.reset();
    m_previewData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);
}

void DeformationRateFieldNode::cleanUpThreadAndWorker()
{
    QThread* thread = m_thread;
    m_thread = nullptr;
    m_worker = nullptr;
    if (thread && thread->isRunning()) {
        thread->quit();
    }
}

void DeformationRateFieldNode::processAutomatically()
{
    if (prepareToStart()) {
        executeProcessing();
    } else {
        setState(ExecutionState::Idle);
    }
}

void DeformationRateFieldNode::onProgressUpdate(int progress, const QString& message)
{
    if (isAutomaticExecutionObsolete()) {
        return;
    }

    Q_UNUSED(message);
    setProgress(progress);
}

void DeformationRateFieldNode::onError(const QString& error)
{
    cleanUpThreadAndWorker();
    IApplicationInterface* iface = NodeUtils::getProjectContext(_widget);
    NodeUtils::abandonOutputTransaction(m_outputTransaction, error,
                                        iface ? iface->projectXml() : nullptr);
    m_generatedOutputPath.clear();
    m_workerOutputPaths.clear();
    m_outputData.reset();
    m_previewData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);
    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    InSARLogManager::LogError("DeformationRateFieldNode", "Error during rate field analysis: " + error);
    setState(ExecutionState::Error);
    finishExecution();
}

void DeformationRateFieldNode::onProcessingFinished()
{
    cleanUpThreadAndWorker();
    IApplicationInterface* iface = NodeUtils::getProjectContext(_widget);
    if (discardObsoleteAutomaticExecution()) {
        NodeUtils::abandonOutputTransaction(m_outputTransaction,
                                            QStringLiteral("obsolete automatic execution"),
                                            iface ? iface->projectXml() : nullptr);
        m_outputData.reset();
        m_previewData.reset();
        setOutputData(0, nullptr);
        setOutputData(1, nullptr);
        return;
    }

    InSARLogManager::LogInfo("DeformationRateFieldNode", "executeProcessing completed.");
    QString transactionError;
    const QString dstNode = m_preparedDstNode;
    if (!iface || !iface->projectXml()) {
        onError(QStringLiteral("Project XML context is unavailable for rate field output commit."));
        return;
    }

    QStringList requiredDatasets = QStringList()
        << QStringLiteral("velocity_std")
        << QStringLiteral("velocity_lower")
        << QStringLiteral("velocity_upper")
        << QStringLiteral("quality_mask")
        << QStringLiteral("mask");
    if (m_preparedModelType == 2) {
        requiredDatasets << QStringLiteral("velocity_nonlinear")
                         << QStringLiteral("acceleration")
                         << QStringLiteral("acceleration_std");
    }

    QStringList finalPaths;
    OutputCommitLease commitLease = acquireOutputCommitLease(m_outputTransaction.executionRevision);
    if (!commitLease) {
        NodeUtils::abandonOutputTransaction(m_outputTransaction,
            QStringLiteral("obsolete execution revision"), iface->projectXml());
        return;
    }
    if (m_resultPublishingFailed || m_workerOutputPaths.size() != 1 ||
        !NodeUtils::validateStagedOutputTransaction(m_outputTransaction, &transactionError) ||
        !NodeUtils::validateStagedH5Datasets(m_outputTransaction, requiredDatasets, &transactionError) ||
        !NodeUtils::workerOutputsMatchManifest(m_preparedOutputPaths, m_workerOutputPaths, &transactionError) ||
        !NodeUtils::promoteOutputTransaction(m_outputTransaction, finalPaths, &transactionError) ||
        !NodeUtils::prepareOutputTransactionMetadataCommit(m_outputTransaction, iface->projectXml(),
                                                            NodeUtils::getProjectFilePath(_widget), &transactionError)) {
        if (transactionError.isEmpty()) {
            transactionError = QStringLiteral("Rate field worker did not return the expected staging output.");
        }
        onError(transactionError);
        return;
    }

    NodeUtils::removeDataNodeFromProject(iface, dstNode, false, false);
    const QString relativePath = QStringLiteral("/%1/DeformationRateField.h5").arg(dstNode);
    if (iface->projectXml()->XMLFile_add_SBAS(dstNode.toStdString().c_str(), "DeformationRateField",
                                              relativePath.toStdString().c_str()) < 0 ||
        !NodeUtils::saveProjectXmlAtomically(iface->projectXml(), NodeUtils::getProjectFilePath(_widget), &transactionError) ||
        !NodeUtils::markOutputTransactionMetadataCommitted(m_outputTransaction, &transactionError)) {
        if (transactionError.isEmpty()) {
            transactionError = QStringLiteral("Unable to commit rate field output metadata.");
        }
        onError(transactionError);
        return;
    }

    if (QStandardItemModel* model = iface->projectModel()) {
        const QList<QStandardItem*> projects = model->findItems(m_preparedProjectName);
        if (!projects.isEmpty()) {
            QStandardItem* outputNode = NodeUtils::findOrCreateProjectNode(
                projects.first(), dstNode, "SBAS-1.0", FOLDER_ICON);
            if (outputNode) {
                NodeUtils::findOrCreateChildItem(outputNode, "DeformationRateField", "SBAS",
                                                  finalPaths.first(), IMAGEDATA_ICON);
            }
        }
    }
    iface->refreshProjectTree();

    m_outputNodeName = dstNode;
    m_generatedOutputPath = finalPaths.first();
    m_outputData = std::make_shared<ImportedFileData>(QStringList() << m_generatedOutputPath, m_outputNodeName);
    m_outputData->setProductDescriptor(ProductDescriptor::fromJson(m_outputTransaction.productDescriptor));
    generateStaticPreviewJpg(true);
}

void DeformationRateFieldNode::onResultsGenerated(const QString& dstNode, const QString& outputH5Path)
{
    if (isAutomaticExecutionObsolete()) {
        return;
    }
    if (dstNode != m_preparedDstNode || outputH5Path.isEmpty()) {
        m_resultPublishingFailed = true;
    }
    m_workerOutputPaths.append(outputH5Path);
}

void DeformationRateFieldNode::onCancelled()
{
    cleanUpThreadAndWorker();
    IApplicationInterface* iface = NodeUtils::getProjectContext(_widget);
    NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("cancelled"),
                                        iface ? iface->projectXml() : nullptr);
    m_generatedOutputPath.clear();
    m_workerOutputPaths.clear();
    m_outputData.reset();
    m_previewData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);
    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    setState(ExecutionState::Stopped);
    Q_EMIT executionStopped();
    Q_EMIT computingFinished();
}

bool DeformationRateFieldNode::validateAndRestoreOutput()
{
    QStringList outputPaths;
    ProductDescriptor::Ptr descriptor;
    QString identityError;
    if (!NodeUtils::loadCommittedOutputManifest(projectPath(), m_outputNodeName, outputPaths) ||
        outputPaths.size() != 1 ||
        QFileInfo(outputPaths.first()).fileName() != QStringLiteral("DeformationRateField.h5") ||
        !NodeUtils::loadCommittedOutputProductDescriptor(projectPath(), m_outputNodeName, descriptor, &identityError) ||
        !validatePublishedDescriptor(productOutputContract(0), descriptor).accepted ||
        !NodeUtils::validateH5Identities(outputPaths, descriptor, &identityError)) {
        return false;
    }

    const QString h5Path = outputPaths.first();
    m_generatedOutputPath = h5Path;
    m_outputData = std::make_shared<ImportedFileData>(QStringList() << h5Path, m_outputNodeName);
    m_outputData->setProductDescriptor(descriptor);
    setOutputData(0, m_outputData);
    generateStaticPreviewJpg();

    setState(ExecutionState::Completed);
    setProgress(100);
    updateLabels();
    Q_EMIT dataUpdated(0);
    return true;
}

void DeformationRateFieldNode::generateStaticPreviewJpg(bool completeExecution)
{
    QString outDir = projectPath() + "/" + m_outputNodeName;
    QString h5Path = outDir + "/DeformationRateField.h5";
    QString jpgPath = outDir + "/velocity_overlay.jpg";

    if (!QFileInfo::exists(h5Path)) return;

    // Resolved in the background task to keep HDF5 probing off the UI thread.
    const auto renderedInputs = std::make_shared<QStringList>();
    QFutureWatcher<void>* watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher, h5Path, jpgPath, completeExecution, renderedInputs]() {
        if (discardObsoleteAutomaticExecution()) {
            watcher->deleteLater();
            return;
        }

        if (NodeUtils::isJpgPreviewCurrent(*renderedInputs, jpgPath)) {
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
        } else {
            m_previewData.reset();
            setOutputData(1, nullptr);
            if (!completeExecution) {
                Q_EMIT dataUpdated(1);
            }
        }

        if (completeExecution) {
            if (!m_outputData || !m_outputData->productDescriptor()) {
                onError(QStringLiteral("Committed deformation rate-field output has no semantic descriptor for publication."));
                watcher->deleteLater();
                return;
            }

            setProgress(100);
            setState(ExecutionState::Running);
            updateLabels();
            setOutputData(0, m_outputData);
            setOutputData(1, m_previewData);
            finishExecution();
        }
        watcher->deleteLater();
    });

    watcher->setFuture(QtConcurrent::run([h5Path, jpgPath, renderedInputs]() {
        NodeUtils::Hdf5Locker locker;

        FormatConversion FC;
        cv::Mat velocity, mask;

        std::string sbasPath;
        FC.read_str_from_h5(h5Path.toStdString().c_str(), "sbas_h5_path", sbasPath);
        int ret = FC.read_array_from_h5(h5Path.toStdString().c_str(), "velocity_nonlinear", velocity);
        if (ret != 0 || velocity.empty()) {
            if (sbasPath.empty()) {
                renderedInputs->clear();
                return;
            }
            *renderedInputs = QStringList() << h5Path << QString::fromStdString(sbasPath);
            if (FC.read_array_from_h5(sbasPath.c_str(), "defomation_velocity", velocity) != 0 ||
                velocity.empty()) {
                renderedInputs->clear();
                return;
            }
        } else {
            *renderedInputs = QStringList() << h5Path;
        }

        // The cache can only be accepted after the input actually used to render is known.
        if (NodeUtils::isJpgPreviewCurrent(*renderedInputs, jpgPath)) {
            return;
        }
        FC.read_array_from_h5(h5Path.toStdString().c_str(), "mask", mask);

        if (!velocity.empty() && !mask.empty()) {
            if (velocity.type() != CV_64F) {
                velocity.convertTo(velocity, CV_64F);
            }
            std::vector<double> valid_vals;
            for (int r = 0; r < velocity.rows; ++r) {
                for (int c = 0; c < velocity.cols; ++c) {
                    if (mask.at<int>(r, c) > 0) {
                        double val = velocity.at<double>(r, c);
                        if (!std::isnan(val) && !std::isinf(val)) {
                            valid_vals.push_back(val);
                        }
                    }
                }
            }

            double min_stretch = -10.0;
            double max_stretch = 10.0;
            if (!valid_vals.empty()) {
                std::sort(valid_vals.begin(), valid_vals.end());
                int low_idx = static_cast<int>(valid_vals.size() * 0.02);
                int high_idx = static_cast<int>(valid_vals.size() * 0.98);
                if (high_idx >= valid_vals.size()) high_idx = valid_vals.size() - 1;
                min_stretch = valid_vals[low_idx];
                max_stretch = valid_vals[high_idx];
                if (max_stretch <= min_stretch) {
                    max_stretch = min_stretch + 1.0;
                }
            }

            cv::Mat normalized = cv::Mat::zeros(velocity.size(), CV_8UC1);
            for (int r = 0; r < velocity.rows; ++r) {
                for (int c = 0; c < velocity.cols; ++c) {
                    if (mask.at<int>(r, c) > 0) {
                        double val = velocity.at<double>(r, c);
                        if (std::isnan(val) || std::isinf(val)) {
                            normalized.at<uchar>(r, c) = 127;
                        } else {
                            double norm = (val - min_stretch) / (max_stretch - min_stretch) * 255.0;
                            if (norm < 0.0) norm = 0.0;
                            if (norm > 255.0) norm = 255.0;
                            normalized.at<uchar>(r, c) = static_cast<uchar>(norm);
                        }
                    } else {
                        normalized.at<uchar>(r, c) = 255;
                    }
                }
            }

            cv::Mat color_img;
            cv::Mat lut(1, 256, CV_8UC3);
            for (int i = 0; i < 256; ++i) {
                if (i <= 127) {
                    double t = i / 127.0;
                    lut.at<cv::Vec3b>(0, i) = cv::Vec3b(255, static_cast<uchar>(255 * t), static_cast<uchar>(255 * t));
                } else {
                    double t = (i - 128) / 127.0;
                    lut.at<cv::Vec3b>(0, i) = cv::Vec3b(static_cast<uchar>(255 * (1.0 - t)), static_cast<uchar>(255 * (1.0 - t)), 255);
                }
            }
            cv::LUT(normalized, lut, color_img);

            for (int r = 0; r < velocity.rows; ++r) {
                for (int c = 0; c < velocity.cols; ++c) {
                    if (mask.at<int>(r, c) == 0) {
                        color_img.at<cv::Vec3b>(r, c) = cv::Vec3b(255, 255, 255);
                    }
                }
            }
            const QFileInfo jpgInfo(jpgPath);
            const QString temporaryJpgPath = jpgInfo.absolutePath() + "/." + jpgInfo.completeBaseName() +
                "." + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".jpg";
            QFile::remove(temporaryJpgPath);
            if (cv::imwrite(temporaryJpgPath.toStdString(), color_img)) {
                if (!NodeUtils::replaceJpgPreviewAtomically(temporaryJpgPath, jpgPath)) {
                    QFile::remove(temporaryJpgPath);
                }
            }
        }
    }));
}

QJsonObject DeformationRateFieldNode::save() const
{
    QJsonObject root = ExecutableNodeDelegateModel::save();
    root["modelType"] = m_modelType;
    root["confidenceLevel"] = m_confidenceLevel;
    root["coherenceThreshHigh"] = m_coherenceThreshHigh;
    root["coherenceThreshMid"] = m_coherenceThreshMid;
    root["uncertaintyThreshHigh"] = m_uncertaintyThreshHigh;
    root["uncertaintyThreshMid"] = m_uncertaintyThreshMid;
    root["colorMap"] = m_colorMap;
    root["showContour"] = m_showContour;
    root["contourInterval"] = m_contourInterval;
    root["showArrow"] = m_showArrow;
    root["arrowSpacing"] = m_arrowSpacing;
    root["outputNodeName"] = m_outputNodeName;
    return root;
}

void DeformationRateFieldNode::load(QJsonObject const& json)
{
    m_modelType = json["modelType"].toInt(1);
    m_confidenceLevel = json["confidenceLevel"].toDouble(0.95);
    m_coherenceThreshHigh = json["coherenceThreshHigh"].toDouble(0.5);
    m_coherenceThreshMid = json["coherenceThreshMid"].toDouble(0.3);
    m_uncertaintyThreshHigh = json["uncertaintyThreshHigh"].toDouble(2.0);
    m_uncertaintyThreshMid = json["uncertaintyThreshMid"].toDouble(5.0);
    m_colorMap = json["colorMap"].toInt(0);
    m_showContour = json["showContour"].toBool(true);
    m_contourInterval = json["contourInterval"].toInt(5);
    m_showArrow = json["showArrow"].toBool(false);
    m_arrowSpacing = json["arrowSpacing"].toInt(10);
    m_outputNodeName = json["outputNodeName"].toString(QStringLiteral("RateField"));

    // SOP rule 15: Must call base load last
    ExecutableNodeDelegateModel::load(json);

    updateLabels();
}

QStringList DeformationRateFieldNode::previewImagePaths() const
{
    QStringList outputPaths;
    if (!NodeUtils::loadCommittedOutputManifest(projectPath(), m_outputNodeName, outputPaths) ||
        outputPaths.size() != 1 ||
        QFileInfo(outputPaths.first()).fileName() != QStringLiteral("DeformationRateField.h5")) {
        return QStringList();
    }
    const QString jpgPath = QFileInfo(outputPaths.first()).absolutePath() + "/velocity_overlay.jpg";
    if (QFileInfo::exists(jpgPath)) {
        return QStringList() << jpgPath;
    }
    return QStringList();
}

QString DeformationRateFieldNode::projectPath() const
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

QString DeformationRateFieldNode::projectName() const
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

