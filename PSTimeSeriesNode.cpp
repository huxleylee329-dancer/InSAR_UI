#include "PSTimeSeriesNode.h"
#include "FormatConversion.h"
#include "Utils.h"
#include "NodeUtils.h"
#include "InSARLogManager.h"
#include "IApplicationInterface.h"
#include "InterfaceManager.h"
#include "MainWindow.h"
#include "WorkspaceUI.h"
#include <QApplication>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>

namespace QtNodes {

PSTimeSeriesNode::PSTimeSeriesNode()
    : _widget(nullptr)
    , m_coherenceThresh(0.7)
    , m_maxDeformationRate(0.1)
    , m_atmosphericWindow(500)
    , m_outputNodeName("PS_TimeSeries")
    , m_worker(nullptr)
    , m_thread(nullptr)
{
    setState(ExecutionState::Idle);
}

PSTimeSeriesNode::~PSTimeSeriesNode()
{
    if (m_worker) m_worker->StopProcess();
    if (m_thread && m_thread->isRunning()) {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
    }
}

ProductInputContract PSTimeSeriesNode::productInputContract(PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    ProductInputContract contract;
    contract.semanticId = QStringLiteral("ps_time_series.input.ps_network");
    contract.allowedProductTypes = QStringList() << QStringLiteral("ps_network");
    contract.requiredProvenanceFields = QStringList()
        << QStringLiteral("producer") << QStringLiteral("output_port");
    return contract;
}

ProductOutputContract PSTimeSeriesNode::productOutputContract(PortIndex portIndex) const
{
    ProductOutputContract contract;
    contract.semanticId = portIndex == 0
        ? QStringLiteral("ps_time_series.output.ps_time_series")
        : QStringLiteral("ps_time_series.output.preview");
    contract.publishedProductTypes = portIndex == 0
        ? QStringList() << QStringLiteral("ps_time_series")
        : QStringList() << QStringLiteral("preview");
    return contract;
}

unsigned int PSTimeSeriesNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 1;
    else
        return 2;
}

NodeDataType PSTimeSeriesNode::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) {
        return NodeDataType{"imported_file", "Imported File"};
    } else {
        if (portIndex == 0)
            return NodeDataType{"imported_file", "Imported File"};
        else
            return NodeDataType{"image_info", "Image Info"};
    }
}

bool PSTimeSeriesNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return true;
}

QString PSTimeSeriesNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In)
        return QStringLiteral("输入网络(H5)");
    else {
        if (portIndex == 0)
            return tr("成果 *");
        else
            return tr("形变速率预览 ?");
    }
}

bool PSTimeSeriesNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out && portIndex == 1)
        return true;
    return false;
}

std::shared_ptr<NodeData> PSTimeSeriesNode::outData(PortIndex port)
{
    if (executionState() != ExecutionState::Completed)
        return nullptr;
    if (port == 0)
        return m_outputData;
    else
        return m_previewData;
}

void PSTimeSeriesNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
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

::QWidget* PSTimeSeriesNode::embeddedWidget()
{
    if (!_widget)
    {
        createWidget();
    }
    return _widget;
}

void PSTimeSeriesNode::setExecutionMode(ExecutionMode mode)
{
    ExecutableNodeDelegateModel::setExecutionMode(mode);
}

void PSTimeSeriesNode::createWidget()
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

    // Parameters Layout helper
    auto addParamRow = [&](const QString& labelText, QWidget* editWidget) {
        QHBoxLayout* layout = new QHBoxLayout();
        QLabel* label = new QLabel(labelText);
        label->setFixedWidth(80); // SOP: Fixed label width
        layout->addWidget(label);
        layout->addWidget(editWidget);
        mainLayout->addLayout(layout);
    };

    m_coherenceThreshEdit = new QLineEdit(QString::number(m_coherenceThresh));
    addParamRow(QStringLiteral("时间相干阈值:"), m_coherenceThreshEdit);

    m_maxDeformationRateEdit = new QLineEdit(QString::number(m_maxDeformationRate));
    addParamRow(QStringLiteral("最大形变速率:"), m_maxDeformationRateEdit);

    m_atmosphericWindowEdit = new QLineEdit(QString::number(m_atmosphericWindow));
    addParamRow(QStringLiteral("大气滤波窗口:"), m_atmosphericWindowEdit);

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
    connect(m_coherenceThreshEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_coherenceThresh = text.toDouble(); invalidateExecution();
    });
    connect(m_maxDeformationRateEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_maxDeformationRate = text.toDouble(); invalidateExecution();
    });
    connect(m_atmosphericWindowEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_atmosphericWindow = text.toInt(); invalidateExecution();
    });
    connect(m_outputNodeNameEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_outputNodeName = text; invalidateExecution();
    });

    updateLabels(); // Widget initial label update (SOP rule 12)
}

