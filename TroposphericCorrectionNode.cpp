#include "TroposphericCorrectionNode.h"
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "InterfaceManager.h"
#include "WorkspaceUI.h"
#include "NodeUtils.h"
#include "Utils.h"
#include "icon_source.h"
#include "InSARLogManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QDir>
#include <QApplication>
#include <QStandardItemModel>
#include <QDebug>
#include <QTimer>
#include <QFileDialog>
#include <QtConcurrent/QtConcurrent>

namespace QtNodes {

TroposphericCorrectionNode::TroposphericCorrectionNode()
    : ExecutableNodeDelegateModel()
    , _widget(nullptr)
    , m_era5DirEdit(nullptr)
    , m_browseBtn(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_outputNodeName("")
    , m_era5Dir("")
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
    setExecutionMode(ExecutionMode::Automatic);
}

TroposphericCorrectionNode::~TroposphericCorrectionNode() { stopExecution(); }

unsigned int TroposphericCorrectionNode::nPorts(PortType portType) const
{
    if (portType == PortType::In) return 1;
    else return 2;
}

NodeDataType TroposphericCorrectionNode::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) return NodeDataType{"imported_file", "Imported File"};
    else {
        if (portIndex == 0) return NodeDataType{"imported_file", "Imported File"};
        else return NodeDataType{"image_info", "Image Info"};
    }
}

bool TroposphericCorrectionNode::portCaptionVisible(PortType, PortIndex) const { return true; }

QString TroposphericCorrectionNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) {
        return QStringLiteral("解缠相位");
    } else {
        if (portIndex == 0) return QStringLiteral("成果 *");
        else return QStringLiteral("预览 ?");
    }
    return QString();
}

bool TroposphericCorrectionNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out && portIndex == 1) return true;
    return false;
}

void TroposphericCorrectionNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    if (port != 0) return;
    m_inputData = std::dynamic_pointer_cast<ImportedFileData>(data);
    if (m_inputData && m_outputNodeName.isEmpty()) {
        m_outputNodeName = generateDefaultOutputName();
        if (m_outputNodeNameEdit) m_outputNodeNameEdit->setText(m_outputNodeName);
    }
    ExecutableNodeDelegateModel::setInData(data, port);
    if (!m_inputData || m_inputData->filePaths().isEmpty()) {
        m_outputData.reset();
        m_imageInfoData.reset();
    }
}

ProductInputContract TroposphericCorrectionNode::productInputContract(PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    ProductInputContract contract;
    contract.semanticId = QStringLiteral("tropospheric_correction.input.unwrapped_phase");
    contract.allowedProductTypes = QStringList()
        << QStringLiteral("unwrapped_phase")
        << QStringLiteral("gacos_corrected_interferogram");
    contract.requiredProvenanceFields = QStringList()
        << QStringLiteral("producer") << QStringLiteral("output_port");
    return contract;
}

ProductOutputContract TroposphericCorrectionNode::productOutputContract(PortIndex portIndex) const
{
    ProductOutputContract contract;
    contract.semanticId = portIndex == 0
        ? QStringLiteral("tropospheric_correction.output.tropospheric_corrected_interferogram")
        : QStringLiteral("tropospheric_correction.output.preview");
    contract.publishedProductTypes = portIndex == 0
        ? QStringList() << QStringLiteral("tropospheric_corrected_interferogram")
        : QStringList() << QStringLiteral("preview");
    return contract;
}

std::shared_ptr<NodeData> TroposphericCorrectionNode::outData(PortIndex port)
{
    return ExecutableNodeDelegateModel::outData(port);
}

::QWidget* TroposphericCorrectionNode::embeddedWidget()
{
    if (!_widget) createWidget();
    return _widget;
}

QJsonObject TroposphericCorrectionNode::save() const
{
    QJsonObject modelJson = ExecutableNodeDelegateModel::save();
    modelJson["outputNodeName"] = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : m_outputNodeName;
    modelJson["era5Dir"] = m_era5Dir;
    return modelJson;
}

