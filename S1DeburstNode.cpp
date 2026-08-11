#include "InSARLogManager.h"
#include "S1DeburstNode.h"
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "WorkspaceUI.h"
#include "InterfaceManager.h"
#include "NodeUtils.h"
#include "FormatConversion.h"
#include "icon_source.h"
#include "tinyxml.h"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QJsonObject>
#include <QJsonValue>
#include <QTimer>
#include <QDebug>
#include <QtConcurrent/QtConcurrentRun>

namespace QtNodes {

S1DeburstNode::S1DeburstNode()
    : ExecutableNodeDelegateModel()
    , m_outputNodeNameEdit(nullptr)
    , m_inputData(nullptr)
    , m_outputData(nullptr)
    , m_imageInfoData(nullptr)
    , m_worker(nullptr)
    , m_thread(nullptr)
{
}

S1DeburstNode::~S1DeburstNode()
{
    m_remedyWatcher.cancel();
    m_remedyWatcher.waitForFinished();

    // Clean up worker
    if (m_worker)
    {
        m_worker->deleteLater();
        m_worker = nullptr;
    }

    if (m_thread)
    {
        if (m_thread->isRunning())
        {
            m_thread->requestInterruption();
            m_thread->quit();
            m_thread->wait();
        }
        m_thread->deleteLater();
        m_thread = nullptr;
    }
}

unsigned int S1DeburstNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 1;  // One input port
    else
        return 2;  // Two output ports (Port 0: H5 data, Port 1: ImageInfo preview)
}

NodeDataType S1DeburstNode::dataType(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    if (portType == PortType::In)
        return NodeDataType{"imported_file", "S1 Burst Data"};

    if (portIndex == 0)
        return NodeDataType{"imported_file", "S1 Deburst Data"};
    else
        return NodeDataType{"image_info", "Image Info"};
}

ProductInputContract S1DeburstNode::productInputContract(PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    ProductInputContract contract;
    contract.semanticId = QStringLiteral("s1_deburst.input.burst_sar");
    contract.allowedProductTypes = QStringList() << QStringLiteral("sentinel1_burst_sar");
    contract.requiredProvenanceFields = QStringList()
        << QStringLiteral("producer") << QStringLiteral("output_port");
    return contract;
}

ProductOutputContract S1DeburstNode::productOutputContract(PortIndex portIndex) const
{
    ProductOutputContract contract;
    contract.semanticId = portIndex == 0
        ? QStringLiteral("s1_deburst.output.debursted_complex_sar")
        : QStringLiteral("s1_deburst.output.preview");
    contract.publishedProductTypes = portIndex == 0
        ? QStringList() << QStringLiteral("debursted_complex_sar")
        : QStringList() << QStringLiteral("preview");
    return contract;
}

std::shared_ptr<NodeData> S1DeburstNode::outData(PortIndex port)
{
    if (executionState() != ExecutionState::Completed)
        return nullptr;
    if (port == 0)
        return m_outputData;
    else
        return m_imageInfoData;
}

bool S1DeburstNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    return portType == PortType::Out;
}

QString S1DeburstNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out)
    {
        if (portIndex == 0)
            return tr("成果 *");
        else if (portIndex == 1)
            return tr("预览 ?");
    }
    Q_UNUSED(portIndex);
    return QString();
}

bool S1DeburstNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out && portIndex == 1)
        return true;
    return false;
}

void S1DeburstNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    Q_UNUSED(port);
    m_inputData = std::dynamic_pointer_cast<ImportedFileData>(data);

    // Generate default output name if not set
    if (m_inputData && m_outputNodeNameEdit && m_outputNodeNameEdit->text().isEmpty())
    {
        m_outputNodeNameEdit->setText(generateDefaultOutputName());
    }

    // Delegate to base class to handle execution mode
    ExecutableNodeDelegateModel::setInData(data, port);
}

::QWidget* S1DeburstNode::embeddedWidget()
{
    if (!_widget)
    {
        createWidget();
    }
    return _widget;
}

QJsonObject S1DeburstNode::save() const
{
    QJsonObject modelJson = ExecutableNodeDelegateModel::save();

    QString nodeName = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : m_outputNodeName;
    modelJson["outputNodeName"] = nodeName;

    return modelJson;
}

void S1DeburstNode::load(QJsonObject const &json)
{
    // 先赋值字段，再调用基类 load()（因为基类 load 会调用 validateAndRestoreOutput()）
    QJsonValue v = json["outputNodeName"];
    if (!v.isUndefined())
    {
        m_outputNodeName = v.toString();
    }

    ExecutableNodeDelegateModel::load(json);

    if (m_outputNodeNameEdit)
        m_outputNodeNameEdit->setText(m_outputNodeName);
}

