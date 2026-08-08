#include "GacosOnlineServiceNode.h"
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "InterfaceManager.h"
#include "WorkspaceUI.h"
#include "NodeUtils.h"
#include "icon_source.h"
#include "InSARLogManager.h"
#include "tinyxml.h"
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
#include <QtConcurrent/QtConcurrent>

namespace QtNodes {

GacosOnlineServiceNode::GacosOnlineServiceNode()
    : ExecutableNodeDelegateModel()
    , _widget(nullptr)
    , m_apiKeyEdit(nullptr)
    , m_emailEdit(nullptr)
    , m_dataFormatCombo(nullptr)
    , m_outputNodeNameEdit(nullptr)
    , m_outputNodeName("")
    , m_apiKey("")
    , m_email("")
    , m_dataFormat(0)
    , m_workerThread(nullptr)
    , m_thread(nullptr)
{
    setExecutionMode(ExecutionMode::Automatic);
}

GacosOnlineServiceNode::~GacosOnlineServiceNode()
{
    ++m_executionGeneration;
    stopExecution();
    rollbackOutputTransaction(QStringLiteral("node destroyed"));
}

unsigned int GacosOnlineServiceNode::nPorts(PortType portType) const
{
    if (portType == PortType::In) return 1;
    else return 2;
}

NodeDataType GacosOnlineServiceNode::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) {
        return NodeDataType{"imported_file", "Imported File"};
    } else {
        if (portIndex == 0) return NodeDataType{"imported_file", "Imported File"};
        else return NodeDataType{"image_info", "Image Info"};
    }
}

bool GacosOnlineServiceNode::portCaptionVisible(PortType, PortIndex) const { return true; }

QString GacosOnlineServiceNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) {
        return QStringLiteral("解缠相位");
    } else {
        if (portIndex == 0) return QStringLiteral("成果 *");
        else return QStringLiteral("预览 ?");
    }
    return QString();
}

bool GacosOnlineServiceNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out && portIndex == 1) return true;
    return false;
}

void GacosOnlineServiceNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
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

ProductInputContract GacosOnlineServiceNode::productInputContract(PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    ProductInputContract contract;
    contract.semanticId = QStringLiteral("gacos_online_service.input.unwrapped_phase");
    contract.allowedProductTypes = QStringList() << QStringLiteral("unwrapped_phase");
    contract.requiredProvenanceFields = QStringList()
        << QStringLiteral("producer") << QStringLiteral("output_port");
    return contract;
}

ProductOutputContract GacosOnlineServiceNode::productOutputContract(PortIndex portIndex) const
{
    ProductOutputContract contract;
    contract.semanticId = portIndex == 0
        ? QStringLiteral("gacos_online_service.output.gacos_corrected_interferogram")
        : QStringLiteral("gacos_online_service.output.preview");
    contract.publishedProductTypes = portIndex == 0
        ? QStringList() << QStringLiteral("gacos_corrected_interferogram")
        : QStringList() << QStringLiteral("preview");
    return contract;
}

std::shared_ptr<NodeData> GacosOnlineServiceNode::outData(PortIndex port)
{
    return ExecutableNodeDelegateModel::outData(port);
}

::QWidget* GacosOnlineServiceNode::embeddedWidget()
{
    if (!_widget) createWidget();
    return _widget;
}

QJsonObject GacosOnlineServiceNode::save() const
{
    QJsonObject modelJson = ExecutableNodeDelegateModel::save();
    modelJson["outputNodeName"] = m_outputNodeNameEdit ? m_outputNodeNameEdit->text() : m_outputNodeName;
    modelJson["apiKey"] = m_apiKey;
    modelJson["email"] = m_email;
    modelJson["dataFormat"] = m_dataFormat;
    return modelJson;
}

void GacosOnlineServiceNode::load(QJsonObject const &json)
{
    QJsonValue vName = json["outputNodeName"];
    if (!vName.isUndefined()) m_outputNodeName = vName.toString();
    QJsonValue vKey = json["apiKey"];
    if (!vKey.isUndefined()) m_apiKey = vKey.toString();
    QJsonValue vEmail = json["email"];
    if (!vEmail.isUndefined()) m_email = vEmail.toString();
    QJsonValue vFmt = json["dataFormat"];
    if (!vFmt.isUndefined()) m_dataFormat = vFmt.toInt();

    ExecutableNodeDelegateModel::load(json);

    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setText(m_outputNodeName);
    if (m_apiKeyEdit) m_apiKeyEdit->setText(m_apiKey);
    if (m_emailEdit) m_emailEdit->setText(m_email);
    if (m_dataFormatCombo) m_dataFormatCombo->setCurrentIndex(m_dataFormat);
}

