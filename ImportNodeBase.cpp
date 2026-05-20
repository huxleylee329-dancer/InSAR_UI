#include "ImportNodeBase.h"
#include "ImportDataTypes.h"
#include "IApplicationInterface.h"
#include "MyThread.h"
#include "MainWindow.h"
#include "InterfaceManager.h"
#include "WorkspaceUI.h"
#include <QApplication>

namespace QtNodes {

ImportNodeBase::ImportNodeBase()
    : ExecutableNodeDelegateModel()
    , m_stopRequested(false)
{
    // Set default execution mode
    setExecutionMode(ExecutionMode::Automatic);
}

unsigned int ImportNodeBase::nPorts(PortType portType) const
{
    // No input ports, one output port
    if (portType == PortType::In)
        return 0;
    else
        return 1;
}

NodeDataType ImportNodeBase::dataType(PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::Out)
        return NodeDataType{"imported_file", "Imported File"};
    return NodeDataType();
}

std::shared_ptr<NodeData> ImportNodeBase::outData(PortIndex port)
{
    // First try to get data from ExecutableNodeDelegateModel base class
    auto data = ExecutableNodeDelegateModel::outData(port);
    if (data)
        return data;
    
    // Fall back to legacy behavior (for backward compatibility)
    // Note: This should only be needed during transition
    return nullptr;
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

QString ImportNodeBase::getOutputNodeName() const
{
    // Default implementation - derived classes can override
    return QString();
}

void ImportNodeBase::onProgressUpdate(int progress, const QString& message)
{
    // Use ExecutableNodeDelegateModel's progress mechanism
    setProgress(progress);
    Q_UNUSED(message);
}

void ImportNodeBase::onImportFinished()
{
    QString filePath = getImportedFilePath();
    QString nodeName = getOutputNodeName();

    if (!filePath.isEmpty() && !nodeName.isEmpty())
    {
        auto outputData = std::make_shared<ImportedFileData>(filePath, nodeName);
        setOutputData(0, outputData);
        Q_EMIT dataUpdated(0);
    }

    finishExecution();
}

void ImportNodeBase::onError(const QString& error)
{
    setState(ExecutionState::Error);
    Q_EMIT executionError(error);
}

void ImportNodeBase::execute()
{
    m_stopRequested = false;
    setProgress(0);
    setState(ExecutionState::Running);
    
    // Call the legacy executeImport() method
    executeImport();
}

void ImportNodeBase::stopExecution()
{
    m_stopRequested = true;

    // 停止后台工作线程：设置 stop_flag 使批处理循环退出
    MyThread* wt = workerThread();
    if (wt)
        wt->StopProcess();

    // 中断单文件操作中的 isInterruptionRequested() 检查
    QThread* qt = qThread();
    if (qt && qt->isRunning())
        qt->requestInterruption();
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

IApplicationInterface* ImportNodeBase::getProjectContext() const
{
    // 1. Navigate up the widget hierarchy to find the interface (standard way)
    if (_widget)
    {
        ::QWidget* parent = _widget->parentWidget();
        while (parent)
        {
            auto* iface = dynamic_cast<IApplicationInterface*>(parent);
            if (iface) {
                return iface;
            }
            parent = parent->parentWidget();
        }
    }

    // 2. Fallback: If not found via hierarchy (e.g., during creation), 
    // try to find it via main window's current interface
    foreach(::QWidget * widget, QApplication::topLevelWidgets()) {
        MainWindow* mainWin = qobject_cast<MainWindow*>(widget);
        if (mainWin && mainWin->interfaceManager()) {
            auto* iface = mainWin->interfaceManager()->currentInterface();
            if (iface) {
                return iface;
            }
        }
    }

    return nullptr;
}

} // namespace QtNodes
) {
                    return iface;
                }
            }
        }
    }

    return nullptr;
}

} // namespace QtNodes