void S1DeburstNode::createWidget()
{
    _widget = new QWidget();
    _widget->setObjectName("NodeEmbeddedWidget");
    _widget->setFixedWidth(300); // 显式设定固定宽度，规避布局尺寸膨胀 Bug
    auto* layout = new QVBoxLayout(_widget);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);



    // 目标节点名 [1:1] - 设定 QLabel 的固定宽度
    auto invalidateNodeData = [this]() {
        if (m_outputData) m_outputData.reset();
        if (m_imageInfoData) m_imageInfoData.reset();
        int outCount = nPorts(PortType::Out);
        for(int i = 0; i < outCount; ++i) setOutputData(i, nullptr);
        invalidateExecution();
    };

    auto* nodeNameLayout = new QHBoxLayout();
    QLabel* nodeNameLabel = new QLabel("目标节点名");
    nodeNameLabel->setFixedWidth(80);
    nodeNameLayout->addWidget(nodeNameLabel);
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setPlaceholderText("自动生成或手动输入");
    m_outputNodeNameEdit->setText(m_outputNodeName);
    connect(m_outputNodeNameEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString text = m_outputNodeNameEdit->text();
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
    nodeNameLayout->addWidget(m_outputNodeNameEdit);
    layout->addLayout(nodeNameLayout);

    // Bottom spacer
    layout->addSpacerItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding));

    // Populate data node and project combo initially if input data is already connected
}

QString S1DeburstNode::generateDefaultOutputName() const
{
    if (m_inputData)
    {
        QString nodeName = m_inputData->nodeName();
        return nodeName + "_Deburst";
    }
    return "burst拼接结果";
}

bool S1DeburstNode::validateInputs() const
{
    if (!m_inputData)
    {
        return false;
    }

    QString nodeName = m_inputData->nodeName();
    if (nodeName.isEmpty())
    {
        return false;
    }

    // Validate project context
    if (!projectModel() || projectPath().isEmpty() || projectName().isEmpty())
    {
        return false;
    }

    return true;
}

void S1DeburstNode::onProgressUpdate(int progress, const QString& message)
{
    Q_UNUSED(message);
    if (isAutomaticExecutionObsolete()) {
        return;
    }
    setProgress(progress);
}

void S1DeburstNode::onProcessingFinished()
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

    QStringList h5Paths;
    QString transactionError;
    if (!projectXml()) {
        onError(QStringLiteral("Project XML context is unavailable for deburst output commit."));
        return;
    }
    if (!NodeUtils::validateStagedOutputTransaction(m_outputTransaction, &transactionError) ||
        !NodeUtils::validateStagedH5Datasets(m_outputTransaction,
                                              QStringList() << QStringLiteral("s_re")
                                                            << QStringLiteral("s_im"),
                                              &transactionError)) {
        onError(transactionError);
        return;
    }
    QStringList manifestPaths;
    const QDir finalDirectory(QDir(m_outputTransaction.projectRoot).absoluteFilePath(m_outputTransaction.nodeName));
    for (const QString& fileName : m_outputTransaction.expectedFileNames) {
        manifestPaths.append(finalDirectory.absoluteFilePath(fileName));
    }
    if (!NodeUtils::workerOutputsMatchManifest(manifestPaths, m_pendingOutputPaths, &transactionError) ||
        !NodeUtils::promoteOutputTransaction(m_outputTransaction, h5Paths, &transactionError)) {
        onError(transactionError);
        return;
    }
    if (!NodeUtils::prepareOutputTransactionMetadataCommit(
            m_outputTransaction, projectXml(), NodeUtils::getProjectFilePath(_widget), &transactionError)) {
        onError(transactionError);
        return;
    }
    NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), m_preparedDstNode, false, false);
    if (!onResultsReceived(m_preparedDstNode, h5Paths, m_pendingOriginNames)) {
        onError(QStringLiteral("Failed to commit deburst project metadata."));
        return;
    }
    if (!NodeUtils::saveProjectXmlAtomically(projectXml(), NodeUtils::getProjectFilePath(_widget), &transactionError)) {
        onError(transactionError);
        return;
    }
    if (!NodeUtils::markOutputTransactionMetadataCommitted(m_outputTransaction, &transactionError)) {
        onError(transactionError);
        return;
    }
    NodeUtils::removeDataNodeFromProjectTree(NodeUtils::getProjectContext(_widget), m_preparedDstNode);
    publishResultsToProjectTree(m_preparedDstNode, h5Paths);
    if (auto* iface = NodeUtils::getProjectContext(_widget)) {
        iface->refreshProjectTree();
    }

    m_outputData = std::make_shared<ImportedFileData>(h5Paths, m_preparedDstNode);
    m_outputData->setProductDescriptor(ProductDescriptor::fromJson(m_outputTransaction.productDescriptor));
    setOutputData(0, m_outputData);

    // 收集生成的预览图
    QStringList jpgPaths;
    for (const QString& h5Path : h5Paths) {
        QFileInfo fi(h5Path);
        QString jpgPath = fi.absolutePath() + "/" + fi.baseName() + ".jpg";
        if (QFile::exists(jpgPath)) {
            jpgPaths.append(jpgPath);
        }
    }

    if (!jpgPaths.isEmpty()) {
        m_imageInfoData = std::make_shared<ImageInfoData>(jpgPaths);
        setOutputData(1, m_imageInfoData);
    } else {
        m_imageInfoData.reset();
        setOutputData(1, nullptr);
    }

    // Clean up thread
    m_worker = nullptr;
    m_thread = nullptr;

    // Update UI
    m_outputNodeNameEdit->setEnabled(true);

    setState(ExecutionState::Running);
    setProgress(100);
    InSARLogManager::LogInfo("S1DeburstNode", "executeProcessing completed.");
    finishExecution();
}

