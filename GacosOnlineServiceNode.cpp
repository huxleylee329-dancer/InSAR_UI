#include "GacosOnlineServiceNode.h"
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "InterfaceManager.h"
#include "WorkspaceUI.h"
#include "NodeUtils.h"
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
    stopExecution();
}

unsigned int GacosOnlineServiceNode::nPorts(PortType portType) const
{
    if (portType == PortType::In) return 2;
    else return 2;
}

NodeDataType GacosOnlineServiceNode::dataType(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
    if (portType == PortType::In)
        return NodeDataType{"imported_file", "Imported File"};
    else {
        if (portIndex == 0) return NodeDataType{"imported_file", "Imported File"};
        else return NodeDataType{"image_info", "Image Info"};
    }
}

bool GacosOnlineServiceNode::portCaptionVisible(PortType, PortIndex) const { return true; }

QString GacosOnlineServiceNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) {
        if (portIndex == 0) return QStringLiteral("干涉图");
        else return QStringLiteral("DEM ?");
    } else {
        if (portIndex == 0) return QStringLiteral("成果 *");
        else return QStringLiteral("预览 ?");
    }
    return QString();
}

bool GacosOnlineServiceNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In && portIndex == 1) return true;
    if (portType == PortType::Out && portIndex == 1) return true;
    return false;
}

void GacosOnlineServiceNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    Q_UNUSED(port);
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
            NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), m_outputNodeName);
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

    m_preparedDstNode = m_outputNodeNameEdit->text().trimmed().isEmpty()
        ? generateDefaultOutputName() : m_outputNodeNameEdit->text().trimmed();
    m_preparedSavePath = projectPath();
    m_preparedProjectName = projectName();
    m_preparedSrcNode = m_inputData->nodeName();
    m_preparedApiKey = m_apiKeyEdit ? m_apiKeyEdit->text().trimmed() : m_apiKey;
    m_preparedEmail = m_emailEdit ? m_emailEdit->text().trimmed() : m_email;
    m_preparedDataFormat = m_dataFormat;

    QStringList pathsToCheck;
    QStringList srcPaths = m_inputData->filePaths();
    for (const QString& srcPath : srcPaths) {
        QFileInfo fi(srcPath);
        pathsToCheck.append(m_preparedSavePath + "/" + m_preparedDstNode + "/" + fi.baseName() + "_gacos_aps.h5");
    }

    if (_isAutoTriggered) {
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
    } else {
        m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(
            NodeUtils::getProjectContext(_widget), m_preparedDstNode, pathsToCheck, nullptr);
    }

    return m_preparedOverwriteResult != NodeUtils::OverwriteResult::Cancel;
}

