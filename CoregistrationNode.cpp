#include "CoregistrationNode.h"
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
#include <QDir>
#include <QApplication>
#include <QDateTime>
#include <QStandardItemModel>
#include <QMessageBox>
#include <QFileDialog>
#include <QtConcurrent/QtConcurrent>

namespace QtNodes {

CoregistrationNode::CoregistrationNode()
    : ExecutableNodeDelegateModel()
    , _widget(nullptr)
    , m_method("Coarse")
    , m_defaultFirstMaster(true)
    , m_masterIndex(1)
    , m_interpTimes(4)
    , m_blockSize(64)
    , m_demPath("")
    , m_outputNodeName("Coregistration")
    , m_outputFileName("{InputName}_regis")
    , m_worker(nullptr)
    , m_thread(nullptr)
    , m_isExecuting(false)
{
    setExecutionMode(ExecutionMode::Automatic);
}

CoregistrationNode::~CoregistrationNode()
{
    stopExecution();
}

unsigned int CoregistrationNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 2;
    else
        return 2;
}

NodeDataType CoregistrationNode::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In)
    {
        if (portIndex == 0)
            return NodeDataType{"imported_file", "Imported File"};
        else
            return NodeDataType{"imported_file", "DEM File"};
    }
    else
    {
        if (portIndex == 0)
            return NodeDataType{"imported_file", "Imported File"};
        else
            return NodeDataType{"image_info", "Image Info"};
    }
}

bool CoregistrationNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return true;
}

QString CoregistrationNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In) {
        if (portIndex == 0)
            return QStringLiteral("输入数据");
        else
            return QStringLiteral("DEM ?");
    } else {
        if (portIndex == 0)
            return QStringLiteral("成果 *");
        else
            return QStringLiteral("预览 ?");
    }
}

bool CoregistrationNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In && portIndex == 1)
        return true;
    if (portType == PortType::Out && portIndex == 1)
        return true;
    return false;
}

void CoregistrationNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    if (port == 0) {
        m_inputData = std::dynamic_pointer_cast<ImportedFileData>(data);
        if (!m_inputData || m_inputData->filePaths().isEmpty()) {
            if (!isRestoring()) {
                m_outputData.reset();
                m_previewData.reset();
                m_outputImagePaths.clear();
                m_outputJpgPaths.clear();
                m_savedOutputFiles.clear();
                setOutputData(0, nullptr);
                setOutputData(1, nullptr);
            }
        }
        updateMasterImageCombo();
        if (m_inputData && m_outputNodeName.isEmpty()) {
            m_outputNodeName = "Coregistration";
            if (m_outputNodeNameEdit) {
                m_outputNodeNameEdit->setText(m_outputNodeName);
            }
        }
    } else if (port == 1) {
        m_demInputData = std::dynamic_pointer_cast<ImportedFileData>(data);
        if (m_demInputData) {
            m_demPath = m_demInputData->filePath();
            if (m_demPathEdit) {
                m_demPathEdit->setText(m_demPath);
            }
        } else {
            if (!isRestoring()) {
                m_demPath.clear();
                if (m_demPathEdit) {
                    m_demPathEdit->clear();
                }
            }
        }
    }

    ExecutableNodeDelegateModel::setInData(data, port);
    updateParameterWidgetsEnableState();
}

std::shared_ptr<NodeData> CoregistrationNode::outData(PortIndex port)
{
    return ExecutableNodeDelegateModel::outData(port);
}

QWidget* CoregistrationNode::embeddedWidget()
{
    if (!_widget) {
        createWidget();
    }
    return _widget;
}

