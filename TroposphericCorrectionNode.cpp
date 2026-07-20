#include "TroposphericCorrectionNode.h"
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
    if (portType == PortType::In) return 2;
    else return 2;
}

NodeDataType TroposphericCorrectionNode::dataType(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portIndex);
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
        if (portIndex == 0) return QStringLiteral("干涉图");
        else return QStringLiteral("DEM ?");
    } else {
        if (portIndex == 0) return QStringLiteral("成果 *");
        else return QStringLiteral("预览 ?");
    }
    return QString();
}

bool TroposphericCorrectionNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In && portIndex == 1) return true;
    if (portType == PortType::Out && portIndex == 1) return true;
    return false;
}

void TroposphericCorrectionNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
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
    m_preparedDstNode = m_outputNodeNameEdit->text().trimmed().isEmpty()
        ? generateDefaultOutputName() : m_outputNodeNameEdit->text().trimmed();
    m_preparedSavePath = projectPath();
    m_preparedProjectName = projectName();
    m_preparedSrcNode = m_inputData->nodeName();
    m_preparedEra5Dir = m_era5DirEdit ? m_era5DirEdit->text().trimmed() : m_era5Dir;

    QStringList pathsToCheck;
    for (const QString& srcPath : m_inputData->filePaths()) {
        QFileInfo fi(srcPath);
        pathsToCheck.append(m_preparedSavePath + "/" + m_preparedDstNode + "/" + fi.baseName() + "_tropo.h5");
    }

    if (_isAutoTriggered) m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
    else m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(
        NodeUtils::getProjectContext(_widget), m_preparedDstNode, pathsToCheck, nullptr);

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

    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::Overwrite)
        NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), m_preparedDstNode);

    setProgress(0);
    setState(ExecutionState::Running);

    m_thread = new QThread();
    m_workerThread = new TroposphericCorrectionWorker();
    m_workerThread->moveToThread(m_thread);

    connect(this, &TroposphericCorrectionNode::startCorrection, m_workerThread, &TroposphericCorrectionWorker::doCorrection);
    connect(m_thread, &QThread::started, this, [this]() {
        Q_EMIT startCorrection(m_preparedEra5Dir, m_preparedSavePath, m_preparedProjectName,
            m_preparedSrcNode, m_preparedDstNode, projectModel());
    });
    connect(m_workerThread, &TroposphericCorrectionWorker::updateProcess, this, &TroposphericCorrectionNode::onProgressUpdate);
    connect(m_workerThread, &TroposphericCorrectionWorker::endProcess, this, &TroposphericCorrectionNode::onProcessingFinished);
    connect(m_workerThread, &TroposphericCorrectionWorker::errorProcess, this, &TroposphericCorrectionNode::onError);
    connect(m_workerThread, &TroposphericCorrectionWorker::cancelled, this, &TroposphericCorrectionNode::onCancelled);
    connect(m_workerThread, &TroposphericCorrectionWorker::cancelled, m_thread, &QThread::quit);
    connect(m_workerThread, &TroposphericCorrectionWorker::sendModel, this, &TroposphericCorrectionNode::onModelUpdated);
    connect(m_workerThread, &TroposphericCorrectionWorker::destroyed, m_thread, &QThread::quit);
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
    QString dstNode = m_outputNodeNameEdit->text().trimmed().isEmpty()
        ? generateDefaultOutputName() : m_outputNodeNameEdit->text().trimmed();
    QString outputPath = projectPath() + "/" + dstNode + "/";
    QStringList h5Paths, jpgPaths, types;
    QDir dir(outputPath);
    if (dir.exists()) {
        QStringList filters; filters << "*.h5";
        for (const QString& f : dir.entryList(filters, QDir::Files)) {
            h5Paths.append(dir.absoluteFilePath(f));
            jpgPaths.append(outputPath + QFileInfo(f).baseName() + ".jpg");
            types.append("phase");
        }
    }

    if (m_thread) { m_thread->quit(); m_thread->wait(); m_thread->deleteLater(); m_thread = nullptr; }
    if (m_workerThread) { m_workerThread->deleteLater(); m_workerThread = nullptr; }

    if (discardObsoleteAutomaticExecution()) return;

    m_outputData = std::make_shared<ImportedFileData>(h5Paths, dstNode);
    setOutputData(0, m_outputData);

    if (!h5Paths.isEmpty()) {
        m_remedyWatcher.cancel(); m_remedyWatcher.waitForFinished(); m_remedyWatcher.disconnect();
        connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this, [this, jpgPaths]() {
            if (discardObsoleteAutomaticExecution()) return;
            m_imageInfoData = std::make_shared<ImageInfoData>(jpgPaths);
            setOutputData(1, m_imageInfoData); Q_EMIT dataUpdated(1);
            m_outputNodeNameEdit->setEnabled(true); m_era5DirEdit->setEnabled(true); m_browseBtn->setEnabled(true);
            setState(ExecutionState::Running); setProgress(100); finishExecution(); Q_EMIT dataUpdated(0);
        });
        QFuture<void> future = QtConcurrent::run([h5Paths, jpgPaths, types]() {
            for (int i = 0; i < h5Paths.size(); ++i) NodeUtils::generateJpgPreviewFromH5(h5Paths[i], jpgPaths[i], types[i]);
        });
        m_remedyWatcher.setFuture(future);
    } else {
        m_imageInfoData.reset(); setOutputData(1, nullptr); Q_EMIT dataUpdated(1);
        m_outputNodeNameEdit->setEnabled(true); m_era5DirEdit->setEnabled(true); m_browseBtn->setEnabled(true);
        setState(ExecutionState::Running); setProgress(100); finishExecution(); Q_EMIT dataUpdated(0);
    }
}