void GacosOnlineServiceNode::setExecutionMode(ExecutionMode mode)
{
    ExecutableNodeDelegateModel::setExecutionMode(mode);
}

void GacosOnlineServiceNode::createWidget()
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

    m_apiKeyEdit = new QLineEdit();
    m_apiKeyEdit->setText(m_apiKey);
    m_apiKeyEdit->setPlaceholderText("GACOS API Key");
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    connect(m_apiKeyEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString text = m_apiKeyEdit->text().trimmed();
        if (m_apiKey != text) {
            if (!confirmParameterChange()) { m_apiKeyEdit->setText(m_apiKey); return; }
            m_apiKey = text;
            invalidateNodeData();
        }
    });

    m_emailEdit = new QLineEdit();
    m_emailEdit->setText(m_email);
    m_emailEdit->setPlaceholderText("注册邮箱");
    connect(m_emailEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString text = m_emailEdit->text().trimmed();
        if (m_email != text) {
            if (!confirmParameterChange()) { m_emailEdit->setText(m_email); return; }
            m_email = text;
            invalidateNodeData();
        }
    });

    m_dataFormatCombo = new QComboBox();
    m_dataFormatCombo->setEditable(false);
    m_dataFormatCombo->addItem("GeoTIFF");
    m_dataFormatCombo->addItem("Binary Grid (.ztd)");
    m_dataFormatCombo->setCurrentIndex(m_dataFormat);
    connect(m_dataFormatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        [this, invalidateNodeData](int index) {
            if (m_dataFormat != index) {
                if (!confirmParameterChange()) {
                    m_dataFormatCombo->blockSignals(true);
                    m_dataFormatCombo->setCurrentIndex(m_dataFormat);
                    m_dataFormatCombo->blockSignals(false);
                    return;
                }
                m_dataFormat = index;
                invalidateNodeData();
            }
        });

    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setText(m_outputNodeName);
    m_outputNodeNameEdit->setPlaceholderText(QStringLiteral("自动生成或手动输入"));
    connect(m_outputNodeNameEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString text = m_outputNodeNameEdit->text().trimmed();
        if (m_outputNodeName != text) {
            if (!confirmParameterChange()) { m_outputNodeNameEdit->setText(m_outputNodeName); return; }
            m_outputNodeName = text;
            invalidateNodeData();
        }
    });

    formLayout->addRow("API Key", m_apiKeyEdit);
    formLayout->addRow("邮箱", m_emailEdit);
    formLayout->addRow("数据格式", m_dataFormatCombo);
    formLayout->addRow("目标节点", m_outputNodeNameEdit);
}

void GacosOnlineServiceNode::updateWidgetSize()
{
    if (_widget) {
        _widget->setFixedWidth(300);
        _widget->adjustSize();
        Q_EMIT embeddedWidgetSizeUpdated();
    }
}

QString GacosOnlineServiceNode::generateDefaultOutputName() const
{
    if (m_inputData) return m_inputData->nodeName() + "_GACOS_APS";
    return "GACOS_APS";
}

bool GacosOnlineServiceNode::validateInputs() const
{
    if (projectName().isEmpty()) return false;
    if (!m_inputData || m_inputData->filePaths().isEmpty()) return false;
    QString dstNode = m_outputNodeNameEdit ? m_outputNodeNameEdit->text().trimmed() : m_outputNodeName;
    if (dstNode.isEmpty()) return false;
    if (!dstNode.contains(QRegularExpression("^\\w+$"))) return false;
    if (m_apiKeyEdit ? m_apiKeyEdit->text().trimmed().isEmpty() : m_apiKey.isEmpty()) return false;
    return true;
}