void CoregistrationNode::createWidget()
{
    _widget = new QWidget();
    _widget->setObjectName("NodeEmbeddedWidget");
    _widget->setFixedWidth(300);

    auto* layout = new QVBoxLayout(_widget);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto invalidateNodeData = [this]() {
        if (m_outputData) m_outputData.reset();
        if (m_previewData) m_previewData.reset();
        setOutputData(0, nullptr);
        setOutputData(1, nullptr);
        invalidateExecution();
    };

    QFormLayout* formLayout = new QFormLayout();
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setSpacing(6);
    formLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    formLayout->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);

    m_methodCombo = new QComboBox();
    m_methodCombo->addItem(QStringLiteral("强度图配准 (Coarse)"), "Coarse");
    m_methodCombo->addItem(QStringLiteral("DEM辅助配准 (Fine)"), "Fine");
    m_methodCombo->setCurrentIndex(m_method == "Coarse" ? 0 : 1);
    connect(m_methodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, invalidateNodeData](int index) {
        QString newMethod = m_methodCombo->itemData(index).toString();
        if (m_method != newMethod) {
            if (!confirmParameterChange()) {
                QSignalBlocker blocker(m_methodCombo);
                m_methodCombo->setCurrentIndex(m_method == "Coarse" ? 0 : 1);
                return;
            }
            m_method = newMethod;
            updateWidgetSize();
            updateParameterWidgetsEnableState();
            invalidateNodeData();
        }
    });
    formLayout->addRow(new QLabel(QStringLiteral("配准模式：")), m_methodCombo);

    m_defaultFirstMasterCheckBox = new QCheckBox(QStringLiteral("默认首张图像为主图像"));
    m_defaultFirstMasterCheckBox->setChecked(m_defaultFirstMaster);
    connect(m_defaultFirstMasterCheckBox, &QCheckBox::stateChanged, this, [this, invalidateNodeData](int state) {
        bool newState = (state == Qt::Checked);
        if (m_defaultFirstMaster != newState) {
            if (!confirmParameterChange()) {
                QSignalBlocker blocker(m_defaultFirstMasterCheckBox);
                m_defaultFirstMasterCheckBox->setChecked(m_defaultFirstMaster);
                return;
            }
            m_defaultFirstMaster = newState;
            updateMasterImageCombo();
            updateParameterWidgetsEnableState();
            invalidateNodeData();
        }
    });
    formLayout->addRow(m_defaultFirstMasterCheckBox);

    m_masterImageCombo = new QComboBox();
    connect(m_masterImageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, invalidateNodeData](int index) {
        if (!m_defaultFirstMaster && m_inputData && index >= 0) {
            int newIndex = index + 1;
            if (m_masterIndex != newIndex) {
                if (!confirmParameterChange()) {
                    QSignalBlocker blocker(m_masterImageCombo);
                    m_masterImageCombo->setCurrentIndex(m_masterIndex - 1);
                    return;
                }
                m_masterIndex = newIndex;
                invalidateNodeData();
            }
        }
    });
    formLayout->addRow(new QLabel(QStringLiteral("主图像：")), m_masterImageCombo);

    m_interpLabel = new QLabel(QStringLiteral("插值倍数："));
    m_interpCombo = new QComboBox();
    m_interpCombo->addItems(QStringList() << "2" << "4" << "8" << "16");
    m_interpCombo->setCurrentText(QString::number(m_interpTimes));
    connect(m_interpCombo, &QComboBox::currentTextChanged, this, [this, invalidateNodeData](const QString& text) {
        int val = text.toInt();
        if (m_interpTimes != val) {
            if (!confirmParameterChange()) {
                QSignalBlocker blocker(m_interpCombo);
                m_interpCombo->setCurrentText(QString::number(m_interpTimes));
                return;
            }
            m_interpTimes = val;
            invalidateNodeData();
        }
    });
    formLayout->addRow(m_interpLabel, m_interpCombo);

    m_blockSizeLabel = new QLabel(QStringLiteral("块大小："));
    m_blockSizeCombo = new QComboBox();
    m_blockSizeCombo->addItems(QStringList() << "32" << "64" << "128" << "256");
    m_blockSizeCombo->setCurrentText(QString::number(m_blockSize));
    connect(m_blockSizeCombo, &QComboBox::currentTextChanged, this, [this, invalidateNodeData](const QString& text) {
        int val = text.toInt();
        if (m_blockSize != val) {
            if (!confirmParameterChange()) {
                QSignalBlocker blocker(m_blockSizeCombo);
                m_blockSizeCombo->setCurrentText(QString::number(m_blockSize));
                return;
            }
            m_blockSize = val;
            invalidateNodeData();
        }
    });
    formLayout->addRow(m_blockSizeLabel, m_blockSizeCombo);

    m_demPathLabel = new QLabel(QStringLiteral("DEM路径："));
    m_demPathLabel->setStyleSheet("QLabel:disabled { color: #888888; }");

    m_demPathEdit = new QLineEdit();
    m_demPathEdit->setText(m_demPath);
    m_demPathEdit->setPlaceholderText(QStringLiteral("选择DEM数据 (*.h5, *.tiff)..."));
    m_demPathEdit->setStyleSheet(
        "QLineEdit:disabled {"
        "  background-color: rgba(120, 120, 120, 0.1);"
        "  color: #888888;"
        "  border: 1px dashed rgba(148, 163, 184, 0.2);"
        "}"
    );
    connect(m_demPathEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString text = m_demPathEdit->text().trimmed();
        if (m_demPath != text) {
            m_demPath = text;
            invalidateNodeData();
        }
    });
    
    m_demBrowseBtn = new QPushButton(QStringLiteral("浏览..."));
    m_demBrowseBtn->setStyleSheet(
        "QPushButton:disabled {"
        "  background-color: rgba(120, 120, 120, 0.1);"
        "  color: #888888;"
        "  border: 1px dashed rgba(148, 163, 184, 0.2);"
        "}"
    );
    connect(m_demBrowseBtn, &QPushButton::clicked, this, [this]() {
        QString file = QFileDialog::getOpenFileName(nullptr, QStringLiteral("选择DEM数据"), "", "DEM Files (*.h5 *.tiff *.tif)");
        if (!file.isEmpty()) {
            m_demPath = file;
            if (m_demPathEdit) m_demPathEdit->setText(m_demPath);
            if (m_outputData) m_outputData.reset();
            if (m_previewData) m_previewData.reset();
            setOutputData(0, nullptr);
            setOutputData(1, nullptr);
            invalidateExecution();
        }
    });

    auto* demLayout = new QHBoxLayout();
    demLayout->addWidget(m_demPathEdit);
    demLayout->addWidget(m_demBrowseBtn);
    formLayout->addRow(m_demPathLabel, demLayout);

    m_outputNodeNameEdit = new QLineEdit();
    m_outputNodeNameEdit->setText(m_outputNodeName);
    connect(m_outputNodeNameEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString text = m_outputNodeNameEdit->text().trimmed();
        if (m_outputNodeName != text) {
            m_outputNodeName = text;
            invalidateNodeData();
        }
    });
    formLayout->addRow(new QLabel(QStringLiteral("输出节点名：")), m_outputNodeNameEdit);

    m_outputFileNameEdit = new QLineEdit();
    m_outputFileNameEdit->setText(m_outputFileName);
    connect(m_outputFileNameEdit, &QLineEdit::editingFinished, this, [this, invalidateNodeData]() {
        QString text = m_outputFileNameEdit->text().trimmed();
        if (m_outputFileName != text) {
            m_outputFileName = text;
            invalidateNodeData();
        }
    });
    formLayout->addRow(new QLabel(QStringLiteral("文件名命名规则：")), m_outputFileNameEdit);

    layout->addLayout(formLayout);

    updateWidgetSize();
    updateParameterWidgetsEnableState();
}