void TroposphericCorrectionNode::load(QJsonObject const &json)
{
    QJsonValue vName = json["outputNodeName"];
    if (!vName.isUndefined()) m_outputNodeName = vName.toString();
    QJsonValue vDir = json["era5Dir"];
    if (!vDir.isUndefined()) m_era5Dir = vDir.toString();

    ExecutableNodeDelegateModel::load(json);

    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setText(m_outputNodeName);
    if (m_era5DirEdit) m_era5DirEdit->setText(m_era5Dir);
}

void TroposphericCorrectionNode::setExecutionMode(ExecutionMode mode)
{
    ExecutableNodeDelegateModel::setExecutionMode(mode);
}

void TroposphericCorrectionNode::createWidget()
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
        if (_scene) Q_EMIT _scene->modified(_scene);
    };

    // ERA5 数据目录
    auto* era5Layout = new QHBoxLayout();
    m_era5DirEdit = new QLineEdit();
    m_era5DirEdit->setText(m_era5Dir);
    m_era5DirEdit->setPlaceholderText("ERA5 NetCDF 数据目录");
    m_browseBtn = new QPushButton("浏览");
    m_browseBtn->setFixedWidth(50);
    era5Layout->addWidget(m_era5DirEdit);
    era5Layout->addWidget(m_browseBtn);

    connect(m_browseBtn, &QPushButton::clicked, this, &TroposphericCorrectionNode::browseEra5Dir);
    connect(m_era5DirEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString text = m_era5DirEdit->text().trimmed();
        if (m_era5Dir != text) {
            if (!confirmParameterChange()) { m_era5DirEdit->setText(m_era5Dir); return; }
            m_era5Dir = text;
            invalidateNodeData();
        }
    });

    // 目标节点名称
    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setText(m_outputNodeName);
    m_outputNodeNameEdit->setPlaceholderText(QStringLiteral("自动生成或手动输入"));
    connect(m_outputNodeNameEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString text = m_outputNodeNameEdit->text().trimmed();
        if (m_outputNodeName != text) {
            if (!confirmParameterChange()) { m_outputNodeNameEdit->setText(m_outputNodeName); return; }
            NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), m_outputNodeName);
            m_outputNodeName = text;
            invalidateNodeData();
        }
    });

    formLayout->addRow("ERA5目录", era5Layout);
    formLayout->addRow("目标节点", m_outputNodeNameEdit);
}

void TroposphericCorrectionNode::browseEra5Dir()
{
    QString dir = QFileDialog::getExistingDirectory(nullptr, "选择ERA5数据目录", m_era5Dir);
    if (!dir.isEmpty()) {
        m_era5Dir = dir;
        if (m_era5DirEdit) m_era5DirEdit->setText(dir);
        if (m_outputData) m_outputData.reset();
        if (m_imageInfoData) m_imageInfoData.reset();
        setOutputData(0, nullptr);
        setOutputData(1, nullptr);
        invalidateExecution();
        if (_scene) Q_EMIT _scene->modified(_scene);
    }
}

void TroposphericCorrectionNode::updateWidgetSize()
{
    if (_widget) {
        _widget->setFixedWidth(300);
        _widget->adjustSize();
        Q_EMIT embeddedWidgetSizeUpdated();
    }
}

QString TroposphericCorrectionNode::generateDefaultOutputName() const
{
    if (m_inputData) return m_inputData->nodeName() + "_TropoCor";
    return "Tropospheric_Correction";
}

bool TroposphericCorrectionNode::validateInputs() const
{
    if (projectName().isEmpty()) return false;
    if (!m_inputData || m_inputData->filePaths().isEmpty()) return false;
    QString dstNode = m_outputNodeNameEdit ? m_outputNodeNameEdit->text().trimmed() : m_outputNodeName;
    if (dstNode.isEmpty()) return false;
    if (!dstNode.contains(QRegularExpression("^\\w+$"))) return false;
    QString era5Dir = m_era5DirEdit ? m_era5DirEdit->text().trimmed() : m_era5Dir;
    if (era5Dir.isEmpty()) return false;
    return true;
}

