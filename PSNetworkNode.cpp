#include "PSNetworkNode.h"
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

PSNetworkNode::PSNetworkNode()
    : _widget(nullptr)
    , m_maxEdgeLength(1000.0)
    , m_refRow(-1)
    , m_refCol(-1)
    , m_outputNodeName("PS_Network")
    , m_worker(nullptr)
    , m_thread(nullptr)
{
    setState(ExecutionState::Idle);
}

PSNetworkNode::~PSNetworkNode()
{
    if (m_worker) m_worker->StopProcess();
    if (m_thread && m_thread->isRunning()) {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
    }
}

ProductInputContract PSNetworkNode::productInputContract(PortIndex portIndex) const
{
    ProductInputContract contract;
    if (portIndex == 0) {
        contract.semanticId = QStringLiteral("ps_network.input.ps_candidates");
        contract.allowedProductTypes = QStringList() << QStringLiteral("ps_candidates");
    } else {
        contract.semanticId = QStringLiteral("ps_network.input.coregistered_complex_sar");
        contract.allowedProductTypes = QStringList() << QStringLiteral("coregistered_complex_sar");
    }
    contract.requiredProvenanceFields = QStringList()
        << QStringLiteral("producer") << QStringLiteral("output_port");
    return contract;
}

ProductOutputContract PSNetworkNode::productOutputContract(PortIndex portIndex) const
{
    ProductOutputContract contract;
    contract.semanticId = portIndex == 0
        ? QStringLiteral("ps_network.output.ps_network")
        : QStringLiteral("ps_network.output.preview");
    contract.publishedProductTypes = portIndex == 0
        ? QStringList() << QStringLiteral("ps_network")
        : QStringList() << QStringLiteral("preview");
    return contract;
}

unsigned int PSNetworkNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 2;
    else
        return 2;
}

NodeDataType PSNetworkNode::dataType(PortType portType, PortIndex portIndex) const
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

bool PSNetworkNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return true;
}

QString PSNetworkNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) {
        if (portIndex == 0)
            return QStringLiteral("候选点数据");
        else
            return QStringLiteral("配准后影像(SLC)");
    } else {
        if (portIndex == 0)
            return tr("网络成果 *");
        else
            return tr("网络拓扑预览 ?");
    }
}

bool PSNetworkNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out && portIndex == 1)
        return true;
    return false;
}

std::shared_ptr<NodeData> PSNetworkNode::outData(PortIndex port)
{
    if (executionState() != ExecutionState::Completed)
        return nullptr;
    if (port == 0)
        return m_outputData;
    else
        return m_previewData;
}

void PSNetworkNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    if (port == 0) {
        m_candidatesData = std::dynamic_pointer_cast<ImportedFileData>(data);
    } else if (port == 1) {
        m_slcData = std::dynamic_pointer_cast<ImportedFileData>(data);
    }

    if (!m_candidatesData || !m_slcData) {
        m_outputData.reset();
        m_previewData.reset();
        setOutputData(0, nullptr);
        setOutputData(1, nullptr);
    }

    updateLabels();
    ExecutableNodeDelegateModel::setInData(data, port);
}

::QWidget* PSNetworkNode::embeddedWidget()
{
    if (!_widget)
    {
        createWidget();
    }
    return _widget;
}

void PSNetworkNode::setExecutionMode(ExecutionMode mode)
{
    ExecutableNodeDelegateModel::setExecutionMode(mode);
}