void CoregistrationNode::updateMasterImageCombo()
{
    if (!m_masterImageCombo) return;

    m_masterImageCombo->clear();

    if (m_defaultFirstMaster) {
        m_masterImageCombo->addItem(QStringLiteral("自动选择首张图像..."));
    } else {
        if (m_inputData) {
            QStringList paths = m_inputData->filePaths();
            for (const QString& path : paths) {
                m_masterImageCombo->addItem(QFileInfo(path).fileName());
            }
            if (m_masterIndex > 0 && m_masterIndex <= m_masterImageCombo->count()) {
                m_masterImageCombo->setCurrentIndex(m_masterIndex - 1);
            } else {
                m_masterImageCombo->setCurrentIndex(0);
                m_masterIndex = 1;
            }
        } else {
            m_masterImageCombo->addItem(QStringLiteral("无数据输入"));
        }
    }
    updateParameterWidgetsEnableState();
}

void CoregistrationNode::updateWidgetSize()
{
    if (!_widget) return;

    bool isCoarse = (m_method == "Coarse");

    m_interpLabel->setVisible(isCoarse);
    m_interpCombo->setVisible(isCoarse);
    m_blockSizeLabel->setVisible(isCoarse);
    m_blockSizeCombo->setVisible(isCoarse);

    m_demPathLabel->setVisible(!isCoarse);
    m_demPathEdit->setVisible(!isCoarse);
    m_demBrowseBtn->setVisible(!isCoarse);
}

void CoregistrationNode::updateParameterWidgetsEnableState()
{
    bool hasInput = (m_inputData && !m_inputData->filePaths().isEmpty());
    bool isExec = m_isExecuting;
    bool enableWidgets = hasInput && !isExec;

    if (m_methodCombo) m_methodCombo->setEnabled(enableWidgets);
    if (m_defaultFirstMasterCheckBox) m_defaultFirstMasterCheckBox->setEnabled(enableWidgets);
    if (m_masterImageCombo) m_masterImageCombo->setEnabled(enableWidgets && !m_defaultFirstMaster);
    
    bool isCoarse = (m_method == "Coarse");
    bool hasDemConn = (m_demInputData != nullptr);
    if (m_interpCombo) m_interpCombo->setEnabled(enableWidgets && isCoarse);
    if (m_blockSizeCombo) m_blockSizeCombo->setEnabled(enableWidgets && isCoarse);
    if (m_demPathLabel) m_demPathLabel->setEnabled(enableWidgets && !isCoarse && !hasDemConn);
    if (m_demPathEdit) m_demPathEdit->setEnabled(enableWidgets && !isCoarse && !hasDemConn);
    if (m_demBrowseBtn) m_demBrowseBtn->setEnabled(enableWidgets && !isCoarse && !hasDemConn);
    if (m_outputNodeNameEdit) m_outputNodeNameEdit->setEnabled(enableWidgets);
    if (m_outputFileNameEdit) m_outputFileNameEdit->setEnabled(enableWidgets);
}

QStringList CoregistrationNode::previewImagePaths() const
{
    QStringList validPaths;
    for (const QString& path : m_outputJpgPaths) {
        if (QFileInfo::exists(path)) {
            validPaths << path;
        }
    }
    return validPaths;
}

QString CoregistrationNode::getRealSavePath() const
{
    QString path = projectPath();
    if (path.endsWith(".insar", Qt::CaseInsensitive)) {
        return QFileInfo(path).absolutePath();
    }
    return path;
}

QString CoregistrationNode::resolveOutputFileName(const QString& originalName) const
{
    QString pattern = m_outputFileName.trimmed();
    if (pattern.isEmpty()) {
        pattern = "{InputName}_regis";
    }
    // Normalize brackets
    QRegularExpression re("[\\{\\x{FF5B}]\\s*InputName\\s*[\\}\\x{FF5D}]", QRegularExpression::CaseInsensitiveOption);
    pattern.replace(re, "{InputName}");
    if (pattern.contains("{InputName}")) {
        pattern.replace("{InputName}", originalName);
    } else {
        pattern = originalName + "_" + pattern;
    }
    return pattern;
}

bool CoregistrationNode::isReady() const
{
    if (!m_inputData || m_inputData->filePaths().isEmpty()) return false;
    if (projectPath().isEmpty() || projectName().isEmpty()) return false;
    if (m_outputNodeName.trimmed().isEmpty()) return false;

    if (m_method == "Fine") {
        if (m_demPath.trimmed().isEmpty()) return false;
        if (!QFileInfo(m_demPath).exists()) return false;
    }

    return true;
}

void CoregistrationNode::processAutomatically()
{
    if (m_worker || m_thread) {
        deferAutomaticCompletion();
        return;
    }

    if (prepareToStart()) {
        executeProcessing();
    } else {
        setState(ExecutionState::Idle);
    }
}

void CoregistrationNode::execute()
{
    executeProcessing();
}