bool GacosOnlineServiceNode::prepareToStart()
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

    const QString configuredDstNode = m_outputNodeNameEdit
        ? m_outputNodeNameEdit->text().trimmed()
        : m_outputNodeName.trimmed();
    m_preparedDstNode = configuredDstNode.isEmpty()
        ? generateDefaultOutputName() : configuredDstNode;
    m_preparedSavePath = projectPath();
    m_preparedProjectName = projectName();
    m_preparedSrcNode = m_inputData->nodeName();
    m_preparedApiKey = m_apiKeyEdit ? m_apiKeyEdit->text().trimmed() : m_apiKey;
    m_preparedEmail = m_emailEdit ? m_emailEdit->text().trimmed() : m_email;
    m_preparedDataFormat = m_dataFormat;
    m_preparedInputPaths = m_inputData->filePaths();

    m_preparedOutputPaths.clear();
    for (const QString& srcPath : m_preparedInputPaths) {
        QFileInfo fi(srcPath);
        m_preparedOutputPaths.append(QDir(m_preparedSavePath).absoluteFilePath(
            m_preparedDstNode + "/" + fi.baseName() + "_gacos_aps.h5"));
    }

    if (_isAutoTriggered) {
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
    } else {
        m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(
            NodeUtils::getProjectContext(_widget), m_preparedDstNode, m_preparedOutputPaths, nullptr);
    }

    return m_preparedOverwriteResult != NodeUtils::OverwriteResult::Cancel;
}

void GacosOnlineServiceNode::executeProcessing()
{
    InSARLogManager::LogInfo("GacosOnlineServiceNode", "executeProcessing started.");

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting) {
        m_outputNodeName = m_preparedDstNode;
        setControlsEnabled(true);
        setState(ExecutionState::Running);
        setProgress(100);
        if (validateAndRestoreOutput()) finishExecution();
        else setState(ExecutionState::Error);
        return;
    }

    const quint64 executionGeneration = ++m_executionGeneration;
    stopExecution();
    QString transactionError;
    if (!NodeUtils::beginOutputTransaction(m_preparedSavePath, m_preparedDstNode,
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
            ProductDescriptor::create(QStringLiteral("gacos_corrected_interferogram"),
                productOutputContract(0).schemaId, productOutputContract(0).schemaVersion,
                productOutputContract(0).publishedState, name(), provenance), &transactionError)) {
        onError(transactionError);
        return;
    }
    setProgress(0);
    setState(ExecutionState::Running);
    m_generatedOutputNames.clear();
    m_generatedOutputPaths.clear();

    m_thread = new QThread();
    m_workerThread = new GacosOnlineServiceWorker();
    m_workerThread->moveToThread(m_thread);

    GacosOnlineServiceWorker* const worker = m_workerThread;
    const QString projectRoot = m_preparedSavePath;
    const QString stagingNode = m_outputTransaction.stagingName;
    const QStringList inputPaths = m_preparedInputPaths;
    connect(m_thread, &QThread::started, m_workerThread, [worker, projectRoot, stagingNode, inputPaths,
            apiKey = m_preparedApiKey, email = m_preparedEmail, dataFormat = m_preparedDataFormat,
            projectName = m_preparedProjectName]() {
        worker->doGacosRequest(apiKey, email, dataFormat, projectRoot, projectName, stagingNode, inputPaths);
    });
    connect(m_workerThread, &GacosOnlineServiceWorker::updateProcess, this,
            [this, executionGeneration](int progress, const QString& message) {
        if (executionGeneration == m_executionGeneration) onProgressUpdate(progress, message);
    });
    connect(m_workerThread, &GacosOnlineServiceWorker::endProcess, this,
            [this, executionGeneration]() { if (executionGeneration == m_executionGeneration) onProcessingFinished(); });
    connect(m_workerThread, &GacosOnlineServiceWorker::errorProcess, this,
            [this, executionGeneration](const QString& error) { if (executionGeneration == m_executionGeneration) onError(error); });
    connect(m_workerThread, &GacosOnlineServiceWorker::cancelled, this,
            [this, executionGeneration]() { if (executionGeneration == m_executionGeneration) onCancelled(); });
    connect(m_workerThread, &GacosOnlineServiceWorker::endProcess, m_thread, &QThread::quit);
    connect(m_workerThread, &GacosOnlineServiceWorker::errorProcess, m_thread, &QThread::quit);
    connect(m_workerThread, &GacosOnlineServiceWorker::cancelled, m_thread, &QThread::quit);
    connect(m_workerThread, &GacosOnlineServiceWorker::outputsGenerated, this,
            [this, executionGeneration](const QString& dstNode, const QStringList& names,
                                        const QStringList& paths, const QString& savePath, const QString& projectName) {
        if (executionGeneration == m_executionGeneration) onResultsReceived(dstNode, names, paths, savePath, projectName);
    });
    connect(m_workerThread, &GacosOnlineServiceWorker::destroyed, m_thread, &QThread::quit);
    connect(m_thread, &QThread::finished, m_workerThread, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);

    QTimer::singleShot(0, this, [this]() {
        if (m_thread && m_thread->isRunning()) setState(ExecutionState::Running);
    });

    setControlsEnabled(false);

    deferAutomaticCompletion();
    m_thread->start();
}