void PSNetworkNode::createWidget()
{
    _widget = new QWidget();
    _widget->setObjectName("NodeEmbeddedWidget");
    _widget->setFixedWidth(300); // SOP: Lock width

    QVBoxLayout* mainLayout = new QVBoxLayout(_widget);
    mainLayout->setContentsMargins(5, 5, 5, 5);
    mainLayout->setSpacing(4);

    // Row 1: Candidates input display
    QHBoxLayout* inputLayout1 = new QHBoxLayout();
    QLabel* inputTitleLabel1 = new QLabel(QStringLiteral("候选点数据:"));
    inputTitleLabel1->setFixedWidth(80); // SOP: Fixed label width
    m_candidatesNodeLabel = new QLabel(QStringLiteral("等待输入"));
    m_candidatesNodeLabel->setStyleSheet("color: gray;");
    inputLayout1->addWidget(inputTitleLabel1);
    inputLayout1->addWidget(m_candidatesNodeLabel);
    mainLayout->addLayout(inputLayout1);

    // Row 2: SLC input display
    QHBoxLayout* inputLayout2 = new QHBoxLayout();
    QLabel* inputTitleLabel2 = new QLabel(QStringLiteral("输入影像集:"));
    inputTitleLabel2->setFixedWidth(80);
    m_slcNodeLabel = new QLabel(QStringLiteral("等待输入"));
    m_slcNodeLabel->setStyleSheet("color: gray;");
    inputLayout2->addWidget(inputTitleLabel2);
    inputLayout2->addWidget(m_slcNodeLabel);
    mainLayout->addLayout(inputLayout2);

    // Parameters Layout helper
    auto addParamRow = [&](const QString& labelText, QWidget* editWidget) {
        QHBoxLayout* layout = new QHBoxLayout();
        QLabel* label = new QLabel(labelText);
        label->setFixedWidth(80); // SOP: Fixed label width
        layout->addWidget(label);
        layout->addWidget(editWidget);
        mainLayout->addLayout(layout);
    };

    m_maxEdgeLengthEdit = new QLineEdit(QString::number(m_maxEdgeLength));
    addParamRow(QStringLiteral("最大连接距离:"), m_maxEdgeLengthEdit);

    m_refRowEdit = new QLineEdit(QString::number(m_refRow));
    addParamRow(QStringLiteral("参考点行:"), m_refRowEdit);

    m_refColEdit = new QLineEdit(QString::number(m_refCol));
    addParamRow(QStringLiteral("参考点列:"), m_refColEdit);

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
    connect(m_maxEdgeLengthEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_maxEdgeLength = text.toDouble(); invalidateExecution();
    });
    connect(m_refRowEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_refRow = text.toInt(); invalidateExecution();
    });
    connect(m_refColEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_refCol = text.toInt(); invalidateExecution();
    });
    connect(m_outputNodeNameEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_outputNodeName = text; invalidateExecution();
    });

    updateLabels(); // Widget initial label update (SOP rule 12)
}

void PSNetworkNode::updateLabels()
{
    if (m_candidatesNodeLabel) {
        if (m_candidatesData) {
            m_candidatesNodeLabel->setText(m_candidatesData->nodeName());
            m_candidatesNodeLabel->setStyleSheet("color: #10B981; font-weight: bold;");
        } else {
            m_candidatesNodeLabel->setText(QStringLiteral("等待输入"));
            m_candidatesNodeLabel->setStyleSheet("color: gray;");
        }
    }
    if (m_slcNodeLabel) {
        if (m_slcData) {
            m_slcNodeLabel->setText(m_slcData->nodeName());
            m_slcNodeLabel->setStyleSheet("color: #10B981; font-weight: bold;");
        } else {
            m_slcNodeLabel->setText(QStringLiteral("等待输入"));
            m_slcNodeLabel->setStyleSheet("color: gray;");
        }
    }
    updateWidgetSize();
}

void PSNetworkNode::updateWidgetSize()
{
    if (_widget) {
        _widget->setFixedWidth(300);
        _widget->adjustSize();
        Q_EMIT embeddedWidgetSizeUpdated();
    }
}

bool PSNetworkNode::validateInputs() const
{
    if (!m_candidatesData || m_candidatesData->filePaths().isEmpty())
        return false;
    if (!m_slcData || m_slcData->filePaths().size() < 2)
        return false;
    if (m_outputNodeName.isEmpty())
        return false;
    if (m_maxEdgeLength <= 0.0)
        return false;
    return true;
}

