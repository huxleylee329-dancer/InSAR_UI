#include "PSCandidateNode.h"
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

PSCandidateNode::PSCandidateNode()
    : _widget(nullptr)
    , m_daThreshold(0.4)
    , m_minPsCount(1000)
    , m_multilookRg(1)
    , m_multilookAz(1)
    , m_outputNodeName("PS_Candidates")
    , m_worker(nullptr)
    , m_thread(nullptr)
{
    setState(ExecutionState::Idle);
}

PSCandidateNode::~PSCandidateNode()
{
    if (m_worker) m_worker->StopProcess();
    if (m_thread && m_thread->isRunning()) {
        m_thread->requestInterruption();
        m_thread->quit();
        m_thread->wait();
    }
}

unsigned int PSCandidateNode::nPorts(PortType portType) const
{
    if (portType == PortType::In)
        return 1;
    else
        return 2;
}

NodeDataType PSCandidateNode::dataType(PortType portType, PortIndex portIndex) const
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

bool PSCandidateNode::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    return true;
}

QString PSCandidateNode::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::In)
        return QStringLiteral("输入影像(SLC)");
    else {
        if (portIndex == 0)
            return tr("候选点成果 *");
        else
            return tr("离差分布预览 ?");
    }
}

bool PSCandidateNode::portIsOptional(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out && portIndex == 1)
        return true;
    return false;
}

std::shared_ptr<NodeData> PSCandidateNode::outData(PortIndex port)
{
    if (executionState() != ExecutionState::Completed)
        return nullptr;
    if (port == 0)
        return m_outputData;
    else
        return m_previewData;
}

void PSCandidateNode::setInData(std::shared_ptr<NodeData> data, PortIndex port)
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

::QWidget* PSCandidateNode::embeddedWidget()
{
    if (!_widget)
    {
        createWidget();
    }
    return _widget;
}

void PSCandidateNode::setExecutionMode(ExecutionMode mode)
{
    ExecutableNodeDelegateModel::setExecutionMode(mode);
}

void PSCandidateNode::createWidget()
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

    m_daThresholdEdit = new QLineEdit(QString::number(m_daThreshold));
    addParamRow(QStringLiteral("振幅离差阈值:"), m_daThresholdEdit);

    m_minPsCountEdit = new QLineEdit(QString::number(m_minPsCount));
    addParamRow(QStringLiteral("最小PS点数:"), m_minPsCountEdit);

    m_multilookRgEdit = new QLineEdit(QString::number(m_multilookRg));
    addParamRow(QStringLiteral("距离向多视:"), m_multilookRgEdit);

    m_multilookAzEdit = new QLineEdit(QString::number(m_multilookAz));
    addParamRow(QStringLiteral("方位向多视:"), m_multilookAzEdit);

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
    connect(m_daThresholdEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_daThreshold = text.toDouble(); invalidateExecution();
    });
    connect(m_minPsCountEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_minPsCount = text.toInt(); invalidateExecution();
    });
    connect(m_multilookRgEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_multilookRg = text.toInt(); invalidateExecution();
    });
    connect(m_multilookAzEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_multilookAz = text.toInt(); invalidateExecution();
    });
    connect(m_outputNodeNameEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_outputNodeName = text; invalidateExecution();
    });

    updateLabels(); // Widget initial label update (SOP rule 12)
}

void PSCandidateNode::updateLabels()
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

void PSCandidateNode::updateWidgetSize()
{
    if (_widget) {
        _widget->setFixedWidth(300);
        _widget->adjustSize();
        Q_EMIT embeddedWidgetSizeUpdated();
    }
}

bool PSCandidateNode::validateInputs() const
{
    if (!m_inputData || m_inputData->filePaths().isEmpty())
        return false;
    if (m_outputNodeName.isEmpty())
        return false;
    if (m_daThreshold <= 0.0 || m_minPsCount <= 0)
        return false;
    return true;
}