void TroposphericCorrectionNode::onError(const QString& error)
{
    Q_UNUSED(error);
    if (m_thread) { m_thread->quit(); m_thread->wait(); m_thread->deleteLater(); m_thread = nullptr; }
    if (m_workerThread) { m_workerThread->deleteLater(); m_workerThread = nullptr; }
    if (discardObsoleteAutomaticExecution()) return;
    m_outputNodeNameEdit->setEnabled(true); m_era5DirEdit->setEnabled(true); m_browseBtn->setEnabled(true);
    setState(ExecutionState::Error);
}

void TroposphericCorrectionNode::onCancelled()
{
    if (m_thread) { m_thread->quit(); m_thread->wait(); m_thread->deleteLater(); m_thread = nullptr; }
    if (m_workerThread) { m_workerThread->deleteLater(); m_workerThread = nullptr; }
    if (discardObsoleteAutomaticExecution()) return;

    setState(ExecutionState::Stopped);
    Q_EMIT executionStopped();
    Q_EMIT computingFinished();
    m_outputNodeNameEdit->setEnabled(true); m_era5DirEdit->setEnabled(true); m_browseBtn->setEnabled(true);
}

void TroposphericCorrectionNode::onModelUpdated(QStandardItemModel* model)
{
    if (isAutomaticExecutionObsolete()) return;
    Q_UNUSED(model);
    auto iface = NodeUtils::getProjectContext(_widget);
    if (iface) iface->refreshProjectTree();
}

bool TroposphericCorrectionNode::validateAndRestoreOutput()
{
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty()) return false;
    QString outputPath = projectPath() + "/" + dstNode + "/";
    QDir dir(outputPath);
    if (!dir.exists()) return false;
    QStringList filters; filters << "*.h5";
    QStringList h5Files = dir.entryList(filters, QDir::Files);
    if (h5Files.isEmpty()) return false;

    QStringList h5Paths, expectedJpgPaths, types;
    for (const QString& f : h5Files) {
        h5Paths.append(dir.absoluteFilePath(f));
        expectedJpgPaths.append(outputPath + QFileInfo(f).baseName() + ".jpg");
        types.append("phase");
    }

    m_outputData = std::make_shared<ImportedFileData>(h5Paths, dstNode);
    setOutputData(0, m_outputData); Q_EMIT dataUpdated(0);

    QStringList missingH5s, missingJpgs, missingTypes, existingJpgs;
    for (int i = 0; i < expectedJpgPaths.size(); ++i) {
        if (QFile::exists(expectedJpgPaths[i])) existingJpgs.append(expectedJpgPaths[i]);
        else { missingH5s.append(h5Paths[i]); missingJpgs.append(expectedJpgPaths[i]); missingTypes.append(types[i]); }
    }
    if (missingH5s.isEmpty()) {
        m_imageInfoData = std::make_shared<ImageInfoData>(existingJpgs);
        setOutputData(1, m_imageInfoData); Q_EMIT dataUpdated(1);
    } else {
        m_remedyWatcher.cancel(); m_remedyWatcher.waitForFinished(); m_remedyWatcher.disconnect();
        connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this, [this, expectedJpgPaths]() {
            m_imageInfoData = std::make_shared<ImageInfoData>(expectedJpgPaths);
            setOutputData(1, m_imageInfoData); Q_EMIT dataUpdated(1);
        });
        QFuture<void> future = QtConcurrent::run([missingH5s, missingJpgs, missingTypes]() {
            for (int i = 0; i < missingH5s.size(); ++i) NodeUtils::generateJpgPreviewFromH5(missingH5s[i], missingJpgs[i], missingTypes[i]);
        });
        m_remedyWatcher.setFuture(future);
    }
    return true;
}

QStringList TroposphericCorrectionNode::previewImagePaths() const
{
    QStringList list;
    QString dstNode = m_outputNodeName.trimmed();
    if (dstNode.isEmpty()) return list;
    QString outputPath = projectPath() + "/" + dstNode + "/";
    QDir dir(outputPath);
    if (dir.exists()) {
        QStringList filters; filters << "*.h5";
        for (const QString& f : dir.entryList(filters, QDir::Files)) {
            QString jpg = outputPath + QFileInfo(f).baseName() + ".jpg";
            if (QFile::exists(jpg)) list.append(jpg);
        }
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

void TroposphericCorrectionNode::execute() { executeProcessing(); }

void TroposphericCorrectionNode::stopExecution()
{
    if (m_thread && m_thread->isRunning()) { m_thread->requestInterruption(); m_thread->quit(); m_thread->wait(); }
}

void TroposphericCorrectionNode::processAutomatically()
{
    if (prepareToStart()) executeProcessing();
    else setState(ExecutionState::Idle);
}

} // namespace QtNodes