void GacosOnlineServiceNode::onProgressUpdate(int progress, const QString& message)
{
    Q_UNUSED(message);
    if (isAutomaticExecutionObsolete()) return;
    setProgress(progress);
}

void GacosOnlineServiceNode::onProcessingFinished()
{
    QString transactionError;
    if (!commitOutputTransaction(&transactionError)) {
        onError(transactionError);
        return;
    }
    releaseFinishedThreadResources();
    const QStringList h5Paths = m_outputData ? m_outputData->filePaths() : QStringList();
    QStringList jpgPaths;
    QStringList types;
    for (const QString& h5Path : h5Paths) {
        const QFileInfo h5Info(h5Path);
        jpgPaths.append(h5Info.absolutePath() + "/" + h5Info.baseName() + ".jpg");
        types.append("phase");
    }

    if (discardObsoleteAutomaticExecution()) return;

    if (!h5Paths.isEmpty()) {
        startPreviewGeneration(h5Paths, jpgPaths, types, h5Paths, jpgPaths, true);
    } else {
        m_imageInfoData.reset();
        setOutputData(1, nullptr);
        setControlsEnabled(true);
        setState(ExecutionState::Running);
        setProgress(100);
        finishExecution();
    }
}

void GacosOnlineServiceNode::onError(const QString& error)
{
    rollbackOutputTransaction(error);
    releaseFinishedThreadResources();
    if (discardObsoleteAutomaticExecution()) return;
    InSARLogManager::LogError("GacosOnlineServiceNode", error);
    setControlsEnabled(true);
    setState(ExecutionState::Error);
}

void GacosOnlineServiceNode::onCancelled()
{
    rollbackOutputTransaction(QStringLiteral("cancelled"));
    releaseFinishedThreadResources();
    if (discardObsoleteAutomaticExecution()) return;

    setState(ExecutionState::Stopped);
    Q_EMIT executionStopped();
    Q_EMIT computingFinished();
    setControlsEnabled(true);
}

void GacosOnlineServiceNode::onResultsReceived(
    const QString& dstNode,
    const QStringList& outputNames,
    const QStringList& outputPaths,
    const QString& savePath,
    const QString& projectName)
{
    if (isAutomaticExecutionObsolete()) return;
    if (dstNode != m_outputTransaction.stagingName || outputNames.isEmpty() ||
        outputNames.size() != outputPaths.size()) {
        ++m_executionGeneration;
        onError(QStringLiteral("Worker returned inconsistent GACOS output metadata."));
        return;
    }
    for (int i = 0; i < outputPaths.size(); ++i) {
        if (QFileInfo(outputPaths[i]).baseName() != outputNames[i]) {
            ++m_executionGeneration;
            onError(QStringLiteral("Worker returned a GACOS output with an inconsistent file name."));
            return;
        }
    }

    m_generatedOutputNames = outputNames;
    m_generatedOutputPaths = outputPaths;
    Q_UNUSED(savePath);
    Q_UNUSED(projectName);
}

bool GacosOnlineServiceNode::validateAndRestoreOutput()
{
    QStringList h5Paths;
    ProductDescriptor::Ptr descriptor;
    QString identityError;
    if (!NodeUtils::loadCommittedOutputManifest(projectPath(), m_outputNodeName, h5Paths) ||
        !NodeUtils::loadCommittedOutputProductDescriptor(projectPath(), m_outputNodeName,
                                                          descriptor, &identityError) ||
        !validatePublishedDescriptor(productOutputContract(0), descriptor).accepted ||
        !NodeUtils::validateH5Identities(h5Paths, descriptor, &identityError)) {
        return false;
    }
    const QString dstNode = m_outputNodeName.trimmed();
    QStringList expectedJpgPaths, types;
    for (const QString& h5Path : h5Paths) {
        expectedJpgPaths.append(QFileInfo(h5Path).absolutePath() + "/" +
                                QFileInfo(h5Path).baseName() + ".jpg");
        types.append("phase");
    }

    m_outputData = std::make_shared<ImportedFileData>(h5Paths, dstNode);
    m_outputData->setProductDescriptor(descriptor);
    setOutputData(0, m_outputData);
    Q_EMIT dataUpdated(0);

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
        setOutputData(1, m_imageInfoData);
        Q_EMIT dataUpdated(1);
    } else {
        startPreviewGeneration(missingH5s, missingJpgs, missingTypes, h5Paths, expectedJpgPaths, false);
    }
    return true;
}