void PSTimeSeriesNode::updateLabels()
{
    if (m_inputNodeLabel) {
        if (m_inputData) {
            m_inputNodeLabel->setText(m_inputData->nodeName());
            m_inputNodeLabel->setStyleSheet("color: black;");
        } else {
            m_inputNodeLabel->setText(QStringLiteral("等待输入"));
            m_inputNodeLabel->setStyleSheet("color: gray;");
        }
    }
    updateWidgetSize();
}

void PSTimeSeriesNode::updateWidgetSize()
{
    if (_widget) {
        _widget->setFixedWidth(300);
        _widget->adjustSize();
        Q_EMIT embeddedWidgetSizeUpdated();
    }
}

bool PSTimeSeriesNode::validateInputs() const
{
    if (!m_inputData || m_inputData->filePaths().isEmpty())
        return false;
    if (m_outputNodeName.isEmpty())
        return false;
    if (m_coherenceThresh <= 0.0 || m_maxDeformationRate <= 0.0)
        return false;
    return true;
}

bool PSTimeSeriesNode::prepareToStart()
{
    if (!validateInputs()) {
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

    m_preparedDstNode = m_outputNodeName;
    m_preparedOutputPaths = QStringList() <<
        QDir(projectPath()).absoluteFilePath(m_preparedDstNode + "/PS_time_series.h5");
    m_preparedInputPaths = m_inputData ? m_inputData->filePaths() : QStringList();

    if (isAutoTriggered()) {
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
        return true;
    }

    // 检查并提示覆盖
    if (executionMode() == ExecutionMode::Manual) {
        m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(
            NodeUtils::getProjectContext(_widget), m_preparedDstNode, m_preparedOutputPaths
        );
        if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::Cancel) {
            m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;
            return false;
        }
    } else {
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
    }

    return true;
}

void PSTimeSeriesNode::execute()
{
    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting) {
        if (!validateAndRestoreOutput()) {
            onError(QStringLiteral("Existing PS time-series output does not satisfy its semantic identity contract."));
        }
        return;
    }

    executeProcessing();
}