bool CoregistrationNode::prepareToStart()
{
    if (m_worker || m_thread) {
        return false;
    }
    if (!isReady()) {
        InSARLogManager::LogWarning("CoregistrationNode", "prepareToStart skipped: node not ready.");
        return false;
    }

    QStringList inputPaths = m_inputData->filePaths();
    QString projDir = getRealSavePath();
    QString nodeName = m_outputNodeName.trimmed();

    m_preparedOutputNames.clear();
    m_preparedH5Paths.clear();
    m_preparedJpgPaths.clear();

    for (const QString& path : inputPaths) {
        QString origName = QFileInfo(path).completeBaseName();
        QString outName = resolveOutputFileName(origName);
        if (!outName.endsWith(".h5", Qt::CaseInsensitive)) {
            outName += ".h5";
        }
        m_preparedOutputNames.append(QFileInfo(outName).completeBaseName());
        m_preparedH5Paths.append(projDir + "/" + nodeName + "/" + outName);
        m_preparedJpgPaths.append(projDir + "/" + nodeName + "/" + QFileInfo(outName).completeBaseName() + ".jpg");
    }

    auto* iface = NodeUtils::getProjectContext(_widget);

    if (_isAutoTriggered) {
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
    } else {
        m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(iface, nodeName, m_preparedH5Paths, nullptr);
    }

    return m_preparedOverwriteResult != NodeUtils::OverwriteResult::Cancel;
}

void CoregistrationNode::executeProcessing()
{
    InSARLogManager::LogInfo("CoregistrationNode", "executeProcessing started.");

    if (m_worker || m_thread) {
        stopExecution();
    }

    setProgress(0);

    m_savedOutputFiles = m_preparedOutputNames;
    m_outputImagePaths = m_preparedH5Paths;
    m_outputJpgPaths = m_preparedJpgPaths;

    auto* iface = NodeUtils::getProjectContext(_widget);
    QString nodeName = m_outputNodeName.trimmed();
    QStringList inputPaths = m_inputData->filePaths();
    QString projDir = getRealSavePath();
    QString projName = projectName();
    QStandardItemModel* model = projectModel();

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting) {
        if (validateAndRestoreOutput()) {
            setState(ExecutionState::Completed);
            finishExecution();
            return;
        } else {
            QMessageBox::warning(nullptr, QStringLiteral("警告"), QStringLiteral("加载已有文件失败，将开始重新计算！"));
        }
    }

    // Clean up old project tree items
    NodeUtils::removeDataNodeFromProject(iface, nodeName);

    m_worker = new CoregistrationWorker();
    m_thread = new QThread(this);
    m_worker->moveToThread(m_thread);

    m_worker->setDemPath(m_demPath);
    m_worker->setFilePattern(m_outputFileName);

    connect(m_worker, &CoregistrationWorker::updateProcess, this, &CoregistrationNode::onProgressUpdate, Qt::QueuedConnection);
    connect(m_worker, &CoregistrationWorker::endProcess, this, &CoregistrationNode::onProcessingFinished, Qt::QueuedConnection);
    connect(m_worker, &CoregistrationWorker::errorProcess, this, &CoregistrationNode::onError, Qt::QueuedConnection);
    connect(m_worker, &CoregistrationWorker::sendModel, this, &CoregistrationNode::onModelUpdated, Qt::QueuedConnection);

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);

    m_isExecuting = true;
    m_thread->start(QThread::LowestPriority);

    int masterIdx = m_defaultFirstMaster ? 1 : m_masterIndex;

    if (m_method == "Coarse") {
        QList<int> para;
        para.push_back(masterIdx);
        para.push_back(m_interpTimes);
        para.push_back(m_blockSize);
        para.push_back(inputPaths.size());

        // Invoke Regis slot inside worker thread
        QMetaObject::invokeMethod(m_worker, "Regis", Qt::QueuedConnection,
            Q_ARG(QList<int>, para),
            Q_ARG(QString, projDir),
            Q_ARG(QString, projName),
            Q_ARG(QString, m_inputData->nodeName()),
            Q_ARG(QString, nodeName),
            Q_ARG(QStandardItemModel*, model));
    } else {
        // Invoke DEMAssistCoregistration slot inside worker thread
        QMetaObject::invokeMethod(m_worker, "DEMAssistCoregistration", Qt::QueuedConnection,
            Q_ARG(int, masterIdx),
            Q_ARG(QString, projDir),
            Q_ARG(QString, projName),
            Q_ARG(QString, m_inputData->nodeName()),
            Q_ARG(QString, nodeName),
            Q_ARG(QStandardItemModel*, model));
    }

    updateParameterWidgetsEnableState();

    setState(ExecutionState::Running);
    deferAutomaticCompletion();

    InSARLogManager::LogInfo("CoregistrationNode", "executeProcessing thread started successfully.");
}

void CoregistrationNode::stopExecution()
{
    if (m_worker) {
        m_worker->StopProcess();
    }
    if (m_thread && m_thread->isRunning()) {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
    }
    m_isExecuting = false;
    m_worker = nullptr;
    m_thread = nullptr;
    updateParameterWidgetsEnableState();
    setState(ExecutionState::Stopped);
}

void CoregistrationNode::onProgressUpdate(int progress, const QString& message)
{
    Q_UNUSED(message);
    setProgress(progress);
}