bool PSNetworkNode::prepareToStart()
{
    if (!validateInputs()) {
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;
        setState(ExecutionState::Error);
        return false;
    }

    const ProductValidationResult candidatesValidation = validateBoundDescriptor(
        productInputContract(0), m_candidatesData->productDescriptor());
    const ProductValidationResult slcValidation = validateBoundDescriptor(
        productInputContract(1), m_slcData->productDescriptor());
    if (!candidatesValidation.accepted || !slcValidation.accepted) {
        setLastErrorMessage(!candidatesValidation.accepted
            ? candidatesValidation.reason : slcValidation.reason);
        setState(ExecutionState::Error);
        return false;
    }
    QString identityError;
    if (!NodeUtils::validateH5Identities(m_candidatesData->filePaths(),
            m_candidatesData->physicalProductDescriptor(), &identityError) ||
        !NodeUtils::validateH5Identities(m_slcData->filePaths(),
            m_slcData->physicalProductDescriptor(), &identityError)) {
        setLastErrorMessage(identityError);
        setState(ExecutionState::Error);
        return false;
    }

    m_preparedDstNode = m_outputNodeName;
    m_preparedOutputPaths = QStringList() <<
        QDir(projectPath()).absoluteFilePath(m_preparedDstNode + "/PS_network.h5");
    m_preparedInputPaths = QStringList();
    if (m_candidatesData) m_preparedInputPaths.append(m_candidatesData->filePath());
    if (m_slcData) m_preparedInputPaths.append(m_slcData->filePaths());

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

void PSNetworkNode::execute()
{
    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting) {
        if (!validateAndRestoreOutput()) {
            onError(QStringLiteral("Existing PS network output does not satisfy its semantic identity contract."));
        }
        return;
    }

    executeProcessing();
}

void PSNetworkNode::executeProcessing()
{
    InSARLogManager::LogInfo("PSNetworkNode", "executeProcessing started.");
    stopExecution();

    QString projPath = projectPath();
    QString projName = projectName();
    const QString candH5 = m_preparedInputPaths.value(0);
    QStringList slcList = m_preparedInputPaths;
    if (!slcList.isEmpty()) {
        slcList.removeFirst();
    }
    QString transactionError;
    if (!NodeUtils::beginOutputTransaction(projPath, m_preparedDstNode, m_preparedOutputPaths,
                                           m_preparedInputPaths, m_outputTransaction, &transactionError, nullptr,
                                           NodeUtils::getProjectFilePath(_widget))) {
        onError(transactionError);
        return;
    }
    m_outputTransaction.executionRevision = executionRevision();
    QMap<QString, QString> provenance;
    provenance.insert(QStringLiteral("producer"), name());
    provenance.insert(QStringLiteral("output_port"), productOutputContract(0).semanticId);
    if (!NodeUtils::setOutputTransactionProductDescriptor(m_outputTransaction,
            ProductDescriptor::create(QStringLiteral("ps_network"),
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
    m_worker = new PSNetworkWorker();
    m_worker->moveToThread(m_thread);
    const QString stagingNode = m_outputTransaction.stagingName;
    connect(m_thread, &QThread::started, m_worker, [this, projPath, projName, stagingNode, candH5, slcList]() {
        m_worker->build_network(
            m_maxEdgeLength,
            m_refRow,
            m_refCol,
            projPath,
            projName,
            stagingNode,
            candH5,
            slcList,
            true
        );
    });

    connect(m_worker, &PSNetworkWorker::updateProcess, this, &PSNetworkNode::onProgressUpdate);
    connect(m_worker, &PSNetworkWorker::outputsGenerated, this, &PSNetworkNode::onOutputsGenerated);
    connect(m_worker, &PSNetworkWorker::endProcess, this, &PSNetworkNode::onProcessingFinished);
    connect(m_worker, &PSNetworkWorker::endProcess, m_thread, &QThread::quit);
    connect(m_worker, &PSNetworkWorker::errorProcess, this, &PSNetworkNode::onError);
    connect(m_worker, &PSNetworkWorker::errorProcess, m_thread, &QThread::quit);
    connect(m_worker, &PSNetworkWorker::cancelled, this, &PSNetworkNode::onCancelled);
    connect(m_worker, &PSNetworkWorker::cancelled, m_thread, &QThread::quit);

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

void PSNetworkNode::stopExecution()
{
    if (m_worker) {
        m_worker->StopProcess();
    }
}

void PSNetworkNode::onProgressUpdate(int progress, const QString& message)
{
    if (isAutomaticExecutionObsolete()) {
        return;
    }

    setProgress(progress);
    if (m_resultLabel) {
        m_resultLabel->setText(QStringLiteral("正在构建 (%1%): %2").arg(progress).arg(message));
    }
}

void PSNetworkNode::onOutputsGenerated(const QStringList& outputPaths)
{
    m_generatedOutputPaths = outputPaths;
}

void PSNetworkNode::onError(const QString& error)
{
    NodeUtils::abandonOutputTransaction(m_outputTransaction, error, projectXml());
    m_worker = nullptr;
    m_thread = nullptr;
    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    InSARLogManager::LogError("PSNetworkNode", "Error in network construction: " + error);
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

void PSNetworkNode::onCancelled()
{
    InSARLogManager::LogInfo("PSNetworkNode", "PS network cancellation cleanup completed.");
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

void PSNetworkNode::onProcessingFinished()
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

    InSARLogManager::LogInfo("PSNetworkNode", "executeProcessing completed successfully.");
    
    const QString dstNode = m_preparedDstNode;
    QStringList h5Paths;
    QString transactionError;
    if (!projectXml() ||
        !NodeUtils::validateStagedOutputTransaction(m_outputTransaction, &transactionError) ||
        !NodeUtils::validateStagedH5Datasets(m_outputTransaction,
            QStringList() << QStringLiteral("ps_coordinates") << QStringLiteral("edges") <<
                QStringLiteral("edge_phase_diff") << QStringLiteral("temporal_baseline") <<
                QStringLiteral("spatial_baseline") << QStringLiteral("formation_matrix"), &transactionError) ||
        !NodeUtils::workerOutputsMatchManifest(m_preparedOutputPaths, m_generatedOutputPaths, &transactionError) ||
        !NodeUtils::promoteOutputTransaction(m_outputTransaction, h5Paths, &transactionError) ||
        !NodeUtils::prepareOutputTransactionMetadataCommit(
            m_outputTransaction, projectXml(), NodeUtils::getProjectFilePath(_widget), &transactionError)) {
        onError(transactionError.isEmpty() ? QStringLiteral("PS网络输出验收失败") : transactionError);
        return;
    }

    const QString h5Path = h5Paths.value(0);
    NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), dstNode, false, false);
    projectXml()->XMLFile_add_unwrap(dstNode.toStdString().c_str(), "PS_network",
        QString("/%1/PS_network.h5").arg(dstNode).toStdString().c_str(), 0, 0, "PS_Network", 0);
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
                NodeUtils::findOrCreateProjectNode(projectItem, dstNode, "mask-1.0");
                iface->refreshProjectTree();
            }
        }
    }

    m_outputData = std::make_shared<ImportedFileData>(QStringList() << h5Path, dstNode);
    m_outputData->setProductDescriptor(ProductDescriptor::fromJson(m_outputTransaction.productDescriptor));
    setOutputData(0, m_outputData);
    
    if (m_resultLabel) {
        m_resultLabel->setText(QStringLiteral("三角网络计算成功！结果保存在: ") + dstNode);
    }

    generateStaticPreviewJpg();

    setState(ExecutionState::Running);
    finishExecution();
    updateLabels();
}