void S1DeburstNode::onError(const QString& error)
{
    InSARLogManager::LogError("S1DeburstNode", "Execution error: " + error);
    // Clean up thread
    m_worker = nullptr;
    m_thread = nullptr;

    if (discardObsoleteAutomaticExecution()) {
        NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("obsolete automatic error"), projectXml());
        return;
    }

    NodeUtils::abandonOutputTransaction(m_outputTransaction, error, projectXml());
    m_outputData.reset();
    m_imageInfoData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);
    m_outputNodeNameEdit->setEnabled(true);
    setState(ExecutionState::Error);
}

void S1DeburstNode::onCancelled()
{
    InSARLogManager::LogInfo("S1DeburstNode", "Deburst cancellation reached a safe boundary.");
    m_worker = nullptr;
    m_thread = nullptr;
    if (discardObsoleteAutomaticExecution()) {
        NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("obsolete automatic cancellation"), projectXml());
        return;
    }

    NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("cancelled"), projectXml());
    m_outputData.reset();
    m_imageInfoData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);
    m_outputNodeNameEdit->setEnabled(true);
    setState(ExecutionState::Stopped);
    Q_EMIT executionStopped();
    Q_EMIT computingFinished();
}

bool S1DeburstNode::onResultsReceived(
    const QString& dstNode,
    const QStringList& deburstH5Paths,
    const QStringList& originNames)
{
    if (isAutomaticExecutionObsolete()) {
        return false;
    }

    // 用全局 XML 句柄 + 原生 TinyXML 写入，绕过外部 DLL 接口（SOP 避坑经验 #9）
    // 注意：此槽通过 Qt 信号队列触发，运行在 UI 线程事件循环中，可安全访问 projectXml()
    XMLFile* xml = projectXml();
    if (!xml)
    {
        InSARLogManager::LogError("S1DeburstNode", "onResultsReceived: projectXml() is null, skipping XML write.");
        return false;
    }

    TiXmlElement* root = nullptr;
    xml->get_root(root);
    if (!root)
    {
        InSARLogManager::LogError("S1DeburstNode", "onResultsReceived: XML root is null, skipping XML write.");
        return false;
    }

    bool xmlModified = false;
    for (int i = 0; i < deburstH5Paths.size(); i++)
    {
        QFileInfo fileinfo(deburstH5Paths.at(i));
        QString relativePath = QString("/%1/%2").arg(dstNode).arg(fileinfo.fileName());

        // 查找或新建 DataNode
        TiXmlElement* dataNodeElem = nullptr;
        for (TiXmlElement* p = root->FirstChildElement(); p != nullptr; p = p->NextSiblingElement())
        {
            const char* nameAttr = p->Attribute("name");
            if (nameAttr && strcmp(p->Value(), "DataNode") == 0 && QString(nameAttr) == dstNode)
            {
                dataNodeElem = p;
                break;
            }
        }

        if (!dataNodeElem)
        {
            // 新建 DataNode，插入到 complex-0.0/complex-1.0 之后的第一个非同级节点前
            dataNodeElem = new TiXmlElement("DataNode");
            dataNodeElem->SetAttribute("name", dstNode.toStdString().c_str());
            dataNodeElem->SetAttribute("data_count", "1");
            dataNodeElem->SetAttribute("data_processing", "deburst");
            dataNodeElem->SetAttribute("rank", "complex-1.0");

            int index = 1;
            TiXmlElement* root_child = root->FirstChildElement();
            if (root_child) root_child = root_child->NextSiblingElement(); // skip project_info

            TiXmlElement* insertBeforeNode = nullptr;
            for (TiXmlElement* p = root_child; p != nullptr; p = p->NextSiblingElement(), index++)
            {
                const char* rankAttr = p->Attribute("rank");
                if (rankAttr && (strcmp(rankAttr, "complex-0.0") == 0 || strcmp(rankAttr, "complex-1.0") == 0))
                    continue;
                else { insertBeforeNode = p; break; }
            }
            dataNodeElem->SetAttribute("index", QString::number(index).toStdString().c_str());

            TiXmlElement* dataElem = new TiXmlElement("Data");
            TiXmlElement* dataNameNode = new TiXmlElement("Data_Name");
            dataNameNode->LinkEndChild(new TiXmlText(fileinfo.baseName().toStdString().c_str()));
            dataElem->LinkEndChild(dataNameNode);
            TiXmlElement* dataRankNode = new TiXmlElement("Data_Rank");
            dataRankNode->LinkEndChild(new TiXmlText("complex-1.0"));
            dataElem->LinkEndChild(dataRankNode);
            TiXmlElement* dataIndexNode = new TiXmlElement("Data_Index");
            dataIndexNode->LinkEndChild(new TiXmlText("1"));
            dataElem->LinkEndChild(dataIndexNode);
            TiXmlElement* dataPathNode = new TiXmlElement("Data_Path");
            dataPathNode->LinkEndChild(new TiXmlText(relativePath.toStdString().c_str()));
            dataElem->LinkEndChild(dataPathNode);
            dataNodeElem->LinkEndChild(dataElem);

            TiXmlElement* paramsElem = new TiXmlElement("Data_Processing_Parameters");
            TiXmlElement* nillElem  = new TiXmlElement("nill");
            nillElem->LinkEndChild(new TiXmlText("0"));
            paramsElem->LinkEndChild(nillElem);
            dataNodeElem->LinkEndChild(paramsElem);

            if (insertBeforeNode)
            {
                root->InsertBeforeChild(insertBeforeNode, *dataNodeElem);
                delete dataNodeElem;
                for (TiXmlElement* p = insertBeforeNode; p != nullptr; p = p->NextSiblingElement())
                {
                    index++;
                    p->SetAttribute("index", QString::number(index).toStdString().c_str());
                }
            }
            else
            {
                root->LinkEndChild(dataNodeElem);
            }
            xmlModified = true;
        }
        else
        {
            // DataNode 已存在，追加 Data 子节点
            const char* countAttr = dataNodeElem->Attribute("data_count");
            int count = countAttr ? QString(countAttr).toInt() : 0;
            count++;
            dataNodeElem->SetAttribute("data_count", QString::number(count).toStdString().c_str());

            TiXmlElement* lastChildNode = dataNodeElem->LastChild() ? dataNodeElem->LastChild()->ToElement() : nullptr;

            TiXmlElement* dataElem = new TiXmlElement("Data");
            TiXmlElement* dataNameNode = new TiXmlElement("Data_Name");
            dataNameNode->LinkEndChild(new TiXmlText(fileinfo.baseName().toStdString().c_str()));
            dataElem->LinkEndChild(dataNameNode);
            TiXmlElement* dataRankNode = new TiXmlElement("Data_Rank");
            dataRankNode->LinkEndChild(new TiXmlText("complex-1.0"));
            dataElem->LinkEndChild(dataRankNode);
            TiXmlElement* dataIndexNode = new TiXmlElement("Data_Index");
            dataIndexNode->LinkEndChild(new TiXmlText(QString::number(count).toStdString().c_str()));
            dataElem->LinkEndChild(dataIndexNode);
            TiXmlElement* dataPathNode = new TiXmlElement("Data_Path");
            dataPathNode->LinkEndChild(new TiXmlText(relativePath.toStdString().c_str()));
            dataElem->LinkEndChild(dataPathNode);

            if (lastChildNode)
            {
                dataNodeElem->InsertBeforeChild(lastChildNode, *dataElem);
                delete dataElem;
            }
            else
            {
                dataNodeElem->LinkEndChild(dataElem);
            }
            xmlModified = true;
        }
    }

    return xmlModified;
}

