#ifndef IMPORTNODEBASE_H
#define IMPORTNODEBASE_H

#include <QtNodes/internal/ExecutableNodeDelegateModel.hpp>
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
class IApplicationInterface;
class MyThread;

namespace QtNodes {

// ============================================================================
// ImportNodeBase - Base class for all import nodes
// ============================================================================
class ImportNodeBase : public ExecutableNodeDelegateModel
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

    // ExecutableNodeDelegateModel interface implementation
    void setExecutionMode(ExecutionMode mode) override;

protected:
    // Subclass must override these (legacy interface)
    virtual void executeImport() = 0;
    
    // Subclasses must implement this to provide the generated file paths
    virtual QStringList getImportedFilePaths() const = 0;
    virtual QString getOutputNodeName() const;
    virtual QWidget* createWidget() = 0;

    // New Executable interface that subclasses must override
    void execute() override;
    void stopExecution() override;
    void processAutomatically() override;

    // Thread accessors - subclasses must implement to return their worker/QThread
    virtual MyThread* workerThread() const { return nullptr; }
    virtual QThread* qThread() const = 0;

    // Helper: create a styled project badge label (shared across all import nodes)
    static QLabel* createProjectBadge(const QString& projectName);

    // Helper methods
    void onProgressUpdate(int progress, const QString& message);
    virtual void onImportFinished();
    void onError(const QString& error);

    // Get project context interface
    IApplicationInterface* getProjectContext() const;

    // Flag for stop request
    bool m_stopRequested;
};

} // namespace QtNodes

#endif // IMPORTNODEBASE_H