bool PSNetworkNode::validateAndRestoreOutput()
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

void PSNetworkNode::generateStaticPreviewJpg()
{
    QString rawPath = projectPath();
    QString dir = rawPath.endsWith(".insar", Qt::CaseInsensitive)
                  ? QFileInfo(rawPath).absolutePath()
                  : rawPath;
    QString outDir = dir + "/" + m_outputNodeName;
    QString h5Path = outDir + "/PS_network.h5";
    QString candidatesH5 = m_candidatesData ? m_candidatesData->filePath() : QString();
    QString jpgPath = outDir + "/network_preview.jpg";

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

    watcher->setFuture(QtConcurrent::run([h5Path, candidatesH5, jpgPath]() {
        NodeUtils::Hdf5Locker locker; // SOP: HDF5 lock protection
        FormatConversion FC;
        
        int rows = 0, cols = 0, ps_count = 0, edge_count = 0, ref_index = 0;
        NodeUtils::readScalarFromH5(h5Path, "rows", rows);
        NodeUtils::readScalarFromH5(h5Path, "cols", cols);
        NodeUtils::readScalarFromH5(h5Path, "ps_count", ps_count);
        NodeUtils::readScalarFromH5(h5Path, "edge_count", edge_count);
        NodeUtils::readScalarFromH5(h5Path, "ref_index", ref_index);

        if (rows <= 0 || cols <= 0) return;

        cv::Mat background = cv::Mat::zeros(rows, cols, CV_8UC3);

        // 如果能读取离差分布作为背景，则加载它
        if (QFileInfo::exists(candidatesH5)) {
            cv::Mat dispersion, mask;
            int ret1 = (NodeUtils::readMatFromH5(candidatesH5, "amplitude_dispersion", dispersion) &&
                        NodeUtils::readMatFromH5(candidatesH5, "ps_mask", mask)) ? 0 : -1;
            if (ret1 == 0 && !dispersion.empty()) {
                double minVal, maxVal;
                cv::minMaxLoc(dispersion, &minVal, &maxVal, NULL, NULL, mask);
                cv::Mat gray;
                dispersion.convertTo(gray, CV_8UC1, 255.0 / (maxVal - minVal + 1e-5), -minVal * 255.0 / (maxVal - minVal + 1e-5));
                cv::cvtColor(gray, background, cv::COLOR_GRAY2BGR);
            }
        }

        cv::Mat ps_coords, edge_nodes;
        int ret = (NodeUtils::readMatFromH5(h5Path, "ps_coordinates", ps_coords) &&
                   NodeUtils::readMatFromH5(h5Path, "edges", edge_nodes)) ? 0 : -1;

        if (ret == 0 && !ps_coords.empty() && !edge_nodes.empty()) {
            // 绘制网格边 (蓝色)
            for (int i = 0; i < edge_count; ++i) {
                int end1 = edge_nodes.at<int>(i, 0);
                int end2 = edge_nodes.at<int>(i, 1);
                cv::Point p1(ps_coords.at<int>(end1, 1), ps_coords.at<int>(end1, 0));
                cv::Point p2(ps_coords.at<int>(end2, 1), ps_coords.at<int>(end2, 0));
                cv::line(background, p1, p2, cv::Scalar(255, 120, 0), 1, cv::LINE_AA);
            }

            // 绘制 PS 点 (红色)
            for (int i = 0; i < ps_count; ++i) {
                cv::Point pt(ps_coords.at<int>(i, 1), ps_coords.at<int>(i, 0));
                cv::circle(background, pt, 2, cv::Scalar(0, 0, 255), -1);
            }

            // 绘制参考点 (绿色大圆点)
            if (ref_index >= 0 && ref_index < ps_count) {
                cv::Point pt(ps_coords.at<int>(ref_index, 1), ps_coords.at<int>(ref_index, 0));
                cv::circle(background, pt, 6, cv::Scalar(0, 255, 0), -1);
                cv::circle(background, pt, 7, cv::Scalar(0, 0, 0), 1);
            }

            cv::imwrite(jpgPath.toStdString(), background);
        }
    }));
}