bool TroposphericCorrectionNode::prepareToStart()
{
    if (!validateInputs()) return false;
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
    m_preparedDstNode = m_outputNodeNameEdit->text().trimmed().isEmpty()
        ? generateDefaultOutputName() : m_outputNodeNameEdit->text().trimmed();
    m_preparedSavePath = projectPath();
    m_preparedProjectName = projectName();
    m_preparedSrcNode = m_inputData->nodeName();
    m_preparedEra5Dir = m_era5DirEdit ? m_era5DirEdit->text().trimmed() : m_era5Dir;

    m_preparedOutputPaths.clear();
    for (const QString& srcPath : m_inputData->filePaths()) {
        QFileInfo fi(srcPath);
        m_preparedOutputPaths.append(m_preparedSavePath + "/" + m_preparedDstNode + "/" + fi.baseName() + "_tropo.h5");
    }

    if (_isAutoTriggered) m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
    else m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(
        NodeUtils::getProjectContext(_widget), m_preparedDstNode, m_preparedOutputPaths, nullptr);

    return m_preparedOverwriteResult != NodeUtils::OverwriteResult::Cancel;
}

void TroposphericCorrectionNode::executeProcessing()
{
    InSARLogManager::LogInfo("TroposphericCorrectionNode", "executeProcessing started.");

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting) {
        m_outputNodeName = m_preparedDstNode;
        m_outputNodeNameEdit->setEnabled(true);
        m_era5DirEdit->setEnabled(true);
        m_browseBtn->setEnabled(true);
        setState(ExecutionState::Running);
        setProgress(100);
        if (validateAndRestoreOutput()) finishExecution();
        else setState(ExecutionState::Error);
        return;
    }

    setProgress(0);
    setState(ExecutionState::Running);
    m_generatedOutputPaths.clear();
    m_preparedPhasePaths = m_inputData ? m_inputData->filePaths() : QStringList();
    m_preparedPhaseNames.clear();
    for (const QString& path : m_preparedPhasePaths)
        m_preparedPhaseNames.append(QFileInfo(path).baseName());
    if (m_preparedPhasePaths.isEmpty()) {
        onError(QStringLiteral("没有可校正的干涉图"));
        return;
    }

    QString transactionError;
    if (!NodeUtils::beginOutputTransaction(m_preparedSavePath, m_preparedDstNode, m_preparedOutputPaths,
                                           m_preparedPhasePaths, m_outputTransaction, &transactionError, nullptr,
                                           NodeUtils::getProjectFilePath(_widget))) {
        onError(transactionError);
        return;
    }
    m_outputTransaction.executionRevision = executionRevision();
    QMap<QString, QString> provenance;
    provenance.insert(QStringLiteral("producer"), name());
    provenance.insert(QStringLiteral("output_port"), productOutputContract(0).semanticId);
    if (!NodeUtils::setOutputTransactionProductDescriptor(m_outputTransaction,
            ProductDescriptor::create(QStringLiteral("tropospheric_corrected_interferogram"),
                productOutputContract(0).schemaId, productOutputContract(0).schemaVersion,
                productOutputContract(0).publishedState, name(), provenance), &transactionError)) {
        onError(transactionError);
        return;
    }
    m_outputData.reset();
    m_imageInfoData.reset();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);

    m_thread = new QThread();
    m_workerThread = new TroposphericCorrectionWorker();
    m_workerThread->moveToThread(m_thread);

    connect(this, &TroposphericCorrectionNode::startCorrection, m_workerThread, &TroposphericCorrectionWorker::doCorrection);
    const QString stagingNode = m_outputTransaction.stagingName;
    connect(m_thread, &QThread::started, this, [this, stagingNode]() {
        Q_EMIT startCorrection(m_preparedEra5Dir, m_preparedSavePath, m_preparedProjectName,
            m_preparedSrcNode, stagingNode, m_preparedPhaseNames, m_preparedPhasePaths);
    });
    connect(m_workerThread, &TroposphericCorrectionWorker::updateProcess, this, &TroposphericCorrectionNode::onProgressUpdate);
    connect(m_workerThread, &TroposphericCorrectionWorker::outputsGenerated, this,
        [this](const QStringList&, const QStringList& outputPaths) {
            m_generatedOutputPaths = outputPaths;
        });
    connect(m_workerThread, &TroposphericCorrectionWorker::endProcess, this, &TroposphericCorrectionNode::onProcessingFinished);
    connect(m_workerThread, &TroposphericCorrectionWorker::errorProcess, this, &TroposphericCorrectionNode::onError);
    connect(m_workerThread, &TroposphericCorrectionWorker::cancelled, this, &TroposphericCorrectionNode::onCancelled);
    connect(m_workerThread, &TroposphericCorrectionWorker::endProcess, m_thread, &QThread::quit);
    connect(m_workerThread, &TroposphericCorrectionWorker::errorProcess, m_thread, &QThread::quit);
    connect(m_workerThread, &TroposphericCorrectionWorker::cancelled, m_thread, &QThread::quit);
    connect(m_workerThread, &TroposphericCorrectionWorker::destroyed, m_thread, &QThread::quit);
    connect(m_thread, &QThread::finished, m_workerThread, &TroposphericCorrectionWorker::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);

    QTimer::singleShot(0, this, [this]() {
        if (m_thread && m_thread->isRunning()) setState(ExecutionState::Running);
    });

    m_outputNodeNameEdit->setEnabled(false);
    m_era5DirEdit->setEnabled(false);
    m_browseBtn->setEnabled(false);
    deferAutomaticCompletion();
    m_thread->start();
}

