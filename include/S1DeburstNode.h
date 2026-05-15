#ifndef S1DEBURSTNODE_H
#define S1DEBURSTNODE_H

#include "ImportDataTypes.h"
#include "MyThread.h"
#include <QtNodes/internal/ExecutableNodeDelegateModel.hpp>
#include <QtNodes/NodeData>
#include <QWidget>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QThread>
#include <QStandardItemModel>
#include <QFileInfo>
#include <QRegularExpression>
#include <memory>

// Forward declarations
class IApplicationInterface;

namespace QtNodes {

// ============================================================================
// S1DeburstNode - Sentinel-1 Deburst preprocessing node
// ============================================================================
class S1DeburstNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    S1DeburstNode();
    ~S1DeburstNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("S1 Deburst"); }
    QString name() const override { return QStringLiteral("S1Deburst"); }
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
    QComboBox* m_dataNodeCombo;
    QLineEdit* m_outputNodeNameEdit;

    // Input data storage
    std::shared_ptr<ImportedFileData> m_inputData;
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
    void startDeburst(QString savePath, QString dstProject,
                     QString srcNode, QString dstNode, QStandardItemModel* model);
};

} // namespace QtNodes

#endif // S1DEBURSTNODE_H