void S1DeburstNode::publishResultsToProjectTree(const QString& dstNode, const QStringList& deburstH5Paths)
{
    QStandardItemModel* model = projectModel();
    const QList<QStandardItem*> projects = model ? model->findItems(projectName()) : QList<QStandardItem*>();
    if (projects.isEmpty()) {
        InSARLogManager::LogError("S1DeburstNode", "Output metadata committed but project tree root is unavailable.");
        return;
    }
    QStandardItem* deburstNode = NodeUtils::findOrCreateProjectNode(
        projects.first(), dstNode, "complex-1.0", FOLDER_ICON);
    if (!deburstNode) return;
    deburstNode->setToolTip(projectName());
    for (const QString& h5Path : deburstH5Paths) {
        QStandardItem* imageItem = NodeUtils::findOrCreateChildItem(
            deburstNode, QFileInfo(h5Path).baseName(), "complex", h5Path, IMAGEDATA_ICON);
        if (imageItem) {
            deburstNode->setChild(imageItem->row(), 1, new QStandardItem(h5Path));
        }
    }
}

QStandardItemModel* S1DeburstNode::projectModel() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectModel() : nullptr;
}

QString S1DeburstNode::projectPath() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    if (iface) {
        QString fullPath = iface->projectPath();
        if (fullPath.endsWith(".insar", Qt::CaseInsensitive)) {
            return QFileInfo(fullPath).absolutePath();
        }
        return fullPath;
    }
    return QString();
}