void TroposphericCorrectionNode::onProgressUpdate(int progress, const QString& message) { Q_UNUSED(message); if (!isAutomaticExecutionObsolete()) setProgress(progress); }

void TroposphericCorrectionNode::onProcessingFinished()
{
    const QString dstNode = m_preparedDstNode;
    QStringList h5Paths;
    QStringList jpgPaths, types;

    cleanUpThreadAndWorker();

    if (discardObsoleteAutomaticExecution()) {
        NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("obsolete automatic execution"), projectXml());
        return;
    }

    QString transactionError;
    OutputCommitLease commitLease = acquireOutputCommitLease(m_outputTransaction.executionRevision);
    if (!commitLease) {
        NodeUtils::abandonOutputTransaction(m_outputTransaction,
            QStringLiteral("obsolete execution revision"), projectXml());
        return;
    }
    if (!projectXml() ||
        !NodeUtils::validateStagedOutputTransaction(m_outputTransaction, &transactionError) ||
        !NodeUtils::validateStagedH5Datasets(m_outputTransaction, QStringList() << QStringLiteral("phase"), &transactionError) ||
        !NodeUtils::workerOutputsMatchManifest(m_preparedOutputPaths, m_generatedOutputPaths, &transactionError) ||
        !NodeUtils::promoteOutputTransaction(m_outputTransaction, h5Paths, &transactionError) ||
        !NodeUtils::prepareOutputTransactionMetadataCommit(
            m_outputTransaction, projectXml(), NodeUtils::getProjectFilePath(_widget), &transactionError)) {
        onError(transactionError.isEmpty() ? QStringLiteral("对流层校正输出验收失败") : transactionError);
        return;
    }

    NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), dstNode, false, false);
    for (const QString& h5Path : h5Paths) {
        const QString name = QFileInfo(h5Path).baseName();
        const QString relativePath = QString("/%1/%2.h5").arg(dstNode, name);
        projectXml()->XMLFile_add_unwrap(dstNode.toStdString().c_str(), name.toStdString().c_str(),
            relativePath.toStdString().c_str(), 0, 0, "ERA5_Tropospheric", 0);
    }
    if (!NodeUtils::saveProjectXmlAtomically(projectXml(), NodeUtils::getProjectFilePath(_widget), &transactionError) ||
        !NodeUtils::markOutputTransactionMetadataCommitted(m_outputTransaction, &transactionError)) {
        onError(transactionError);
        return;
    }

    NodeUtils::removeDataNodeFromProjectTree(NodeUtils::getProjectContext(_widget), dstNode);
    persistOutputToProject(dstNode, h5Paths);
    m_outputData = std::make_shared<ImportedFileData>(h5Paths, dstNode);
    m_outputData->setProductDescriptor(ProductDescriptor::fromJson(
        m_outputTransaction.productDescriptor));
    setOutputData(0, m_outputData);
    for (const QString& h5Path : h5Paths) {
        jpgPaths.append(QFileInfo(h5Path).absolutePath() + "/" + QFileInfo(h5Path).baseName() + ".jpg");
        types.append("phase");
    }

    startPreviewGeneration(h5Paths, jpgPaths, types, h5Paths, jpgPaths, true);
}