void GacosOnlineServiceNode::executeProcessing()
{
    InSARLogManager::LogInfo("GacosOnlineServiceNode", "executeProcessing started.");

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting) {
        m_outputNodeName = m_preparedDstNode;
        m_outputNodeNameEdit->setEnabled(true);
        m_apiKeyEdit->setEnabled(true);
        m_emailEdit->setEnabled(true);
        m_dataFormatCombo->setEnabled(true);
        setState(ExecutionState::Running);
        setProgress(100);
        if (validateAndRestoreOutput()) finishExecution();
        else setState(ExecutionState::Error);
        return;
    }

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::Overwrite) {
        NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), m_preparedDstNode);
    }

    setProgress(0);
    setState(ExecutionState::Running);
    const QStringList inputPaths = m_inputData->filePaths();
    m_generatedOutputNames.clear();
    m_generatedOutputPaths.clear();

    m_thread = new QThread();
    m_workerThread = new GacosOnlineServiceWorker();
    m_workerThread->moveToThread(m_thread);

    connect(this, &GacosOnlineServiceNode::startGacos, m_workerThread, &GacosOnlineServiceWorker::doGacosRequest);
    connect(m_thread, &QThread::started, this, [this, inputPaths]() {
        Q_EMIT startGacos(m_preparedApiKey, m_preparedEmail, m_preparedDataFormat,
            m_preparedSavePath, m_preparedProjectName, m_preparedDstNode, inputPaths);
    });
    connect(m_workerThread, &GacosOnlineServiceWorker::updateProcess, this, &GacosOnlineServiceNode::onProgressUpdate);
    connect(m_workerThread, &GacosOnlineServiceWorker::endProcess, this, &GacosOnlineServiceNode::onProcessingFinished);
    connect(m_workerThread, &GacosOnlineServiceWorker::errorProcess, this, &GacosOnlineServiceNode::onError);
    connect(m_workerThread, &GacosOnlineServiceWorker::cancelled, this, &GacosOnlineServiceNode::onCancelled);
    connect(m_workerThread, &GacosOnlineServiceWorker::endProcess, m_thread, &QThread::quit);
    connect(m_workerThread, &GacosOnlineServiceWorker::errorProcess, m_thread, &QThread::quit);
    connect(m_workerThread, &GacosOnlineServiceWorker::cancelled, m_thread, &QThread::quit);
    connect(m_workerThread, &GacosOnlineServiceWorker::outputsGenerated,
            this, &GacosOnlineServiceNode::onResultsReceived);
    connect(m_workerThread, &GacosOnlineServiceWorker::destroyed, m_thread, &QThread::quit);
    connect(m_thread, &QThread::finished, m_workerThread, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);

    QTimer::singleShot(0, this, [this]() {
        if (m_thread && m_thread->isRunning()) setState(ExecutionState::Running);
    });

    m_outputNodeNameEdit->setEnabled(false);
    m_apiKeyEdit->setEnabled(false);
    m_emailEdit->setEnabled(false);
    m_dataFormatCombo->setEnabled(false);

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
    if (m_generatedOutputPaths.isEmpty()) {
        onError(QStringLiteral("GACOS did not return output files."));
        return;
    }
    for (const QString& h5Path : m_generatedOutputPaths) {
        if (!QFileInfo::exists(h5Path)) {
            onError(QStringLiteral("GACOS returned a missing output file."));
            return;
        }
    }
    const QString dstNode = m_outputNodeName;
    const QStringList h5Paths = m_generatedOutputPaths;
    QStringList jpgPaths;
    QStringList types;
    for (const QString& h5Path : h5Paths) {
        const QFileInfo h5Info(h5Path);
        jpgPaths.append(h5Info.absolutePath() + "/" + h5Info.baseName() + ".jpg");
        types.append("phase");
    }

    releaseFinishedThreadResources();

    if (discardObsoleteAutomaticExecution()) return;

    m_outputData = std::make_shared<ImportedFileData>(h5Paths, dstNode);
    setOutputData(0, m_outputData);

    if (!h5Paths.isEmpty()) {
        startPreviewGeneration(h5Paths, jpgPaths, types, h5Paths, jpgPaths, true);
    } else {
        m_imageInfoData.reset();
        setOutputData(1, nullptr);
        m_outputNodeNameEdit->setEnabled(true);
        m_apiKeyEdit->setEnabled(true);
        m_emailEdit->setEnabled(true);
        m_dataFormatCombo->setEnabled(true);
        setState(ExecutionState::Running);
        setProgress(100);
        finishExecution();
    }
}

void GacosOnlineServiceNode::onError(const QString& error)
{
    Q_UNUSED(error);
    releaseFinishedThreadResources();
    if (discardObsoleteAutomaticExecution()) return;
    m_outputNodeNameEdit->setEnabled(true);
    m_apiKeyEdit->setEnabled(true);
    m_emailEdit->setEnabled(true);
    m_dataFormatCombo->setEnabled(true);
    setState(ExecutionState::Error);
}

void GacosOnlineServiceNode::onCancelled()
{
    releaseFinishedThreadResources();
    if (discardObsoleteAutomaticExecution()) return;

    setState(ExecutionState::Stopped);
    Q_EMIT executionStopped();
    Q_EMIT computingFinished();
    m_outputNodeNameEdit->setEnabled(true);
    m_apiKeyEdit->setEnabled(true);
    m_emailEdit->setEnabled(true);
    m_dataFormatCombo->setEnabled(true);
}