bool PSCandidateNode::prepareToStart()
{
    if (!validateInputs()) {
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::NoConflict;
        setState(ExecutionState::Error);
        return false;
    }

    if (isAutoTriggered()) {
        m_preparedOverwriteResult = NodeUtils::OverwriteResult::Overwrite;
        return true;
    }

    // 检查并提示覆盖（仅在手动执行模态下，避免后台自动连续运行挂起）
    if (executionMode() == ExecutionMode::Manual) {
        QString rawPath = projectPath();
        QString dir = rawPath.endsWith(".insar", Qt::CaseInsensitive)
                      ? QFileInfo(rawPath).absolutePath()
                      : rawPath;
        QString outDir = dir + "/" + m_outputNodeName;
        QString h5Path = outDir + "/PS_candidates.h5";

        QStringList pathsToCheck;
        pathsToCheck << h5Path;
        m_preparedOverwriteResult = NodeUtils::checkAndPromptOverwrite(
            NodeUtils::getProjectContext(_widget), m_outputNodeName, pathsToCheck
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

void PSCandidateNode::execute()
{
    if (m_preparedOverwriteResult == NodeUtils::OverwriteResult::LoadExisting) {
        validateAndRestoreOutput();
        return;
    }

    // 清除旧的项目子节点及 XML 条目，防止 UI 树和项目 XML 的多重重影 bug
    NodeUtils::removeDataNodeFromProject(NodeUtils::getProjectContext(_widget), m_outputNodeName);

    executeProcessing();
}

void PSCandidateNode::executeProcessing()
{
    InSARLogManager::LogInfo("PSCandidateNode", "executeProcessing started.");
    stopExecution();

    m_thread = new QThread(this);
    m_worker = new PSCandidateWorker();
    m_worker->moveToThread(m_thread);

    QString projPath = projectPath();
    QString projName = projectName();
    QStringList slcList = m_inputData ? m_inputData->filePaths() : QStringList();

    connect(m_thread, &QThread::started, m_worker, [this, projPath, projName, slcList]() {
        m_worker->select_candidates(
            m_daThreshold,
            m_minPsCount,
            m_multilookRg,
            m_multilookAz,
            projPath,
            projName,
            m_outputNodeName,
            slcList
        );
    });

    connect(m_worker, &PSCandidateWorker::updateProcess, this, &PSCandidateNode::onProgressUpdate);
    connect(m_worker, &PSCandidateWorker::endProcess, this, &PSCandidateNode::onProcessingFinished);
    connect(m_worker, &PSCandidateWorker::endProcess, m_thread, &QThread::quit);
    connect(m_worker, &PSCandidateWorker::errorProcess, this, &PSCandidateNode::onError);
    connect(m_worker, &PSCandidateWorker::errorProcess, m_thread, &QThread::quit);
    connect(m_worker, &PSCandidateWorker::cancelled, this, &PSCandidateNode::onCancelled);
    connect(m_worker, &PSCandidateWorker::cancelled, m_thread, &QThread::quit);

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

void PSCandidateNode::stopExecution()
{
    if (m_worker) {
        m_worker->StopProcess();
    }
}

void PSCandidateNode::onProgressUpdate(int progress, const QString& message)
{
    if (isAutomaticExecutionObsolete()) {
        return;
    }

    setProgress(progress);
    if (m_resultLabel) {
        m_resultLabel->setText(QStringLiteral("正在计算 (%1%): %2").arg(progress).arg(message));
    }
}

void PSCandidateNode::onError(const QString& error)
{
    m_worker = nullptr;
    m_thread = nullptr;
    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    InSARLogManager::LogError("PSCandidateNode", "Error in candidate selection: " + error);
    if (m_resultLabel) {
        m_resultLabel->setText(QStringLiteral("计算出错: ") + error);
    }
    setState(ExecutionState::Error);
    finishExecution();
}

void PSCandidateNode::onCancelled()
{
    InSARLogManager::LogInfo("PSCandidateNode", "PS candidate selection cancellation cleanup completed.");
    m_worker = nullptr;
    m_thread = nullptr;
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

void PSCandidateNode::onProcessingFinished()
{
    m_worker = nullptr;
    m_thread = nullptr;
    if (discardObsoleteAutomaticExecution()) {
        return;
    }

    InSARLogManager::LogInfo("PSCandidateNode", "executeProcessing completed successfully.");
    
    QString rawPath = projectPath();
    QString dir = rawPath.endsWith(".insar", Qt::CaseInsensitive)
                  ? QFileInfo(rawPath).absolutePath()
                  : rawPath;
    QString outDir = dir + "/" + m_outputNodeName;
    QString h5Path = outDir + "/PS_candidates.h5";

    // 注册生成的数据节点到项目树中
    IApplicationInterface* iface = NodeUtils::getProjectContext(_widget);
    if (iface) {
        QStandardItemModel* model = iface->projectModel();
        if (model) {
            QList<QStandardItem*> found = model->findItems(projectName());
            if (!found.isEmpty()) {
                QStandardItem* projectItem = found.first();
                NodeUtils::findOrCreateProjectNode(projectItem, m_outputNodeName, "mask-1.0");
                iface->refreshProjectTree();
            }
        }
    }

    m_outputData = std::make_shared<ImportedFileData>(QStringList() << h5Path, m_outputNodeName);
    
    if (m_resultLabel) {
        m_resultLabel->setText(QStringLiteral("计算成功！结果保存在: ") + m_outputNodeName);
    }

    generateStaticPreviewJpg();

    setState(ExecutionState::Running);
    finishExecution();
    updateLabels();

    Q_EMIT dataUpdated(0);
}

bool PSCandidateNode::validateAndRestoreOutput()
{
    QString rawPath = projectPath();
    QString dir = rawPath.endsWith(".insar", Qt::CaseInsensitive)
                  ? QFileInfo(rawPath).absolutePath()
                  : rawPath;
    QString h5Path = dir + "/" + m_outputNodeName + "/PS_candidates.h5";

    if (QFileInfo::exists(h5Path)) {
        m_outputData = std::make_shared<ImportedFileData>(QStringList() << h5Path, m_outputNodeName);
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

void PSCandidateNode::generateStaticPreviewJpg()
{
    QString rawPath = projectPath();
    QString dir = rawPath.endsWith(".insar", Qt::CaseInsensitive)
                  ? QFileInfo(rawPath).absolutePath()
                  : rawPath;
    QString outDir = dir + "/" + m_outputNodeName;
    QString h5Path = outDir + "/PS_candidates.h5";
    QString jpgPath = outDir + "/amplitude_dispersion.jpg";

    if (!QFileInfo::exists(h5Path)) return;

    QFutureWatcher<void>* watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher, jpgPath]() {
        if (QFileInfo::exists(jpgPath)) {
            m_previewData = std::make_shared<ImageInfoData>(jpgPath);
            Q_EMIT dataUpdated(1);
        }
        watcher->deleteLater();
    });

    watcher->setFuture(QtConcurrent::run([h5Path, jpgPath]() {
        NodeUtils::Hdf5Locker locker; // SOP: HDF5 lock protection
        FormatConversion FC;
        Utils util;
        cv::Mat dispersion, mask;
        int ret = (NodeUtils::readMatFromH5(h5Path, "amplitude_dispersion", dispersion, CV_64F) &&
                   NodeUtils::readMatFromH5(h5Path, "ps_mask", mask)) ? 0 : -1;
        if (ret == 0 && !dispersion.empty()) {
            util.savephase_white(jpgPath.toStdString().c_str(), "jet", dispersion, mask);
        }
    }));
}

QStringList PSCandidateNode::previewImagePaths() const
{
    QString rawPath = projectPath();
    QString dir = rawPath.endsWith(".insar", Qt::CaseInsensitive)
                  ? QFileInfo(rawPath).absolutePath()
                  : rawPath;
    QString jpgPath = dir + "/" + m_outputNodeName + "/amplitude_dispersion.jpg";
    QStringList paths;
    if (QFileInfo::exists(jpgPath)) {
        paths << jpgPath;
    }
    return paths;
}

QJsonObject PSCandidateNode::save() const
{
    QJsonObject root = ExecutableNodeDelegateModel::save();
    root["daThreshold"] = m_daThreshold;
    root["minPsCount"] = m_minPsCount;
    root["multilookRg"] = m_multilookRg;
    root["multilookAz"] = m_multilookAz;
    root["outputNodeName"] = m_outputNodeName;
    return root;
}

void PSCandidateNode::load(QJsonObject const& json)
{
    m_daThreshold = json["daThreshold"].toDouble(0.4);
    m_minPsCount = json["minPsCount"].toInt(1000);
    m_multilookRg = json["multilookRg"].toInt(1);
    m_multilookAz = json["multilookAz"].toInt(1);
    m_outputNodeName = json["outputNodeName"].toString(QStringLiteral("PS_Candidates"));

    // Call base class load AFTER initializing local fields, to ensure validateAndRestoreOutput works during project load
    ExecutableNodeDelegateModel::load(json);
}

QString PSCandidateNode::projectPath() const
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

QString PSCandidateNode::projectName() const
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

void PSCandidateNode::processAutomatically()
{
    if (prepareToStart()) {
        executeProcessing();
    } else {
        setState(ExecutionState::Idle);
    }
}

} // namespace QtNodes