void TroposphericCorrectionNode::onError(const QString& error)
{
    NodeUtils::abandonOutputTransaction(m_outputTransaction, error, projectXml());
    cleanUpThreadAndWorker();
    if (discardObsoleteAutomaticExecution()) return;
    m_outputNodeNameEdit->setEnabled(true); m_era5DirEdit->setEnabled(true); m_browseBtn->setEnabled(true);
    m_outputData.reset(); m_imageInfoData.reset();
    setOutputData(0, nullptr); setOutputData(1, nullptr);
    setLastErrorMessage(error);
    setState(ExecutionState::Error);
}

void TroposphericCorrectionNode::onCancelled()
{
    NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("cancelled"), projectXml());
    cleanUpThreadAndWorker();
    if (discardObsoleteAutomaticExecution()) return;

    setState(ExecutionState::Stopped);
    Q_EMIT executionStopped();
    Q_EMIT computingFinished();
    m_outputNodeNameEdit->setEnabled(true); m_era5DirEdit->setEnabled(true); m_browseBtn->setEnabled(true);
}

bool TroposphericCorrectionNode::validateAndRestoreOutput()
{
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty()) return false;
    QStringList h5Paths;
    ProductDescriptor::Ptr descriptor;
    QString identityError;
    if (!NodeUtils::loadCommittedOutputManifest(projectPath(), dstNode, h5Paths) ||
        !NodeUtils::loadCommittedOutputProductDescriptor(projectPath(), dstNode,
                                                          descriptor, &identityError) ||
        !validatePublishedDescriptor(productOutputContract(0), descriptor).accepted ||
        !NodeUtils::validateH5Identities(h5Paths, descriptor, &identityError)) {
        return false;
    }
    QStringList expectedJpgPaths, types;
    for (const QString& h5Path : h5Paths) {
        expectedJpgPaths.append(QFileInfo(h5Path).absolutePath() + "/" + QFileInfo(h5Path).baseName() + ".jpg");
        types.append("phase");
    }

    m_outputData = std::make_shared<ImportedFileData>(h5Paths, dstNode);
    m_outputData->setProductDescriptor(descriptor);
    setOutputData(0, m_outputData);
    QStringList missingH5s, missingJpgs, missingTypes, existingJpgs;
    for (int i = 0; i < expectedJpgPaths.size(); ++i) {
        if (NodeUtils::isJpgPreviewCurrent(h5Paths[i], expectedJpgPaths[i])) existingJpgs.append(expectedJpgPaths[i]);
        else { missingH5s.append(h5Paths[i]); missingJpgs.append(expectedJpgPaths[i]); missingTypes.append(types[i]); }
    }
    if (missingH5s.isEmpty()) {
        m_imageInfoData = std::make_shared<ImageInfoData>(existingJpgs);
        QMap<QString, QString> previewProvenance;
        previewProvenance.insert(QStringLiteral("producer"), name());
        previewProvenance.insert(QStringLiteral("output_port"), productOutputContract(1).semanticId);
        m_imageInfoData->setProductDescriptor(ProductDescriptor::create(QStringLiteral("preview"),
            productOutputContract(1).schemaId, productOutputContract(1).schemaVersion,
            productOutputContract(1).publishedState, name(), previewProvenance));
        setOutputData(1, m_imageInfoData); Q_EMIT dataUpdated(1);
    } else {
        startPreviewGeneration(missingH5s, missingJpgs, missingTypes, h5Paths, expectedJpgPaths, false);
    }
    return true;
}

void TroposphericCorrectionNode::persistOutputToProject(const QString& outputNodeName,
                                                        const QStringList& h5Paths)
{
    QStandardItemModel* model = projectModel();
    if (!model)
        return;

    const QList<QStandardItem*> projects = model->findItems(projectName());
    if (projects.isEmpty())
        return;

    QStandardItem* outputNode = NodeUtils::findOrCreateProjectNode(
        projects.first(), outputNodeName, "phase-2.5", FOLDER_ICON);
    if (!outputNode)
        return;

    for (const QString& h5Path : h5Paths) {
        const QString name = QFileInfo(h5Path).baseName();
        NodeUtils::findOrCreateChildItem(outputNode, name, "phase", h5Path, IMAGEDATA_ICON);
    }
    if (auto* iface = NodeUtils::getProjectContext(_widget))
        iface->refreshProjectTree();
}

