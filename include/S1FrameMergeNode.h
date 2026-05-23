#ifndef S1FRAMEMERGENODE_H
#define S1FRAMEMERGENODE_H

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
// S1FrameMergeNode - Sentinel-1 Frame Merge preprocessing node
// ============================================================================
class S1FrameMergeNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    S1FrameMergeNode();
    ~S1FrameMergeNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("S1 Frame Merge"); }
    QString name() const override { return QStringLiteral("S1FrameMerge"); }
    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;
    void setInData(std::shared_ptr<NodeData> data, PortIndex port) override;
    ::QWidget* embeddedWidget() override;

    QJsonObject save() const override;

    void load(QJsonObject const &json) override;

    // ExecutableNodeDelegateModel interface implementation
    void setExecutionMode(ExecutionMode mode) override;

protected:
    bool validateAndRestoreOutput() override;

private:
    // UI elements
    QComboBox* m_projectCombo;
    QComboBox* m_dataNodeCombo[2];
    QSpinBox* m_indexSpins[2];
    QLineEdit* m_outputNodeNameEdit;

    // Input data storage
    std::shared_ptr<ImportedFileData> m_inputs[2];
    std::shared_ptr<ImportedFileData> m_outputData;
    QString m_outputNodeName;
    int m_index1 = 1;
    int m_index2 = 1;

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
    QStandardItemModel* projectModel() const;
    QString projectPath() const;
    QString projectName() const;

    // Executable interface implementation
    void execute() override;
    void stopExecution() override;
    void processAutomatically() override;

signals:
    void startFrameMerge(int index1, int index2, QString project,
                        QString node1, QString node2, QString dstNode, QStandardItemModel*);
};

} // namespace QtNodes

#endif // S1FRAMEMERGENODE_H