void CoregistrationNode::onProcessingFinished()
{
    InSARLogManager::LogInfo("CoregistrationNode", "Coregistration process finished. Generating previews...");

    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
    }
    m_worker = nullptr;
    m_thread = nullptr;

    // Generate preview JPGs asynchronously
    QStringList h5Paths = m_outputImagePaths;
    QStringList jpgPaths = m_outputJpgPaths;

    m_remedyWatcher.cancel();
    m_remedyWatcher.waitForFinished();
    m_remedyWatcher.disconnect();

    connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this, [this, h5Paths, jpgPaths]() {
        InSARLogManager::LogInfo("CoregistrationNode", "Coregistration JPG preview generation finished.");
        m_outputData = std::make_shared<ImportedFileData>(h5Paths, m_outputNodeName.trimmed());
        m_previewData = std::make_shared<ImageInfoData>(jpgPaths);
        setOutputData(0, m_outputData);
        setOutputData(1, m_previewData);

        m_isExecuting = false;
        updateParameterWidgetsEnableState();

        Q_EMIT dataUpdated(0);
        Q_EMIT dataUpdated(1);
        finishExecution();
    });

    QFuture<void> future = QtConcurrent::run([h5Paths, jpgPaths]() {
        for (int i = 0; i < h5Paths.size(); ++i) {
            NodeUtils::generateJpgPreviewFromH5(h5Paths[i], jpgPaths[i], "complex");
        }
    });
    m_remedyWatcher.setFuture(future);
}

void CoregistrationNode::onError(const QString& error)
{
    InSARLogManager::LogError("CoregistrationNode", error);
    Q_EMIT executionError(error);
    setState(ExecutionState::Error);

    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
    }
    m_worker = nullptr;
    m_thread = nullptr;

    m_isExecuting = false;
    updateParameterWidgetsEnableState();

    m_outputData.reset();
    m_previewData.reset();
    m_outputImagePaths.clear();
    m_outputJpgPaths.clear();
    setOutputData(0, nullptr);
    setOutputData(1, nullptr);
}

void CoregistrationNode::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
    auto* iface = NodeUtils::getProjectContext(_widget);
    if (iface) {
        iface->refreshProjectTree();
    }
}

QJsonObject CoregistrationNode::save() const
{
    QJsonObject modelJson = ExecutableNodeDelegateModel::save();

    modelJson["method"] = m_method;
    modelJson["defaultFirstMaster"] = m_defaultFirstMaster;
    modelJson["masterIndex"] = m_masterIndex;
    modelJson["interpTimes"] = m_interpTimes;
    modelJson["blockSize"] = m_blockSize;
    modelJson["demPath"] = m_demPath;
    modelJson["outputNodeName"] = m_outputNodeName;
    modelJson["outputFileName"] = m_outputFileName;

    QJsonArray h5FilesArr;
    for (const QString& path : m_outputImagePaths) {
        h5FilesArr.append(QFileInfo(path).fileName());
    }
    modelJson["outputFiles"] = h5FilesArr;

    return modelJson;
}

void CoregistrationNode::load(QJsonObject const &json)
{
    m_method = json["method"].toString("Coarse");
    m_defaultFirstMaster = json["defaultFirstMaster"].toBool(true);
    m_masterIndex = json["masterIndex"].toInt(1);
    m_interpTimes = json["interpTimes"].toInt(4);
    m_blockSize = json["blockSize"].toInt(64);
    m_demPath = json["demPath"].toString("");
    m_outputNodeName = json["outputNodeName"].toString("Coregistration");
    m_outputFileName = json["outputFileName"].toString("{InputName}_regis");

    m_savedOutputFiles.clear();
    if (json.contains("outputFiles")) {
        QJsonArray arr = json["outputFiles"].toArray();
        for (int i = 0; i < arr.size(); ++i) {
            m_savedOutputFiles.append(arr[i].toString());
        }
    }

    ExecutableNodeDelegateModel::load(json);

    if (m_methodCombo) {
        m_methodCombo->setCurrentIndex(m_method == "Coarse" ? 0 : 1);
    }
    if (m_defaultFirstMasterCheckBox) {
        m_defaultFirstMasterCheckBox->setChecked(m_defaultFirstMaster);
    }
    if (m_interpCombo) {
        m_interpCombo->setCurrentText(QString::number(m_interpTimes));
    }
    if (m_blockSizeCombo) {
        m_blockSizeCombo->setCurrentText(QString::number(m_blockSize));
    }
    if (m_demPathEdit) {
        m_demPathEdit->setText(m_demPath);
    }
    if (m_outputNodeNameEdit) {
        m_outputNodeNameEdit->setText(m_outputNodeName);
    }
    if (m_outputFileNameEdit) {
        m_outputFileNameEdit->setText(m_outputFileName);
    }

    updateMasterImageCombo();
    updateWidgetSize();
}

