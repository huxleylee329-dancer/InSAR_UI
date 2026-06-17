#include "ImportNodeBase.h"
#include "ImportDataTypes.h"
#include "BaseImportWorker.h"
#include "ImportTask.h"
#include "IApplicationInterface.h"
#include "MainWindow.h"
#include "InterfaceManager.h"
#include "WorkspaceUI.h"
#include "NodeUtils.h"
#include <QApplication>
#include <QThread>
#include <QFileInfo>
#include <QtConcurrent/QtConcurrent>

namespace QtNodes {

ImportNodeBase::ImportNodeBase()
    : ExecutableNodeDelegateModel()
    , m_stopRequested(false)
{
    // Set default execution mode to Manual for import nodes
    // These nodes require user selection and shouldn't trigger the workflow automatically
    setExecutionMode(ExecutionMode::Manual);
}

ImportNodeBase::~ImportNodeBase()
{
    // 统一清理工作线程
    if (m_worker) {
        if (m_thread && m_thread->isRunning()) {
            m_worker->StopProcess();
        }
        m_worker->deleteLater();
        m_worker = nullptr;
    }

    if (m_thread) {
        if (m_thread->isRunning()) {
            m_thread->quit();
            m_thread->wait();
        }
        m_thread->deleteLater();
        m_thread = nullptr;
    }
}

// ============================================================================
// 统一的端口配置（默认 2 端口：Port0=成果, Port1=预览）
// ============================================================================

unsigned int ImportNodeBase::nPorts(PortType portType) const
{
    return (portType == PortType::In) ? 0 : 2;
}

NodeDataType ImportNodeBase::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out) {
        if (portIndex == 0) return NodeDataType{"imported_file", "Imported File"};
        if (portIndex == 1) return NodeDataType{"image_info", "Image Info"};
    }
    return NodeDataType();
}

bool ImportNodeBase::portCaptionVisible(PortType portType, PortIndex portIndex) const
{
    return portType == PortType::Out;
}

QString ImportNodeBase::portCaption(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out) {
        if (portIndex == 0) return tr("成果 *");
        if (portIndex == 1) return tr("预览 ?");
    }
    return QString();
}

bool ImportNodeBase::portIsOptional(PortType portType, PortIndex portIndex) const
{
    return portType == PortType::Out && portIndex == 1;
}

std::shared_ptr<NodeData> ImportNodeBase::outData(PortIndex port)
{
    // First try to get data from ExecutableNodeDelegateModel base class
    auto data = ExecutableNodeDelegateModel::outData(port);
    if (data)
        return data;

    // Fall back to member variables
    if (port == 0) return m_importedFiles;
    if (port == 1) return m_imageInfo;

    return nullptr;
}

QStringList ImportNodeBase::previewImagePaths() const
{
    QStringList jpgPaths;
    if (!m_importedFilePaths.isEmpty()) {
        QFileInfo fi(m_importedFilePaths.first());
        QString jpg = fi.absolutePath() + "/" + fi.baseName() + ".jpg";
        if (QFileInfo::exists(jpg))
            jpgPaths.append(jpg);
    }
    return jpgPaths;
}

void ImportNodeBase::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    // Call base class implementation
    ExecutableNodeDelegateModel::setInData(data, port);
}

::QWidget* ImportNodeBase::embeddedWidget()
{
    // Create widget on first access
    if (!_widget)
    {
        _widget = createWidget();

        // Set object name for QSS targeting
        _widget->setObjectName("NodeEmbeddedWidget");
    }
    return _widget;
}

QStandardItemModel* ImportNodeBase::projectModel() const
{
    auto iface = getProjectContext();
    return iface ? iface->projectModel() : nullptr;
}

QString ImportNodeBase::projectPath() const
{
    auto iface = getProjectContext();
    if (iface) {
        QString fullPath = iface->projectPath();
        if (fullPath.isEmpty()) return QString();
        return QFileInfo(fullPath).absolutePath();
    }
    return QString();
}

QString ImportNodeBase::projectName() const
{
    auto iface = getProjectContext();
    return iface ? iface->projectName() : QString();
}

// ============================================================================
// 统一的执行控制
// ============================================================================

void ImportNodeBase::execute()
{
    // 防止重复执行：如果已经在运行中，直接返回
    if (m_thread != nullptr) {
        return;
    }

    m_stopRequested = false;
    setProgress(0);
    setState(ExecutionState::Running);

    // Call the legacy executeImport() method
    executeImport();
}

void ImportNodeBase::stopExecution()
{
    m_stopRequested = true;

    // 停止 Worker
    if (m_worker) {
        m_worker->StopProcess();
    }

    // 中断单文件操作中的 isInterruptionRequested() 检查
    if (m_thread && m_thread->isRunning()) {
        m_thread->requestInterruption();
    }
}