QString S1DeburstNode::projectName() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectName() : QString();
}

XMLFile* S1DeburstNode::projectXml() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectXml() : nullptr;
}

void S1DeburstNode::execute()
{
    executeProcessing();
}

void S1DeburstNode::stopExecution()
{
    if (m_thread && m_thread->isRunning())
    {
        m_thread->requestInterruption();
    }
}

void S1DeburstNode::processAutomatically()
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

void S1DeburstNode::setExecutionMode(ExecutionMode mode)
{
    ExecutableNodeDelegateModel::setExecutionMode(mode);
}

bool S1DeburstNode::prepareToStart()
{
    if (!validateInputs())
        return false;

    QString identityError;
    if (!NodeUtils::validateH5Identities(m_inputData->filePaths(),
                                         m_inputData->physicalProductDescriptor(), &identityError)) {
        setStartFailureMessage(identityError);
        setLastErrorMessage(identityError);
        return false;
    }

    QString dstNode = m_outputNodeNameEdit->text().isEmpty()
        ? generateDefaultOutputName()
        : m_outputNodeNameEdit->text();
    m_preparedDstNode = dstNode;

    QString savePath = projectPath();
    QString outputPath = savePath + "/" + dstNode + "/";

    m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;
    QDir outDir(outputPath);
    if (outDir.exists() && outDir.entryList(QDir::Files | QDir::NoDotAndDotDot).count() > 0)
    {
        auto ctx = NodeUtils::getProjectContext(_widget);
        if (_isAutoTriggered) {
            m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
        } else {
            m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(ctx, dstNode, { outputPath }, nullptr);
        }
    }

    return m_preparedOverwriteResult != NodeUtils::OverwriteResult::Cancel;
}

void S1DeburstNode::executeProcessing()
{
    InSARLogManager::LogInfo("S1DeburstNode", "executeProcessing started.");

    // Prepare processing
    QString dstNode = m_preparedDstNode;
    QString savePath = projectPath();
    QString dstProject = projectName();
    const QStringList inputPaths = m_inputData->filePaths();

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting)
    {
        m_outputNodeName = dstNode;
        if (validateAndRestoreOutput())
        {
            setState(ExecutionState::Running);
            setProgress(100);
            finishExecution();
            return;
        }
        else
        {
            setState(ExecutionState::Error);
            return;
        }
    }

    setProgress(0);

    QStringList expectedOutputPaths;
    for (const QString& inputPath : inputPaths) {
        expectedOutputPaths.append(savePath + "/" + dstNode + "/" + QFileInfo(inputPath).baseName() + "_deburst.h5");
    }
    QString transactionError;
    if (!NodeUtils::beginOutputTransaction(savePath, dstNode, expectedOutputPaths,
                                           inputPaths, m_outputTransaction, &transactionError, nullptr,
                                           NodeUtils::getProjectFilePath(_widget))) {
        onError(transactionError);
        return;
    }
    m_outputTransaction.executionRevision = executionRevision();
    QMap<QString, QString> descriptorProvenance;
    descriptorProvenance.insert(QStringLiteral("producer"), name());
    descriptorProvenance.insert(QStringLiteral("output_port"),
                                QStringLiteral("s1_deburst.output.debursted_complex_sar"));
    if (!NodeUtils::setOutputTransactionProductDescriptor(
            m_outputTransaction, ProductDescriptor::create(
                QStringLiteral("debursted_complex_sar"), QStringLiteral("sat-explorer-product"), 1,
                ProductState::Committed, name(), descriptorProvenance), &transactionError)) {
        onError(transactionError);
        return;
    }
    m_pendingOutputPaths.clear();
    m_pendingOriginNames.clear();
    m_outputData.reset();
    m_imageInfoData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);

    // Create thread and worker
    m_thread = new QThread();
    m_worker = new S1DeburstWorker();
    m_worker->moveToThread(m_thread);

    // Connect signals
    connect(this, &S1DeburstNode::startDeburst, m_worker, &S1DeburstWorker::S1_Deburst);
    const QString stagingNode = m_outputTransaction.stagingName;
    connect(m_thread, &QThread::started, [this, savePath, dstProject, stagingNode, inputPaths]() {
        Q_EMIT startDeburst(savePath, dstProject, stagingNode, inputPaths);
    });
    connect(m_worker, &S1DeburstWorker::updateProcess, this, &S1DeburstNode::onProgressUpdate);
    connect(m_worker, &S1DeburstWorker::endProcess, this, &S1DeburstNode::onProcessingFinished);
    connect(m_worker, &S1DeburstWorker::endProcess, m_thread, &QThread::quit);
    connect(m_worker, &S1DeburstWorker::errorProcess, this, &S1DeburstNode::onError);
    connect(m_worker, &S1DeburstWorker::errorProcess, m_thread, &QThread::quit);
    connect(m_worker, &S1DeburstWorker::cancelled, this, &S1DeburstNode::onCancelled);
    connect(m_worker, &S1DeburstWorker::cancelled, m_thread, &QThread::quit);
    connect(m_worker, &S1DeburstWorker::sendResults, this,
            [this](const QString&, const QStringList& paths, const QStringList& names) {
                m_pendingOutputPaths = paths;
                m_pendingOriginNames = names;
            });
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);

    // Start thread
    deferAutomaticCompletion();
    m_thread->start();
    m_outputNodeNameEdit->setEnabled(false);

    // 在下一个事件循环中强行将状态重置为 Running，防止基类 setInData 在 Automatic 模式下将其强行设为 Idle
    QTimer::singleShot(0, this, [this]() {
        if (m_thread && m_thread->isRunning())
        {
            setState(ExecutionState::Running);
        }
    });
}