void PSTimeSeriesNode::executeProcessing()
{
    InSARLogManager::LogInfo("PSTimeSeriesNode", "executeProcessing started.");
    stopExecution();

    QString projPath = projectPath();
    QString projName = projectName();
    const QStringList networkFileList = m_preparedInputPaths;
    QString transactionError;
    if (!NodeUtils::beginOutputTransaction(projPath, m_preparedDstNode, m_preparedOutputPaths,
                                           networkFileList, m_outputTransaction, &transactionError, nullptr,
                                           NodeUtils::getProjectFilePath(_widget))) {
        onError(transactionError);
        return;
    }
    m_outputTransaction.executionRevision = executionRevision();
    QMap<QString, QString> provenance;
    provenance.insert(QStringLiteral("producer"), name());
    provenance.insert(QStringLiteral("output_port"), productOutputContract(0).semanticId);
    if (!NodeUtils::setOutputTransactionProductDescriptor(m_outputTransaction,
            ProductDescriptor::create(QStringLiteral("ps_time_series"),
                productOutputContract(0).schemaId, productOutputContract(0).schemaVersion,
                productOutputContract(0).publishedState, name(), provenance), &transactionError)) {
        onError(transactionError);
        return;
    }
    m_outputData.reset();
    m_previewData.reset();
    m_generatedOutputPaths.clear();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);

    m_thread = new QThread(this);
    m_worker = new PSTimeSeriesWorker();
    m_worker->moveToThread(m_thread);
    const QString stagingNode = m_outputTransaction.stagingName;
    connect(m_thread, &QThread::started, m_worker, [this, projPath, projName, stagingNode, networkFileList]() {
        m_worker->ps_time_series(
            m_coherenceThresh,
            m_maxDeformationRate,
            m_atmosphericWindow,
            projPath,
            projName,
            stagingNode,
            networkFileList,
            true
        );
    });

    connect(m_worker, &PSTimeSeriesWorker::updateProcess, this, &PSTimeSeriesNode::onProgressUpdate);
    connect(m_worker, &PSTimeSeriesWorker::outputsGenerated, this, &PSTimeSeriesNode::onOutputsGenerated);
    connect(m_worker, &PSTimeSeriesWorker::endProcess, this, &PSTimeSeriesNode::onProcessingFinished);
    connect(m_worker, &PSTimeSeriesWorker::endProcess, m_thread, &QThread::quit);
    connect(m_worker, &PSTimeSeriesWorker::errorProcess, this, &PSTimeSeriesNode::onError);
    connect(m_worker, &PSTimeSeriesWorker::errorProcess, m_thread, &QThread::quit);
    connect(m_worker, &PSTimeSeriesWorker::cancelled, this, &PSTimeSeriesNode::onCancelled);
    connect(m_worker, &PSTimeSeriesWorker::cancelled, m_thread, &QThread::quit);

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);

    setState(ExecutionState::Running);
    setProgress(0);

    QTimer::singleShot(0, this, [this]() {
        if (m_thread && m_thread->isRunning()) {
            setState(ExecutionState::Running);
        }
    });

    deferAutomaticCompletion();
    m_thread->start();
}

void PSTimeSeriesNode::stopExecution()
{
    if (m_worker) {
        m_worker->StopProcess();
    }
}

void PSTimeSeriesNode::onProgressUpdate(int progress, const QString& message)
{
    if (isAutomaticExecutionObsolete()) {
        return;
    }

    setProgress(progress);
    if (m_resultLabel) {
        m_resultLabel->setText(QStringLiteral("正在反演 (%1%): %2").arg(progress).arg(message));
    }
}

void PSTimeSeriesNode::onOutputsGenerated(const QStringList& outputPaths)
{
    m_generatedOutputPaths = outputPaths;
}

void PSTimeSeriesNode::onError(const QString& error)
{
    NodeUtils::abandonOutputTransaction(m_outputTransaction, error, projectXml());
    m_worker = nullptr;
    m_thread = nullptr;
    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    InSARLogManager::LogError("PSTimeSeriesNode", "Error in time series inversion: " + error);
    if (m_resultLabel) {
        m_resultLabel->setText(QStringLiteral("计算出错: ") + error);
    }
    m_outputData.reset();
    m_previewData.reset();
    m_generatedOutputPaths.clear();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);
    setState(ExecutionState::Error);
    finishExecution();
}

void PSTimeSeriesNode::onCancelled()
{
    InSARLogManager::LogInfo("PSTimeSeriesNode", "PS time-series cancellation cleanup completed.");
    m_worker = nullptr;
    m_thread = nullptr;
    NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("cancelled"), projectXml());
    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    m_outputData.reset();
    m_previewData.reset();
    m_generatedOutputPaths.clear();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);
    if (m_resultLabel) m_resultLabel->setText(QStringLiteral("已取消"));
    setState(ExecutionState::Stopped);
    Q_EMIT executionStopped();
    Q_EMIT computingFinished();
}