bool ImportNodeBase::validateAndRestoreOutput()
{
    QStringList expectedPaths = getExpectedOutputFilePaths();
    if (expectedPaths.isEmpty())
        return false;

    QString firstH5 = expectedPaths.first();
    if (!QFile::exists(firstH5))
        return false;

    // 恢复 Port 0
    m_importedFilePaths = expectedPaths;
    QString nodeName = getOutputNodeName();
    m_importedFiles = std::make_shared<ImportedFileData>(expectedPaths, nodeName);
    setOutputData(0, m_importedFiles);
    Q_EMIT dataUpdated(0);

    // 恢复 Port 1 预览
    QFileInfo fi(firstH5);
    QString jpgPath = fi.absolutePath() + "/" + fi.baseName() + ".jpg";

    if (!QFileInfo::exists(jpgPath)) {
        // 异步生成预览
        m_remedyWatcher.cancel();
        m_remedyWatcher.waitForFinished();
        m_remedyWatcher.disconnect();

        connect(&m_remedyWatcher, &QFutureWatcher<void>::finished, this, [this, jpgPath]() {
            m_imageInfo = std::make_shared<ImageInfoData>(jpgPath);
            setOutputData(1, m_imageInfo);
            Q_EMIT dataUpdated(1);
        });

        QString capturedH5 = firstH5;
        QFuture<void> future = QtConcurrent::run([capturedH5, jpgPath]() {
            NodeUtils::generateJpgPreviewFromH5(capturedH5, jpgPath, "complex");
        });
        m_remedyWatcher.setFuture(future);
    } else {
        m_imageInfo = std::make_shared<ImageInfoData>(jpgPath);
        setOutputData(1, m_imageInfo);
        Q_EMIT dataUpdated(1);
    }

    return true;
}

void ImportNodeBase::processAutomatically()
{
    // For import nodes, automatic mode typically doesn't do anything
    // since they need user input to select files
    // But we'll complete automatic execution to keep the state consistent
    completeAutomaticExecution();
}

void ImportNodeBase::setExecutionMode(ExecutionMode mode)
{
    ExecutableNodeDelegateModel::setExecutionMode(mode);

    // If switching from Manual to Automatic, we could potentially trigger auto-execution
    // but for import nodes this usually doesn't make sense
    // So we just update the mode
}

// ============================================================================
// 统一的 Worker 槽函数
// ============================================================================

void ImportNodeBase::onImportProgress(int progress, const QString& message)
{
    Q_UNUSED(message);
    setProgress(progress);
}

void ImportNodeBase::onImportFinished()
{
    // 更新导入文件路径
    m_importedFilePaths = getExpectedOutputFilePaths();
    QString nodeName = getOutputNodeName();

    // 装载 Port 0 数据
    if (!m_importedFilePaths.isEmpty() && !nodeName.isEmpty()) {
        m_importedFiles = std::make_shared<ImportedFileData>(m_importedFilePaths, nodeName);
        setOutputData(0, m_importedFiles);
        Q_EMIT dataUpdated(0);
    }

    // 装载 Port 1 预览数据（JPG 已由 Worker 生成）
    if (!m_importedFilePaths.isEmpty()) {
        QFileInfo fi(m_importedFilePaths.first());
        QString jpgPath = fi.absolutePath() + "/" + fi.baseName() + ".jpg";
        m_imageInfo = std::make_shared<ImageInfoData>(jpgPath);
        setOutputData(1, m_imageInfo);
        Q_EMIT dataUpdated(1);
    }

    // 清理线程
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
        m_thread->deleteLater();
        m_thread = nullptr;
    }
    if (m_worker) {
        m_worker->deleteLater();
        m_worker = nullptr;
    }

    finishExecution();
}

void ImportNodeBase::onThreadError(const QString& error)
{
    onError(error);

    // 清理线程
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
        m_thread->deleteLater();
        m_thread = nullptr;
    }
    if (m_worker) {
        m_worker->deleteLater();
        m_worker = nullptr;
    }
}

void ImportNodeBase::onModelUpdated(QStandardItemModel* model)
{
    Q_UNUSED(model);
}

// ============================================================================
// 辅助启动函数
// ============================================================================

void ImportNodeBase::startWorker(BaseImportWorker* worker, const std::vector<ImportTask>& tasks)
{
    if (!worker) return;

    m_worker = worker;
    m_thread = new QThread(this);
    m_worker->moveToThread(m_thread);

    // 绑定通用槽函数
    connect(m_worker, &BaseImportWorker::updateProcess, this, &ImportNodeBase::onImportProgress);
    connect(m_worker, &BaseImportWorker::endProcess, this, &ImportNodeBase::onImportFinished);
    connect(m_worker, &BaseImportWorker::errorProcess, this, &ImportNodeBase::onThreadError);
    connect(m_worker, &BaseImportWorker::sendModel, this, &ImportNodeBase::onModelUpdated);

    m_thread->start();

    QMetaObject::invokeMethod(m_worker, "import_patch",
        Q_ARG(QString, projectPath()),
        Q_ARG(std::vector<ImportTask>, tasks),
        Q_ARG(QString, getOutputNodeName()),
        Q_ARG(QString, projectName()),
        Q_ARG(QStandardItemModel*, projectModel()));
}

// ============================================================================
// Helper methods
// ============================================================================

void ImportNodeBase::onProgressUpdate(int progress, const QString& message)
{
    // Use ExecutableNodeDelegateModel's progress mechanism
    setProgress(progress);
    Q_UNUSED(message);
}

void ImportNodeBase::onError(const QString& error)
{
    setState(ExecutionState::Error);
    Q_EMIT executionError(error);
}

IApplicationInterface* ImportNodeBase::getProjectContext() const
{
    return NodeUtils::getProjectContext(_widget);
}

QLabel* ImportNodeBase::createProjectBadge(const QString& projectName)
{
    auto* label = new QLabel();
    label->setObjectName("ProjectBadge");
    label->setText(QStringLiteral(" 📁 当前工程: %1")
        .arg(projectName.isEmpty() ? QStringLiteral("未打开项目") : projectName));
    label->setStyleSheet(
        "QLabel#ProjectBadge {"
        "  background-color: rgba(128, 128, 128, 0.12);"
        "  border: 1px solid rgba(128, 128, 128, 0.2);"
        "  border-radius: 4px;"
        "  padding: 4px 8px;"
        "  font-size: 11px;"
        "  font-weight: 500;"
        "}"
    );
    return label;
}

} // namespace QtNodes