QStringList PSNetworkNode::previewImagePaths() const
{
    QStringList paths;
    QStringList h5Paths;
    if (!NodeUtils::loadCommittedOutputManifest(projectPath(), m_outputNodeName, h5Paths) || h5Paths.size() != 1) return paths;
    const QString jpgPath = QFileInfo(h5Paths.first()).absolutePath() + "/network_preview.jpg";
    if (QFileInfo::exists(jpgPath)) {
        paths << jpgPath;
    }
    return paths;
}

QJsonObject PSNetworkNode::save() const
{
    QJsonObject root = ExecutableNodeDelegateModel::save();
    root["maxEdgeLength"] = m_maxEdgeLength;
    root["refRow"] = m_refRow;
    root["refCol"] = m_refCol;
    root["outputNodeName"] = m_outputNodeName;
    return root;
}

void PSNetworkNode::load(QJsonObject const& json)
{
    m_maxEdgeLength = json["maxEdgeLength"].toDouble(1000.0);
    m_refRow = json["refRow"].toInt(-1);
    m_refCol = json["refCol"].toInt(-1);
    m_outputNodeName = json["outputNodeName"].toString(QStringLiteral("PS_Network"));

    // Call base class load AFTER initializing local fields, to ensure validateAndRestoreOutput works during project load
    ExecutableNodeDelegateModel::load(json);
}

QString PSNetworkNode::projectPath() const
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

QString PSNetworkNode::projectName() const
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

XMLFile* PSNetworkNode::projectXml() const
{
    IApplicationInterface* iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectXml() : nullptr;
}

void PSNetworkNode::processAutomatically()
{
    if (prepareToStart()) {
        executeProcessing();
    } else {
        setState(ExecutionState::Idle);
    }
}

} // namespace QtNodes