void PSTimeSeriesNode::onProcessingFinished()
{
    m_worker = nullptr;
    m_thread = nullptr;
    if (discardObsoleteAutomaticExecution()) {
        NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("obsolete automatic execution"), projectXml());
        return;
    }

    OutputCommitLease commitLease = acquireOutputCommitLease(m_outputTransaction.executionRevision);
    if (!commitLease) {
        NodeUtils::abandonOutputTransaction(m_outputTransaction,
            QStringLiteral("obsolete execution revision"), projectXml());
        return;
    }

    InSARLogManager::LogInfo("PSTimeSeriesNode", "executeProcessing completed successfully.");
    
    const QString dstNode = m_preparedDstNode;
    QStringList h5Paths;
    QString transactionError;
    if (!projectXml() ||
        !NodeUtils::validateStagedOutputTransaction(m_outputTransaction, &transactionError) ||
        !NodeUtils::validateStagedH5Datasets(m_outputTransaction,
            QStringList() << QStringLiteral("ps_coordinates") << QStringLiteral("deformation_velocity") <<
                QStringLiteral("temporal_coherence") << QStringLiteral("topographic_residual") <<
                QStringLiteral("deformation_time_series") << QStringLiteral("mask") <<
                QStringLiteral("mask_count_map") << QStringLiteral("temporal_baseline"), &transactionError) ||
        !NodeUtils::workerOutputsMatchManifest(m_preparedOutputPaths, m_generatedOutputPaths, &transactionError) ||
        !NodeUtils::promoteOutputTransaction(m_outputTransaction, h5Paths, &transactionError) ||
        !NodeUtils::prepareOutputTransactionMetadataCommit(
            m_outputTransaction, projectXml(), NodeUtils::getProjectFilePath(_widget), &transactionError)) {
        onError(transactionError.isEmpty() ? QStringLiteral("PS时序输出验收失败") : transactionError);
        return;
    }

    const QString h5Path = h5Paths.value(0);
    NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), dstNode, false, false);
    projectXml()->XMLFile_add_unwrap(dstNode.toStdString().c_str(), "PS_time_series",
        QString("/%1/PS_time_series.h5").arg(dstNode).toStdString().c_str(), 0, 0, "PS_TimeSeries", 0);
    if (!NodeUtils::saveProjectXmlAtomically(projectXml(), NodeUtils::getProjectFilePath(_widget), &transactionError) ||
        !NodeUtils::markOutputTransactionMetadataCommitted(m_outputTransaction, &transactionError)) {
        onError(transactionError);
        return;
    }
    m_outputNodeName = dstNode;
    NodeUtils::removeDataNodeFromProjectTree(NodeUtils::getProjectContext(_widget), dstNode);

    // 注册生成的数据节点到项目树中
    IApplicationInterface* iface = NodeUtils::getProjectContext(_widget);
    if (iface) {
        QStandardItemModel* model = iface->projectModel();
        if (model) {
            QList<QStandardItem*> found = model->findItems(projectName());
            if (!found.isEmpty()) {
                QStandardItem* projectItem = found.first();
                NodeUtils::findOrCreateProjectNode(projectItem, dstNode, "double-1.0");
                iface->refreshProjectTree();
            }
        }
    }

    m_outputData = std::make_shared<ImportedFileData>(QStringList() << h5Path, dstNode);
    m_outputData->setProductDescriptor(ProductDescriptor::fromJson(m_outputTransaction.productDescriptor));
    setOutputData(0, m_outputData);
    
    if (m_resultLabel) {
        m_resultLabel->setText(QStringLiteral("时序反演计算成功！结果保存在: ") + dstNode);
    }

    generateStaticPreviewJpg();

    setState(ExecutionState::Running);
    finishExecution();
    updateLabels();
}

bool PSTimeSeriesNode::validateAndRestoreOutput()
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
        generateStaticPreviewJpg();
        
        setState(ExecutionState::Completed);
        setProgress(100);
        updateLabels();
        if (m_resultLabel) {
            m_resultLabel->setText(QStringLiteral("已自动恢复已有输出: ") + m_outputNodeName);
        }
        Q_EMIT dataUpdated(0);
        return true;
    }
    return false;
}