void GacosOnlineServiceNode::startPreviewGeneration(const QStringList& h5Paths,
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
            if (discardObsoleteAutomaticExecution()) return;
            m_imageInfoData = std::make_shared<ImageInfoData>(currentJpgPaths);
            QMap<QString, QString> previewProvenance;
            previewProvenance.insert(QStringLiteral("producer"), name());
            previewProvenance.insert(QStringLiteral("output_port"), productOutputContract(1).semanticId);
            m_imageInfoData->setProductDescriptor(ProductDescriptor::create(QStringLiteral("preview"),
                productOutputContract(1).schemaId, productOutputContract(1).schemaVersion,
                productOutputContract(1).publishedState, name(), previewProvenance));
            setOutputData(1, m_imageInfoData);
            setControlsEnabled(true);
            setState(ExecutionState::Running);
            setProgress(100);
            finishExecution();
        } else {
            m_imageInfoData = std::make_shared<ImageInfoData>(currentJpgPaths);
            QMap<QString, QString> previewProvenance;
            previewProvenance.insert(QStringLiteral("producer"), name());
            previewProvenance.insert(QStringLiteral("output_port"), productOutputContract(1).semanticId);
            m_imageInfoData->setProductDescriptor(ProductDescriptor::create(QStringLiteral("preview"),
                productOutputContract(1).schemaId, productOutputContract(1).schemaVersion,
                productOutputContract(1).publishedState, name(), previewProvenance));
            setOutputData(1, m_imageInfoData);
            Q_EMIT dataUpdated(1);
        }
    });
    m_remedyWatcher.setFuture(QtConcurrent::run([h5Paths, generatedJpgPaths, types]() {
        for (int i = 0; i < h5Paths.size(); ++i) {
            NodeUtils::generateJpgPreviewFromH5(h5Paths[i], generatedJpgPaths[i], types[i]);
        }
    }));
}

QStringList GacosOnlineServiceNode::previewImagePaths() const
{
    QStringList h5Paths;
    if (!NodeUtils::loadCommittedOutputManifest(projectPath(), m_outputNodeName, h5Paths)) {
        return QStringList();
    }
    QStringList list;
    for (const QString& h5Path : h5Paths) {
        const QString jpgPath = QFileInfo(h5Path).absolutePath() + "/" +
            QFileInfo(h5Path).baseName() + ".jpg";
        if (QFile::exists(jpgPath)) list.append(jpgPath);
    }
    return list;
}

QStandardItemModel* GacosOnlineServiceNode::projectModel() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectModel() : nullptr;
}

QString GacosOnlineServiceNode::projectPath() const
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

QString GacosOnlineServiceNode::projectName() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectName() : QString();
}

XMLFile* GacosOnlineServiceNode::projectXml() const
{
    auto iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectXml() : nullptr;
}

void GacosOnlineServiceNode::execute() { executeProcessing(); }

void GacosOnlineServiceNode::stopExecution()
{
    if (m_thread && m_thread->isRunning()) {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
    }
    rollbackOutputTransaction(QStringLiteral("stopped"));
    m_thread = nullptr;
    m_workerThread = nullptr;
}

void GacosOnlineServiceNode::releaseFinishedThreadResources()
{
    QThread* thread = m_thread;
    m_thread = nullptr;
    m_workerThread = nullptr;
    if (thread && thread->isRunning()) {
        thread->quit();
    }
}

