#ifndef S1SWATHMERGENODE_H
#define S1SWATHMERGENODE_H

#include "ImportDataTypes.h"
#include "MyThread.h"
#include <QtNodes/internal/ExecutableNodeDelegateModel.hpp>
#include <QtNodes/NodeData>
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QThread>
#include <QStandardItemModel>
#include <QFileInfo>
#include <memory>

// Forward declarations
class IApplicationInterface;

namespace QtNodes {

// ============================================================================
// S1SwathMergeNode - Sentinel-1 Swath Merge preprocessing node
// ============================================================================
class S1SwathMergeNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    S1SwathMergeNode();
    ~S1SwathMergeNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("S1 Swath Merge"); }
    QString name() const override { return QStringLiteral("S1SwathMerge"); }
    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;
    void setInData(std::shared_ptr<NodeData> data, PortIndex port) override;
    ::QWidget* embeddedWidget() override;

    QJsonObject save() const override;

    void load(QJsonObject const &json) override;

    // ExecutableNodeDelegateModel interface implementation
    void setExecutionMode(ExecutionMode mode) override;

private:
    // UI elements
    QComboBox* m_projectCombo;
    QComboBox* m_dataNodeCombo[3];
    QSpinBox* m_indexSpins[3];
    QLineEdit* m_outputNodeNameEdit;

    // Input data storage
    std::shared_ptr<ImportedFileData> m_inputs[3];
    std::shared_ptr<ImportedFileData> m_outputData;

    // Worker thread
    MyThread* m_workerThread;
    QThread* m_thread;

    // Helper methods
    void createWidget();
    void onProgressUpdate(int progress, const QString& message);
    void onProcessingFinished();
    void onError(const QString& error);
    void onModelUpdated(QStandardItemModel* model);
    bool validateInputs() const;
    void updateLabels();
    QString generateDefaultOutputName() const;
    void executeProcessing();

    // Get project context interface
    IApplicationInterface* getProjectContext() const;
    QStandardItemModel* projectModel() const;
    QString projectPath() const;
    QString projectName() const;

    // Executable interface implementation
    void execute() override;
    void stopExecution() override;
    void processAutomatically() override;

signals:
    void startSwathMerge(int index1, int index2, int index3, QString project,
                        QString node1, QString node2, QString node3, QString dstNode, QStandardItemModel*);
};

} // namespace QtNodes

#endif // S1SWATHMERGENODE_H
