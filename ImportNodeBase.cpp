#include "ImportNodeBase.h"
#include "ImportDataTypes.h"
#include "NodeEditorWindow.h"

namespace QtNodes {

ImportNodeBase::ImportNodeBase()
    : m_outputData(nullptr)
    , m_isProcessing(false)
    , m_canStop(false)
    , m_widget(nullptr)
{
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
    return m_outputData;
}

void ImportNodeBase::setInData(std::shared_ptr<NodeData> data, PortIndex port)
{
    Q_UNUSED(data);
    Q_UNUSED(port);
}

QWidget* ImportNodeBase::embeddedWidget()
{
    // Create widget on first access
    if (!m_widget)
    {
        m_widget = createWidget();
    }
    return m_widget;
}

QStandardItemModel* ImportNodeBase::projectModel() const
{
    auto editor = getNodeEditorWindow();
    return editor ? editor->projectModel() : nullptr;
}

QString ImportNodeBase::projectPath() const
{
    auto editor = getNodeEditorWindow();
    return editor ? editor->projectPath() : QString();
}

QString ImportNodeBase::projectName() const
{
    auto editor = getNodeEditorWindow();
    return editor ? editor->projectName() : QString();
}

QString ImportNodeBase::getOutputNodeName() const
{
    // Default implementation - derived classes can override
    return QString();
}

void ImportNodeBase::onProgressUpdate(int progress, const QString& message)
{
    // Default implementation - derived classes can override to update UI
    Q_UNUSED(progress);
    Q_UNUSED(message);
}

void ImportNodeBase::onImportFinished()
{
    QString filePath = getImportedFilePath();
    QString nodeName = getOutputNodeName();

    if (!filePath.isEmpty() && !nodeName.isEmpty())
    {
        m_outputData = std::make_shared<ImportedFileData>(filePath, nodeName);
        Q_EMIT dataUpdated(0);
    }

    m_isProcessing = false;
    m_canStop = false;
}

void ImportNodeBase::onError(const QString& error)
{
    m_isProcessing = false;
    m_canStop = false;
    Q_UNUSED(error);
}

NodeEditorWindow* ImportNodeBase::getNodeEditorWindow() const
{
    // Navigate up the widget hierarchy to find NodeEditorWindow
    if (!m_widget)
        return nullptr;

    QWidget* parent = m_widget->parentWidget();
    while (parent)
    {
        auto* editor = qobject_cast<NodeEditorWindow*>(parent);
        if (editor)
            return editor;
        parent = parent->parentWidget();
    }

    return nullptr;
}

} // namespace QtNodes