bool GacosOnlineServiceNode::commitOutputTransaction(QString* errorMessage)
{
    OutputCommitLease commitLease = acquireOutputCommitLease(m_outputTransaction.executionRevision);
    if (!commitLease) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("GACOS output is obsolete because its execution revision changed.");
        }
        return false;
    }
    if (m_generatedOutputNames.size() != m_generatedOutputPaths.size() || m_generatedOutputPaths.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("GACOS worker did not return a complete staging output map.");
        return false;
    }
    IApplicationInterface* iface = NodeUtils::getProjectContext(_widget);
    XMLFile* xml = iface ? iface->projectXml() : nullptr;
    const QString xmlPath = NodeUtils::getProjectFilePath(_widget);
    if (!xml || xmlPath.isEmpty() ||
        !NodeUtils::validateStagedOutputTransaction(m_outputTransaction, errorMessage) ||
        !NodeUtils::validateStagedH5Datasets(m_outputTransaction, QStringList() << QStringLiteral("phase"), errorMessage) ||
        !NodeUtils::workerOutputsMatchManifest(m_preparedOutputPaths, m_generatedOutputPaths, errorMessage)) {
        return false;
    }
    QStringList finalPaths;
    if (!NodeUtils::promoteOutputTransaction(m_outputTransaction, finalPaths, errorMessage) ||
        !NodeUtils::prepareOutputTransactionMetadataCommit(m_outputTransaction, xml, xmlPath, errorMessage)) {
        return false;
    }
    TiXmlElement* root = nullptr;
    if (xml->get_root(root) < 0 || !root) {
        if (errorMessage) *errorMessage = QStringLiteral("Unable to read project XML root for GACOS output.");
        return false;
    }
    for (TiXmlElement* element = root->FirstChildElement(); element != nullptr; ) {
        const char* name = element->Attribute("name");
        if (name && m_preparedDstNode == QString::fromUtf8(name)) {
            TiXmlElement* toRemove = element;
            element = element->NextSiblingElement();
            root->RemoveChild(toRemove);
        } else {
            element = element->NextSiblingElement();
        }
    }
    for (int i = 0; i < finalPaths.size(); ++i) {
        const QString outputName = QFileInfo(finalPaths[i]).baseName();
        const QString relativePath = QStringLiteral("/%1/%2").arg(m_preparedDstNode, QFileInfo(finalPaths[i]).fileName());
        if (xml->XMLFile_add_unwrap(m_preparedDstNode.toStdString().c_str(), outputName.toStdString().c_str(),
                                    relativePath.toStdString().c_str(), 0, 0, "GACOS", 0) < 0) {
            if (errorMessage) *errorMessage = QStringLiteral("Unable to add GACOS output to project XML.");
            return false;
        }
    }
    if (!NodeUtils::saveProjectXmlAtomically(xml, xmlPath, errorMessage) ||
        !NodeUtils::markOutputTransactionMetadataCommitted(m_outputTransaction, errorMessage)) {
        return false;
    }
    if (iface && iface->projectModel()) {
        NodeUtils::removeDataNodeFromProjectTree(iface, m_preparedDstNode);
        const QList<QStandardItem*> projects = iface->projectModel()->findItems(m_preparedProjectName);
        if (!projects.isEmpty()) {
            QStandardItem* outputNode = NodeUtils::findOrCreateProjectNode(
                projects.first(), m_preparedDstNode, "phase-2.5", FOLDER_ICON);
            if (outputNode) {
                outputNode->setToolTip(m_preparedProjectName);
                for (int i = 0; i < finalPaths.size(); ++i) {
                    NodeUtils::findOrCreateChildItem(outputNode, QFileInfo(finalPaths[i]).baseName(),
                                                      "phase", finalPaths[i], IMAGEDATA_ICON);
                }
            }
        }
        iface->refreshProjectTree();
    }
    m_outputNodeName = m_preparedDstNode;
    m_outputData = std::make_shared<ImportedFileData>(finalPaths, m_preparedDstNode);
    m_outputData->setProductDescriptor(ProductDescriptor::fromJson(
        m_outputTransaction.productDescriptor));
    setOutputData(0, m_outputData);
    return true;
}

void GacosOnlineServiceNode::rollbackOutputTransaction(const QString& reason)
{
    IApplicationInterface* iface = NodeUtils::getProjectContext(_widget);
    NodeUtils::abandonOutputTransaction(m_outputTransaction, reason, iface ? iface->projectXml() : nullptr);
}

void GacosOnlineServiceNode::setControlsEnabled(bool enabled)
{
    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setEnabled(enabled);
    if (m_apiKeyEdit) m_apiKeyEdit->setEnabled(enabled);
    if (m_emailEdit) m_emailEdit->setEnabled(enabled);
    if (m_dataFormatCombo) m_dataFormatCombo->setEnabled(enabled);
}

void GacosOnlineServiceNode::processAutomatically()
{
    if (prepareToStart()) executeProcessing();
    else setState(ExecutionState::Idle);
}

} // namespace QtNodes

