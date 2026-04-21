#ifndef IMPORTNODEBASE_H
#define IMPORTNODEBASE_H

#include <QtNodes/NodeDelegateModel>
#include <QtNodes/NodeData>
#include <QtNodes/NodeDelegateModelRegistry>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QStandardItemModel>
#include <memory>

// Forward declarations
class WorkflowUI;

namespace QtNodes {

// ============================================================================
// ImportNodeBase - Base class for all import nodes
// ============================================================================
class ImportNodeBase : public NodeDelegateModel
{
    Q_OBJECT

public:
    ImportNodeBase();
    virtual ~ImportNodeBase() = default;

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("Import"); }
    QString name() const override { return QStringLiteral("ImportBase"); }

    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;
    void setInData(std::shared_ptr<NodeData> data, PortIndex port) override;

    // Get project context (to be accessed by derived classes)
    QStandardItemModel* projectModel() const;
    QString projectPath() const;
    QString projectName() const;
    ::QWidget* embeddedWidget() override;

protected:
    // Subclass must override these
    virtual void executeImport() = 0;
    virtual QString getImportedFilePath() const = 0;
    virtual QString getOutputNodeName() const;
    virtual QWidget* createWidget() = 0;

    // Output data
    std::shared_ptr<NodeData> m_outputData;

    // Processing state
    bool m_isProcessing;
    bool m_canStop;

    // Cached widget (created on first access)
    mutable QWidget* m_widget;

    // Helper methods
    void onProgressUpdate(int progress, const QString& message);
    void onImportFinished();
    void onError(const QString& error);

    // Get WorkflowUI reference
    WorkflowUI* getNodeEditorWindow() const;
};

} // namespace QtNodes

#endif // IMPORTNODEBASE_H