void TroposphericCorrectionNode::startPreviewGeneration(const QStringList& h5Paths,
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
        if (completeExecution && discardObsoleteAutomaticExecution()) return;
        m_imageInfoData = std::make_shared<ImageInfoData>(currentJpgPaths);
        QMap<QString, QString> previewProvenance;
        previewProvenance.insert(QStringLiteral("producer"), name());
        previewProvenance.insert(QStringLiteral("output_port"), productOutputContract(1).semanticId);
        m_imageInfoData->setProductDescriptor(ProductDescriptor::create(QStringLiteral("preview"),
            productOutputContract(1).schemaId, productOutputContract(1).schemaVersion,
            productOutputContract(1).publishedState, name(), previewProvenance));
        setOutputData(1, m_imageInfoData);
        if (completeExecution) {
            m_outputNodeNameEdit->setEnabled(true); m_era5DirEdit->setEnabled(true); m_browseBtn->setEnabled(true);
            setState(ExecutionState::Running); setProgress(100); finishExecution();
        } else {
            Q_EMIT dataUpdated(1);
        }
    });
    QFuture<void> future = QtConcurrent::run([h5Paths, generatedJpgPaths, types]() {
        for (int i = 0; i < h5Paths.size(); ++i) {
            NodeUtils::generateJpgPreviewFromH5(h5Paths[i], generatedJpgPaths[i], types[i]);
        }
    });
    m_remedyWatcher.setFuture(future);
}

QStringList TroposphericCorrectionNode::previewImagePaths() const
{
    QStringList list;
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty()) return list;
    QStringList h5Paths;
    if (!NodeUtils::loadCommittedOutputManifest(projectPath(), dstNode, h5Paths)) return list;
    for (const QString& h5Path : h5Paths) {
        const QString jpg = QFileInfo(h5Path).absolutePath() + "/" + QFileInfo(h5Path).baseName() + ".jpg";
        if (QFile::exists(jpg)) list.append(jpg);
    }
    return list;
}

QStandardItemModel* TroposphericCorrectionNode::projectModel() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectModel() : nullptr;
}

QString TroposphericCorrectionNode::projectPath() const
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
        if (fullPath.endsWith(".insar", Qt::CaseInsensitive)) return QFileInfo(fullPath).absolutePath();
        return fullPath;
    }
    return QString();
}

QString TroposphericCorrectionNode::projectName() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectName() : QString();
}

XMLFile* TroposphericCorrectionNode::projectXml() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectXml() : nullptr;
}

void TroposphericCorrectionNode::cleanUpThreadAndWorker()
{
    QThread* thread = m_thread;
    m_thread = nullptr;
    m_workerThread = nullptr;

    if (!thread)
        return;

    if (thread->isRunning()) {
        thread->quit();
        thread->wait();
    }
}

void TroposphericCorrectionNode::releaseFinishedThreadAndWorker()
{
    QThread* thread = m_thread;
    m_thread = nullptr;
    m_workerThread = nullptr;
    if (thread && thread->isRunning()) {
        thread->quit();
    }
}

void TroposphericCorrectionNode::execute() { executeProcessing(); }

void TroposphericCorrectionNode::stopExecution()
{
    if (m_thread && m_thread->isRunning())
        m_thread->requestInterruption();
    cleanUpThreadAndWorker();
    NodeUtils::abandonOutputTransaction(m_outputTransaction, QStringLiteral("cancelled"), projectXml());
    m_outputData.reset(); m_imageInfoData.reset();
    setOutputData(0, nullptr); setOutputData(1, nullptr);
}

void TroposphericCorrectionNode::processAutomatically()
{
    if (prepareToStart()) executeProcessing();
    else setState(ExecutionState::Idle);
}

} // namespace QtNodes