bool CoregistrationNode::validateAndRestoreOutput()
{
    QString nodeName = m_outputNodeName.trimmed();
    if (nodeName.isEmpty()) return false;

    QString projDir = getRealSavePath();
    if (projDir.isEmpty()) return false;

    // Determine expected files
    QStringList expectedH5Paths;
    QStringList expectedJpgPaths;

    if (!m_savedOutputFiles.isEmpty()) {
        for (const QString& file : m_savedOutputFiles) {
            QString h5File = file;
            if (!h5File.endsWith(".h5", Qt::CaseInsensitive)) h5File += ".h5";
            expectedH5Paths.append(projDir + "/" + nodeName + "/" + h5File);
            expectedJpgPaths.append(projDir + "/" + nodeName + "/" + QFileInfo(h5File).completeBaseName() + ".jpg");
        }
    } else if (m_inputData) {
        // Fallback reconstruction
        for (const QString& path : m_inputData->filePaths()) {
            QString origName = QFileInfo(path).completeBaseName();
            QString outName = resolveOutputFileName(origName);
            if (!outName.endsWith(".h5", Qt::CaseInsensitive)) outName += ".h5";
            expectedH5Paths.append(projDir + "/" + nodeName + "/" + outName);
            expectedJpgPaths.append(projDir + "/" + nodeName + "/" + QFileInfo(outName).completeBaseName() + ".jpg");
        }
    } else {
        return false;
    }

    // Verify all H5 files exist
    for (const QString& path : expectedH5Paths) {
        bool exists = QFile::exists(path);
        if (!exists) return false;
    }

    m_outputImagePaths = expectedH5Paths;
    m_outputJpgPaths = expectedJpgPaths;

    // Asynchronous JPG preview generation
    QStringList missingH5s;
    QStringList missingJpgs;
    for (int i = 0; i < expectedH5Paths.size(); ++i) {
        if (!QFile::exists(expectedJpgPaths[i])) {
            missingH5s.append(expectedH5Paths[i]);
            missingJpgs.append(expectedJpgPaths[i]);
        }
    }

    if (!missingH5s.isEmpty()) {
        m_remedyWatcher.cancel();
        m_remedyWatcher.waitForFinished();
        m_remedyWatcher.disconnect();

        connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this, [this, expectedJpgPaths]() {
            InSARLogManager::LogInfo("CoregistrationNode", "Remedy preview generation finished.");
            m_previewData = std::make_shared<ImageInfoData>(expectedJpgPaths);
            setOutputData(1, m_previewData);
            Q_EMIT dataUpdated(1);
        });

        QFuture<void> future = QtConcurrent::run([missingH5s, missingJpgs]() {
            for (int i = 0; i < missingH5s.size(); ++i) {
                NodeUtils::generateJpgPreviewFromH5(missingH5s[i], missingJpgs[i], "complex");
            }
        });
        m_remedyWatcher.setFuture(future);
    } else {
        m_previewData = std::make_shared<ImageInfoData>(expectedJpgPaths);
        setOutputData(1, m_previewData);
    }

    m_outputData = std::make_shared<ImportedFileData>(expectedH5Paths, nodeName);
    setOutputData(0, m_outputData);
    Q_EMIT dataUpdated(0);
    if (missingH5s.isEmpty()) {
        Q_EMIT dataUpdated(1);
    }

    // Restore Standard Item Model Tree View
    QStandardItemModel* projModelPtr = projectModel();
    if (projModelPtr) {
        QList<QStandardItem*> foundProjects = projModelPtr->findItems(projectName());
        if (!foundProjects.isEmpty()) {
            QStandardItem* projectItem = foundProjects.first();

            QStandardItem* regisItem = nullptr;
            for (int i = 0; i < projectItem->rowCount(); i++) {
                if (projectItem->child(i, 0)->text() == nodeName) {
                    regisItem = projectItem->child(i, 0);
                    break;
                }
            }

            if (!regisItem) {
                regisItem = new QStandardItem(nodeName);
                regisItem->setToolTip(projectName());
                int insert = 0;
                for (; insert < projectItem->rowCount(); insert++) {
                    if (projectItem->child(insert, 1)->text().compare("complex-0.0") == 0 ||
                        projectItem->child(insert, 1)->text().compare("complex-1.0") == 0 ||
                        projectItem->child(insert, 1)->text().compare("complex-2.0") == 0)
                        continue;
                    else
                        break;
                }
                regisItem->setIcon(QIcon(FOLDER_ICON));
                projectItem->insertRow(insert, regisItem);
                QStandardItem* regisRank = new QStandardItem("complex-2.0");
                projectItem->setChild(insert, 1, regisRank);
            }

            for (const QString& h5Path : expectedH5Paths) {
                QFileInfo fileinfo(h5Path);
                QString regis_img_name = fileinfo.baseName();

                QStandardItem* item_img = nullptr;
                for (int j = 0; j < regisItem->rowCount(); j++) {
                    if (regisItem->child(j, 0)->text() == regis_img_name) {
                        item_img = regisItem->child(j, 0);
                        break;
                    }
                }

                if (!item_img) {
                    QStandardItem* regis_images_name = new QStandardItem(regis_img_name);
                    regis_images_name->setToolTip("complex");
                    QStandardItem* regis_images_path = new QStandardItem(fileinfo.absoluteFilePath());
                    regis_images_name->setIcon(QIcon(IMAGEDATA_ICON));
                    regisItem->appendRow(regis_images_name);
                    regisItem->setChild(regisItem->rowCount() - 1, 1, regis_images_path);
                } else {
                    regisItem->setChild(item_img->row(), 1, new QStandardItem(fileinfo.absoluteFilePath()));
                }
            }
        }
    }

    // Safe XML restoration using native TinyXML API
    XMLFile* xml = projectXml();
    if (xml) {
        bool xmlModified = false;
        TiXmlElement* root = nullptr;
        xml->get_root(root);
        if (root) {
            bool dataNodeExists = false;
            for (TiXmlElement* p = root->FirstChildElement(); p != nullptr; p = p->NextSiblingElement()) {
                const char* nameAttr = p->Attribute("name");
                if (nameAttr && strcmp(p->Value(), "DataNode") == 0 && QString(nameAttr) == nodeName) {
                    dataNodeExists = true;
                    break;
                }
            }

            if (!dataNodeExists && m_inputData) {
                QStringList inputPaths = m_inputData->filePaths();
                TiXmlElement* dataNodeElem = new TiXmlElement("DataNode");
                dataNodeElem->SetAttribute("name", nodeName.toStdString().c_str());
                dataNodeElem->SetAttribute("data_count", QString::number(inputPaths.size()).toStdString().c_str());
                dataNodeElem->SetAttribute("data_processing", "coregistration");
                dataNodeElem->SetAttribute("rank", "complex-2.0");

                int index = 1;
                TiXmlElement* root_child = root->FirstChildElement();
                if (root_child) {
                    root_child = root_child->NextSiblingElement(); // skip project_info
                }

                TiXmlElement* insertBeforeNode = nullptr;
                for (TiXmlElement* p = root_child; p != nullptr; p = p->NextSiblingElement(), index++) {
                    const char* rankAttr = p->Attribute("rank");
                    if (rankAttr && (strcmp(rankAttr, "complex-0.0") == 0 ||
                                     strcmp(rankAttr, "complex-1.0") == 0 ||
                                     strcmp(rankAttr, "complex-2.0") == 0)) {
                        continue;
                    } else {
                        insertBeforeNode = p;
                        break;
                    }
                }
                dataNodeElem->SetAttribute("index", QString::number(index).toStdString().c_str());

                NodeUtils::Hdf5Locker locker;
                FormatConversion FC;
                Utils util;
                
                Mat State_Vec_Master, Lon_Coeff_Master, Lat_Coeff_Master;
                Mat tmp_double = Mat::zeros(1, 1, CV_64FC1);
                double interp_interval = 0;
                double time_Master = 0;
                string time_master_str;

                int masterIdx = m_defaultFirstMaster ? 1 : m_masterIndex;
                if (masterIdx < 1 || masterIdx > inputPaths.size()) masterIdx = 1;

                QString temporal_baseline, B_parallel, B_effect;

                if (m_method == "Coarse") {
                    QString masterPath = inputPaths.at(masterIdx - 1);
                    NodeUtils::readMatFromH5(masterPath, "state_vec", State_Vec_Master, CV_64F);
                    NodeUtils::readMatFromH5(masterPath, "lon_coefficient", Lon_Coeff_Master, CV_64F);
                    NodeUtils::readMatFromH5(masterPath, "lat_coefficient", Lat_Coeff_Master, CV_64F);
                    
                    double prf = 0.0;
                    NodeUtils::readScalarFromH5(masterPath, "prf", prf);
                    if (prf != 0) {
                        interp_interval = 1 / prf;
                    }
                    NodeUtils::readStringFromH5(masterPath, "acquisition_start_time", time_master_str);
                    FC.utc2gps(time_master_str.c_str(), &time_Master);

                    // Get Rows and Cols from master regis H5
                    int Rows = 0, Cols = 0;
                    QString masterRegisName = resolveOutputFileName(QFileInfo(masterPath).completeBaseName());
                    if (!masterRegisName.endsWith(".h5", Qt::CaseInsensitive)) masterRegisName += ".h5";
                    QString masterRegisPath = projDir + "/" + nodeName + "/" + masterRegisName;
                    NodeUtils::readScalarFromH5(masterRegisPath, "azimuth_len", Rows);
                    NodeUtils::readScalarFromH5(masterRegisPath, "range_len", Cols);

                    for (int i = 0; i < inputPaths.size(); i++) {
                        if (i == masterIdx - 1) {
                            temporal_baseline += "0 ";
                            B_parallel += "0 ";
                            B_effect += "0 ";
                        } else {
                            Mat State_Vec_Slave, Lon_Coeff_Slave, Lat_Coeff_Slave;
                            double interp_interval_slave = 0;
                            double V_baseline = 0, H_baseline = 0;
                            double sigma_V = 0, sigma_H = 0;
                            double time_Slave = 0;
                            string time_slave_str;
                            QString slavePath = inputPaths.at(i);
                            NodeUtils::readMatFromH5(slavePath, "state_vec", State_Vec_Slave, CV_64F);
                            NodeUtils::readMatFromH5(slavePath, "lon_coefficient", Lon_Coeff_Slave, CV_64F);
                            NodeUtils::readMatFromH5(slavePath, "lat_coefficient", Lat_Coeff_Slave, CV_64F);
                            
                            double prf_slave = 0.0;
                            NodeUtils::readScalarFromH5(slavePath, "prf", prf_slave);
                            if (prf_slave != 0) {
                                interp_interval_slave = 1 / prf_slave;
                            }
                            NodeUtils::readStringFromH5(slavePath, "acquisition_start_time", time_slave_str);
                            FC.utc2gps(time_slave_str.c_str(), &time_Slave);
                            double delta = (time_Slave - time_Master) / 60 / 60 / 24;
                            char tmp_d2s[512];
                            sprintf_s(tmp_d2s, "%.4f", delta);
                            temporal_baseline += QString("%1 ").arg(QString(tmp_d2s));

                            int offset_row = 0, offset_col = 0;
                            QString regisName = resolveOutputFileName(QFileInfo(slavePath).completeBaseName());
                            if (!regisName.endsWith(".h5", Qt::CaseInsensitive)) regisName += ".h5";
                            QString regisPath = projDir + "/" + nodeName + "/" + regisName;
                            NodeUtils::readScalarFromH5(regisPath, "offset_row", offset_row);
                            NodeUtils::readScalarFromH5(regisPath, "offset_col", offset_col);

                            util.baseline_estimation(State_Vec_Master, State_Vec_Slave, Lon_Coeff_Master, Lat_Coeff_Master,
                                offset_row, offset_col, Rows, Cols, interp_interval, interp_interval_slave, &V_baseline, &H_baseline, &sigma_V, &sigma_H);

                            sprintf_s(tmp_d2s, "%.2f", V_baseline);
                            B_parallel += QString("%1 ").arg(QString(tmp_d2s));
                            sprintf_s(tmp_d2s, "%.2f", H_baseline);
                            B_effect += QString("%1 ").arg(QString(tmp_d2s));
                        }
                    }
                } else {
                    for (int i = 0; i < inputPaths.size(); ++i) {
                        temporal_baseline += "0 ";
                        B_parallel += "0 ";
                        B_effect += "0 ";
                    }
                }

                for (int i = 0; i < inputPaths.size(); i++) {
                    QString origName = QFileInfo(inputPaths[i]).completeBaseName();
                    QString outName = resolveOutputFileName(origName);
                    if (!outName.endsWith(".h5", Qt::CaseInsensitive)) outName += ".h5";
                    QString relativePath = QString("/%1/%2").arg(nodeName).arg(outName);
                    QString outH5Path = projDir + "/" + nodeName + "/" + outName;

                    int rowOffset = 0, colOffset = 0;
                    NodeUtils::readScalarFromH5(outH5Path, "offset_row", rowOffset);
                    NodeUtils::readScalarFromH5(outH5Path, "offset_col", colOffset);

                    TiXmlElement* dataElem = new TiXmlElement("Data");

                    TiXmlElement* dataNameNode = new TiXmlElement("Data_Name");
                    dataNameNode->LinkEndChild(new TiXmlText(QFileInfo(outName).completeBaseName().toStdString().c_str()));
                    dataElem->LinkEndChild(dataNameNode);

                    TiXmlElement* dataRankNode = new TiXmlElement("Data_Rank");
                    dataRankNode->LinkEndChild(new TiXmlText("complex-2.0"));
                    dataElem->LinkEndChild(dataRankNode);

                    TiXmlElement* dataIndexNode = new TiXmlElement("Data_Index");
                    dataIndexNode->LinkEndChild(new TiXmlText(QString::number(i + 1).toStdString().c_str()));
                    dataElem->LinkEndChild(dataIndexNode);

                    TiXmlElement* dataPathNode = new TiXmlElement("Data_Path");
                    dataPathNode->LinkEndChild(new TiXmlText(relativePath.toStdString().c_str()));
                    dataElem->LinkEndChild(dataPathNode);

                    TiXmlElement* rowOffsetNode = new TiXmlElement("Row_Offset");
                    rowOffsetNode->LinkEndChild(new TiXmlText(QString::number(rowOffset).toStdString().c_str()));
                    dataElem->LinkEndChild(rowOffsetNode);

                    TiXmlElement* colOffsetNode = new TiXmlElement("Col_Offset");
                    colOffsetNode->LinkEndChild(new TiXmlText(QString::number(colOffset).toStdString().c_str()));
                    dataElem->LinkEndChild(colOffsetNode);

                    dataNodeElem->LinkEndChild(dataElem);
                }

                TiXmlElement* paramsElem = new TiXmlElement("Data_Processing_Parameters");

                TiXmlElement* masterImgNode = new TiXmlElement("master_image");
                masterImgNode->LinkEndChild(new TiXmlText(QString::number(masterIdx).toStdString().c_str()));
                paramsElem->LinkEndChild(masterImgNode);

                TiXmlElement* blocksizeNode = new TiXmlElement("blocksize");
                blocksizeNode->LinkEndChild(new TiXmlText(QString::number(m_blockSize).toStdString().c_str()));
                paramsElem->LinkEndChild(blocksizeNode);

                TiXmlElement* interpTimesNode = new TiXmlElement("interp_times");
                interpTimesNode->LinkEndChild(new TiXmlText(QString::number(m_interpTimes).toStdString().c_str()));
                paramsElem->LinkEndChild(interpTimesNode);

                TiXmlElement* tempBaselineNode = new TiXmlElement("temporal_baseline_distribution");
                tempBaselineNode->SetAttribute("unit", "day");
                tempBaselineNode->LinkEndChild(new TiXmlText(temporal_baseline.toStdString().c_str()));
                paramsElem->LinkEndChild(tempBaselineNode);

                TiXmlElement* effectBaselineNode = new TiXmlElement("effect_baseline_distribution");
                effectBaselineNode->SetAttribute("unit", "m");
                effectBaselineNode->LinkEndChild(new TiXmlText(B_effect.toStdString().c_str()));
                paramsElem->LinkEndChild(effectBaselineNode);

                TiXmlElement* parallelBaselineNode = new TiXmlElement("parallel_baseline_distribution");
                parallelBaselineNode->SetAttribute("unit", "m");
                parallelBaselineNode->LinkEndChild(new TiXmlText(B_parallel.toStdString().c_str()));
                paramsElem->LinkEndChild(parallelBaselineNode);

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

                xmlModified = true;
            }
        }
        if (xmlModified) {
            QString xmlPath = projectPath() + "/" + projectName();
            xml->XMLFile_save(xmlPath.toStdString().c_str());
        }
    }

    auto* iface = NodeUtils::getProjectContext(_widget);
    if (iface) {
        iface->refreshProjectTree();
    }

    return true;
}

QStandardItemModel* CoregistrationNode::projectModel() const
{
    auto* iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectModel() : nullptr;
}

QString CoregistrationNode::projectPath() const
{
    auto* iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectPath() : QString();
}

QString CoregistrationNode::projectName() const
{
    auto* iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectName() : QString();
}

XMLFile* CoregistrationNode::projectXml() const
{
    auto* iface = NodeUtils::getProjectContext(_widget);
    return iface ? iface->projectXml() : nullptr;
}

} // namespace QtNodes