void PSTimeSeriesNode::generateStaticPreviewJpg()
{
    QString rawPath = projectPath();
    QString dir = rawPath.endsWith(".insar", Qt::CaseInsensitive)
                  ? QFileInfo(rawPath).absolutePath()
                  : rawPath;
    QString outDir = dir + "/" + m_outputNodeName;
    QString h5Path = outDir + "/PS_time_series.h5";
    QString jpgPath = outDir + "/deformation_velocity.jpg";

    if (!QFileInfo::exists(h5Path)) return;

    QFutureWatcher<void>* watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher, jpgPath]() {
        if (QFileInfo::exists(jpgPath)) {
            m_previewData = std::make_shared<ImageInfoData>(jpgPath);
            QMap<QString, QString> previewProvenance;
            previewProvenance.insert(QStringLiteral("producer"), name());
            previewProvenance.insert(QStringLiteral("output_port"), productOutputContract(1).semanticId);
            m_previewData->setProductDescriptor(ProductDescriptor::create(QStringLiteral("preview"),
                productOutputContract(1).schemaId, productOutputContract(1).schemaVersion,
                productOutputContract(1).publishedState, name(), previewProvenance));
            setOutputData(1, m_previewData);
            Q_EMIT dataUpdated(1);
        }
        watcher->deleteLater();
    });

    watcher->setFuture(QtConcurrent::run([h5Path, jpgPath]() {
        NodeUtils::Hdf5Locker locker; // SOP: HDF5 lock protection
        FormatConversion FC;
        Utils util;
        
        cv::Mat velocity_1d, ps_coords, mask;
        int ret = (NodeUtils::readMatFromH5(h5Path, "deformation_velocity", velocity_1d, CV_64F) &&
                   NodeUtils::readMatFromH5(h5Path, "ps_coordinates", ps_coords) &&
                   NodeUtils::readMatFromH5(h5Path, "mask", mask)) ? 0 : -1;

        if (ret == 0 && !velocity_1d.empty() && !ps_coords.empty() && !mask.empty()) {
            int rows = mask.rows;
            int cols = mask.cols;
            cv::Mat velocity_2d = cv::Mat::zeros(rows, cols, CV_64FC1);
            int ps_count = velocity_1d.rows;

            for (int i = 0; i < ps_count; ++i) {
                int r = ps_coords.at<int>(i, 0);
                int c = ps_coords.at<int>(i, 1);
                velocity_2d.at<double>(r, c) = velocity_1d.at<double>(i, 0);
            }

            util.savephase_white(jpgPath.toStdString().c_str(), "jet", velocity_2d, mask);
        }
    }));
}

QStringList PSTimeSeriesNode::previewImagePaths() const
{
    QStringList paths;
    QStringList h5Paths;
    if (!NodeUtils::loadCommittedOutputManifest(projectPath(), m_outputNodeName, h5Paths) || h5Paths.size() != 1) return paths;
    const QString jpgPath = QFileInfo(h5Paths.first()).absolutePath() + "/deformation_velocity.jpg";
    if (QFileInfo::exists(jpgPath)) {
        paths << jpgPath;
    }
    return paths;
}

QJsonObject PSTimeSeriesNode::save() const
{
    QJsonObject root = ExecutableNodeDelegateModel::save();
    root["coherenceThresh"] = m_coherenceThresh;
    root["maxDeformationRate"] = m_maxDeformationRate;
    root["atmosphericWindow"] = m_atmosphericWindow;
    root["outputNodeName"] = m_outputNodeName;
    return root;
}

void PSTimeSeriesNode::load(QJsonObject const& json)
{
    m_coherenceThresh = json["coherenceThresh"].toDouble(0.7);
    m_maxDeformationRate = json["maxDeformationRate"].toDouble(0.1);
    m_atmosphericWindow = json["atmosphericWindow"].toInt(500);
    m_outputNodeName = json["outputNodeName"].toString(QStringLiteral("PS_TimeSeries"));

    // Call base class load AFTER initializing local fields, to ensure validateAndRestoreOutput works during project load
    ExecutableNodeDelegateModel::load(json);
}

QString PSTimeSeriesNode::projectPath() const
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

QString PSTimeSeriesNode::projectName() const
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

XMLFile* PSTimeSeriesNode::projectXml() const
{
    IApplicationInterface* iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectXml() : nullptr;
}

void PSTimeSeriesNode::processAutomatically()
{
    if (prepareToStart()) {
        executeProcessing();
    } else {
        setState(ExecutionState::Idle);
    }
}

} // namespace QtNodes