bool S1DeburstNode::validateAndRestoreOutput()
{
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty())
        return false;

    QString outputPath = projectPath() + "/" + dstNode + "/";
    QStringList h5Paths;
    if (NodeUtils::loadCommittedOutputManifest(projectPath(), dstNode, h5Paths)) {
            ProductDescriptor::Ptr descriptor;
            QString identityError;
            if (!NodeUtils::loadCommittedOutputProductDescriptor(projectPath(), dstNode, descriptor, &identityError) ||
                !validatePublishedDescriptor(productOutputContract(0), descriptor).accepted ||
                !NodeUtils::validateH5Identities(h5Paths, descriptor, &identityError)) {
                return false;
            }
            // 恢复 Port 0 数据
            h5Paths.sort();
            m_outputData = std::make_shared<ImportedFileData>(h5Paths, dstNode);
            m_outputData->setProductDescriptor(descriptor);
            setOutputData(0, m_outputData);
            Q_EMIT dataUpdated(0);

            // 恢复 Port 1 预览数据 & 后台异步补救缺失的 JPG
            QStringList existingJpgPaths;
            QStringList missingH5s;
            QStringList missingJpgs;
            QStringList allJpgPaths;

            for (const QString& h5Path : h5Paths) {
                QFileInfo fi(h5Path);
                QString jpgPath = fi.absolutePath() + "/" + fi.baseName() + ".jpg";
                allJpgPaths.append(jpgPath);

                if (NodeUtils::isJpgPreviewCurrent(h5Path, jpgPath)) {
                    existingJpgPaths.append(jpgPath);
                } else {
                    missingH5s.append(h5Path);
                    missingJpgs.append(jpgPath);
                }
            }

            if (!existingJpgPaths.isEmpty()) {
                m_imageInfoData = std::make_shared<ImageInfoData>(existingJpgPaths);
                setOutputData(1, m_imageInfoData);
                Q_EMIT dataUpdated(1);
            } else {
                m_imageInfoData.reset();
                setOutputData(1, nullptr);
                Q_EMIT dataUpdated(1);
            }

            // 异步补录 JPG 预览
            if (!missingH5s.isEmpty()) {
                startPreviewGeneration(missingH5s, missingJpgs, h5Paths, allJpgPaths);
            }

            // 恢复左侧树标准项目模型 (Standard Item Model Tree View)
            QStandardItemModel* projModelPtr = projectModel();
            if (projModelPtr) {
                QList<QStandardItem*> foundProjects = projModelPtr->findItems(projectName());
                if (!foundProjects.isEmpty()) {
                    QStandardItem* projectItem = foundProjects.first();

                    // 1. 查找或建立 deburst 根节点
                    QStandardItem* deburstItem = nullptr;
                    for (int i = 0; i < projectItem->rowCount(); i++) {
                        if (projectItem->child(i, 0)->text() == dstNode) {
                            deburstItem = projectItem->child(i, 0);
                            break;
                        }
                    }

                    if (!deburstItem) {
                        deburstItem = new QStandardItem(dstNode);
                        deburstItem->setToolTip(projectName());
                        int insert = 0;
                        for (; insert < projectItem->rowCount(); insert++) {
                            if (projectItem->child(insert, 1)->text().compare("complex-0.0") == 0 ||
                                projectItem->child(insert, 1)->text().compare("complex-1.0") == 0)
                                continue;
                            else
                                break;
                        }
                        deburstItem->setIcon(QIcon(FOLDER_ICON));
                        projectItem->insertRow(insert, deburstItem);
                        QStandardItem* deburstRank = new QStandardItem("complex-1.0");
                        projectItem->setChild(insert, 1, deburstRank);
                    }

                    // 2. 补全下属图像节点
                    for (const QString& h5Path : h5Paths) {
                        QFileInfo fileinfo(h5Path);
                        QString deburst_name = fileinfo.baseName();

                        QStandardItem* item_img = nullptr;
                        for (int j = 0; j < deburstItem->rowCount(); j++) {
                            if (deburstItem->child(j, 0)->text() == deburst_name) {
                                item_img = deburstItem->child(j, 0);
                                break;
                            }
                        }

                        if (!item_img) {
                            QStandardItem* deburst_images_name = new QStandardItem(deburst_name);
                            deburst_images_name->setToolTip("complex");
                            QStandardItem* deburst_images_path = new QStandardItem(fileinfo.absoluteFilePath());
                            deburst_images_name->setIcon(QIcon(IMAGEDATA_ICON));
                            deburstItem->appendRow(deburst_images_name);
                            deburstItem->setChild(deburstItem->rowCount() - 1, 1, deburst_images_path);
                        } else {
                            deburstItem->setChild(item_img->row(), 1, new QStandardItem(fileinfo.absoluteFilePath()));
                        }
                    }
                }
            }

            // XML 工程文件自愈修复 (EXE端原生 TinyXML 实现)
            XMLFile* xml = projectXml();
            if (xml) {
                bool xmlModified = false;
                TiXmlElement* root = nullptr;
                xml->get_root(root);
                if (root) {
                    bool dataNodeExists = false;
                    for (TiXmlElement* p = root->FirstChildElement(); p != nullptr; p = p->NextSiblingElement()) {
                        const char* nameAttr = p->Attribute("name");
                        if (nameAttr && strcmp(p->Value(), "DataNode") == 0 && QString(nameAttr) == dstNode) {
                            dataNodeExists = true;
                            break;
                        }
                    }

                    if (!dataNodeExists) {
                        for (const QString& h5Path : h5Paths) {
                            QFileInfo fileinfo(h5Path);
                            QString relativePath = QString("/%1/%2").arg(dstNode).arg(fileinfo.fileName());

                            TiXmlElement* dataNodeElem = nullptr;
                            for (TiXmlElement* p = root->FirstChildElement(); p != nullptr; p = p->NextSiblingElement()) {
                                const char* nameAttr = p->Attribute("name");
                                if (nameAttr && strcmp(p->Value(), "DataNode") == 0 && QString(nameAttr) == dstNode) {
                                    dataNodeElem = p;
                                    break;
                                }
                            }

                            if (!dataNodeElem) {
                                dataNodeElem = new TiXmlElement("DataNode");
                                dataNodeElem->SetAttribute("name", dstNode.toStdString().c_str());
                                dataNodeElem->SetAttribute("data_count", "1");
                                dataNodeElem->SetAttribute("data_processing", "deburst");
                                dataNodeElem->SetAttribute("rank", "complex-1.0");

                                int index = 1;
                                TiXmlElement* root_child = root->FirstChildElement();
                                if (root_child) {
                                    root_child = root_child->NextSiblingElement(); // skip project_info
                                }

                                TiXmlElement* insertBeforeNode = nullptr;
                                for (TiXmlElement* p = root_child; p != nullptr; p = p->NextSiblingElement(), index++) {
                                    const char* rankAttr = p->Attribute("rank");
                                    if (rankAttr && (strcmp(rankAttr, "complex-0.0") == 0 ||
                                                     strcmp(rankAttr, "complex-1.0") == 0)) {
                                        continue;
                                    } else {
                                        insertBeforeNode = p;
                                        break;
                                    }
                                }
                                dataNodeElem->SetAttribute("index", QString::number(index).toStdString().c_str());

                                TiXmlElement* dataElem = new TiXmlElement("Data");

                                TiXmlElement* dataNameNode = new TiXmlElement("Data_Name");
                                dataNameNode->LinkEndChild(new TiXmlText(fileinfo.baseName().toStdString().c_str()));
                                dataElem->LinkEndChild(dataNameNode);

                                TiXmlElement* dataRankNode = new TiXmlElement("Data_Rank");
                                dataRankNode->LinkEndChild(new TiXmlText("complex-1.0"));
                                dataElem->LinkEndChild(dataRankNode);

                                TiXmlElement* dataIndexNode = new TiXmlElement("Data_Index");
                                dataIndexNode->LinkEndChild(new TiXmlText("1"));
                                dataElem->LinkEndChild(dataIndexNode);

                                TiXmlElement* dataPathNode = new TiXmlElement("Data_Path");
                                dataPathNode->LinkEndChild(new TiXmlText(relativePath.toStdString().c_str()));
                                dataElem->LinkEndChild(dataPathNode);

                                dataNodeElem->LinkEndChild(dataElem);

                                // Deburst details params
                                TiXmlElement* paramsElem = new TiXmlElement("Data_Processing_Parameters");
                                TiXmlElement* nillElem = new TiXmlElement("nill");
                                nillElem->LinkEndChild(new TiXmlText("0"));
                                paramsElem->LinkEndChild(nillElem);
                                dataNodeElem->LinkEndChild(paramsElem);

                                if (insertBeforeNode) {
                                    root->InsertBeforeChild(insertBeforeNode, *dataNodeElem);
                                    delete dataNodeElem;
                                    for (TiXmlElement* p = insertBeforeNode; p != nullptr; p = p->NextSiblingElement()) {
                                        index++;
                                        p->SetAttribute("index", QString::number(index).toStdString().c_str());
                                    }
                                } else {
                                    root->LinkEndChild(dataNodeElem);
                                }
                            } else {
                                const char* countAttr = dataNodeElem->Attribute("data_count");
                                int count = countAttr ? QString(countAttr).toInt() : 0;
                                count++;
                                dataNodeElem->SetAttribute("data_count", QString::number(count).toStdString().c_str());

                                TiXmlElement* lastChildNode = dataNodeElem->LastChild() ? dataNodeElem->LastChild()->ToElement() : nullptr;

                                TiXmlElement* dataElem = new TiXmlElement("Data");

                                TiXmlElement* dataNameNode = new TiXmlElement("Data_Name");
                                dataNameNode->LinkEndChild(new TiXmlText(fileinfo.baseName().toStdString().c_str()));
                                dataElem->LinkEndChild(dataNameNode);

                                TiXmlElement* dataRankNode = new TiXmlElement("Data_Rank");
                                dataRankNode->LinkEndChild(new TiXmlText("complex-1.0"));
                                dataElem->LinkEndChild(dataRankNode);

                                TiXmlElement* dataIndexNode = new TiXmlElement("Data_Index");
                                dataIndexNode->LinkEndChild(new TiXmlText(QString::number(count).toStdString().c_str()));
                                dataElem->LinkEndChild(dataIndexNode);

                                TiXmlElement* dataPathNode = new TiXmlElement("Data_Path");
                                dataPathNode->LinkEndChild(new TiXmlText(relativePath.toStdString().c_str()));
                                dataElem->LinkEndChild(dataPathNode);

                                if (lastChildNode) {
                                    dataNodeElem->InsertBeforeChild(lastChildNode, *dataElem);
                                    delete dataElem;
                                } else {
                                    dataNodeElem->LinkEndChild(dataElem);
                                }
                            }
                            xmlModified = true;
                        }
                    }
                }
                if (xmlModified) {
                    QString xmlPath = projectPath() + "/" + projectName();
                    xml->XMLFile_save(xmlPath.toStdString().c_str());
                }
            }

            // 刷新左侧树视图
            auto iface = NodeUtils::getProjectContext(_widget);
            if (iface) {
                iface->refreshProjectTree();
            }

            return true;
        }

    return false;
}