void GacosOnlineServiceNode::onResultsReceived(
    const QString& dstNode,
    const QStringList& outputNames,
    const QStringList& outputPaths,
    const QString& savePath,
    const QString& projectName)
{
    if (isAutomaticExecutionObsolete()) return;
    if (outputNames.isEmpty() || outputNames.size() != outputPaths.size()) {
        InSARLogManager::LogError("GacosOnlineServiceNode", "Worker returned inconsistent GACOS output metadata.");
        return;
    }

    m_generatedOutputNames = outputNames;
    m_generatedOutputPaths = outputPaths;
    QStandardItemModel* model = projectModel();
    const QList<QStandardItem*> projects = model ? model->findItems(projectName) : QList<QStandardItem*>();
    if (projects.isEmpty()) {
        InSARLogManager::LogError("GacosOnlineServiceNode", "Project tree root was not found.");
        return;
    }

    QStandardItem* outputNode = NodeUtils::findOrCreateProjectNode(
        projects.first(), dstNode, "phase-2.5", FOLDER_ICON);
    if (!outputNode) {
        InSARLogManager::LogError("GacosOnlineServiceNode", "Unable to create GACOS output node.");
        return;
    }
    outputNode->setToolTip(projectName);

    XMLFile* xml = projectXml();
    for (int i = 0; i < outputPaths.size(); ++i) {
        QStandardItem* imageItem = NodeUtils::findOrCreateChildItem(
            outputNode, outputNames[i], "phase", outputPaths[i], IMAGEDATA_ICON);
        if (imageItem) {
            outputNode->setChild(imageItem->row(), 1, new QStandardItem(outputPaths[i]));
        }
        if (xml) {
            const QString relativePath = QString("/%1/%2").arg(dstNode, QFileInfo(outputPaths[i]).fileName());
            xml->XMLFile_add_unwrap(dstNode.toStdString().c_str(), outputNames[i].toStdString().c_str(),
                relativePath.toStdString().c_str(), 0, 0, "GACOS", 0);
        }
    }
    if (xml) {
        const QString xmlPath = savePath + "/" + projectName;
        xml->XMLFile_save(xmlPath.toStdString().c_str());
    }
    if (auto* iface = NodeUtils::getProjectContext(_widget)) {
        iface->refreshProjectTree();
    }
}

bool GacosOnlineServiceNode::validateAndRestoreOutput()
{
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty()) return false;
    QString outputPath = projectPath() + "/" + dstNode + "/";
    QDir dir(outputPath);
    if (!dir.exists()) return false;

    QStringList filters;
    filters << "*.h5";
    QStringList h5Files = dir.entryList(filters, QDir::Files);
    if (h5Files.isEmpty()) return false;

    QStringList h5Paths, expectedJpgPaths, types;
    for (const QString& h5File : h5Files) {
        h5Paths.append(dir.absoluteFilePath(h5File));
        expectedJpgPaths.append(outputPath + QFileInfo(h5File).baseName() + ".jpg");
        types.append("phase");
    }

    m_outputData = std::make_shared<ImportedFileData>(h5Paths, dstNode);
    setOutputData(0, m_outputData);
    Q_EMIT dataUpdated(0);

    QStringList missingH5s, missingJpgs, missingTypes, existingJpgs;
    for (int i = 0; i < expectedJpgPaths.size(); ++i) {
        if (NodeUtils::isJpgPreviewCurrent(h5Paths[i], expectedJpgPaths[i])) existingJpgs.append(expectedJpgPaths[i]);
        else { missingH5s.append(h5Paths[i]); missingJpgs.append(expectedJpgPaths[i]); missingTypes.append(types[i]); }
    }

    if (missingH5s.isEmpty()) {
        m_imageInfoData = std::make_shared<ImageInfoData>(existingJpgs);
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
            setOutputData(1, m_imageInfoData);
            m_outputNodeNameEdit->setEnabled(true);
            m_apiKeyEdit->setEnabled(true);
            m_emailEdit->setEnabled(true);
            m_dataFormatCombo->setEnabled(true);
            setState(ExecutionState::Running);
            setProgress(100);
            finishExecution();
        } else {
            m_imageInfoData = std::make_shared<ImageInfoData>(currentJpgPaths);
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
    QStringList list;
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty()) return list;
    QString outputPath = projectPath() + "/" + dstNode + "/";
    QDir dir(outputPath);
    if (dir.exists()) {
        QStringList filters;
        filters << "*.h5";
        for (const QString& h5File : dir.entryList(filters, QDir::Files)) {
            QString jpgPath = outputPath + QFileInfo(h5File).baseName() + ".jpg";
            if (QFile::exists(jpgPath)) list.append(jpgPath);
        }
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

void GacosOnlineServiceNode::processAutomatically()
{
    if (prepareToStart()) executeProcessing();
    else setState(ExecutionState::Idle);
}

} // namespace QtNodes