void S1DeburstNode::startPreviewGeneration(const QStringList& h5Paths,
                                           const QStringList& generatedJpgPaths,
                                           const QStringList& resultH5Paths,
                                           const QStringList& resultJpgPaths)
{
    if (m_remedyWatcher.isRunning()) {
        m_remedyWatcher.cancel();
        m_remedyWatcher.disconnect(this);
        connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this,
                [this, h5Paths, generatedJpgPaths, resultH5Paths, resultJpgPaths]() {
            m_remedyWatcher.disconnect(this);
            startPreviewGeneration(h5Paths, generatedJpgPaths, resultH5Paths, resultJpgPaths);
        });
        return;
    }

    m_remedyWatcher.disconnect(this);
    connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this, [this, resultH5Paths, resultJpgPaths]() {
        InSARLogManager::LogInfo("S1DeburstNode", "Remedy preview generation finished. Updating Port 1.");
        QStringList currentJpgPaths;
        for (int i = 0; i < resultH5Paths.size() && i < resultJpgPaths.size(); ++i) {
            if (NodeUtils::isJpgPreviewCurrent(resultH5Paths[i], resultJpgPaths[i])) currentJpgPaths.append(resultJpgPaths[i]);
        }
        m_imageInfoData = std::make_shared<ImageInfoData>(currentJpgPaths);
        setOutputData(1, m_imageInfoData);
        Q_EMIT dataUpdated(1);
    });
    m_remedyWatcher.setFuture(QtConcurrent::run([h5Paths, generatedJpgPaths]() {
        for (int i = 0; i < h5Paths.size(); ++i) {
            NodeUtils::generateJpgPreviewFromH5(h5Paths[i], generatedJpgPaths[i], "complex");
        }
    }));
}

QStringList S1DeburstNode::previewImagePaths() const
{
    QStringList existingPaths;
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty())
        return existingPaths;

    QStringList h5Paths;
    if (NodeUtils::loadCommittedOutputManifest(projectPath(), dstNode, h5Paths)) {
        for (const QString& h5Path : h5Paths) {
            QFileInfo fi(h5Path);
            QString jpgPath = fi.absolutePath() + "/" + fi.baseName() + ".jpg";
            if (QFile::exists(jpgPath)) {
                existingPaths << jpgPath;
            }
        }
    }
    return existingPaths;
}

} // namespace QtNodes
