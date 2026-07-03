#ifndef ORBITREFINEMENTNODE_H
#define ORBITREFINEMENTNODE_H

#include "ImportDataTypes.h"
#include "NodeDataTypes.h"
#include "OrbitRefinementWorker.h"
#include <GCPDatabase.h>
#include <QtNodes/internal/ExecutableNodeDelegateModel.hpp>
#include <QtNodes/NodeData>
#include <QWidget>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QThread>
#include <QStandardItemModel>
#include <QPointer>
#include <memory>

class IApplicationInterface;
class XMLFile;

namespace QtNodes {

class OrbitRefinementNode : public ExecutableNodeDelegateModel
{
    Q_OBJECT

public:
    OrbitRefinementNode();
    ~OrbitRefinementNode();

    // NodeDelegateModel interface
    QString caption() const override { return QStringLiteral("Orbit Refinement"); }
    QString name() const override { return QStringLiteral("OrbitRefinement"); }
    unsigned int nPorts(PortType portType) const override;
    NodeDataType dataType(PortType portType, PortIndex portIndex) const override;
    std::shared_ptr<NodeData> outData(PortIndex port) override;
    void setInData(std::shared_ptr<NodeData> data, PortIndex port) override;
    bool portCaptionVisible(PortType portType, PortIndex portIndex) const override;
    QString portCaption(PortType portType, PortIndex portIndex) const override;
    bool portIsOptional(PortType portType, PortIndex portIndex) const override;
    ::QWidget* embeddedWidget() override;

    QJsonObject save() const override;
    void load(QJsonObject const &json) override;

    // ExecutableNodeDelegateModel interface implementation
    void setExecutionMode(ExecutionMode mode) override;
    QStringList previewImagePaths() const override;

protected:
    bool validateAndRestoreOutput() override;

private:
    // UI elements
    ::QWidget* _widget;
    QLabel* m_inputNodeLabel;
    QSpinBox* m_masterIndexSpin;
    QComboBox* m_polyDegreeCombo;
    QLineEdit* m_outputNodeNameEdit;
    QLabel* m_statusLabel;

    // Input/output data storage
    std::shared_ptr<ImportedFileData> m_inputData;
    std::shared_ptr<ImportedFileData> m_outputData;
    std::shared_ptr<ImageInfoData> m_imageInfoData;
    QString m_outputNodeName;
    int m_masterIndex;
    int m_polyDegree;

    // GCP Database
    GCPDatabase* m_db;

    // Worker thread
    QPointer<OrbitRefinementWorker> m_worker;
    QPointer<QThread> m_thread;

    // Helper methods
    void createWidget();
    void updateLabels();
    void updateWidgetSize();
    void initDatabase();
    void onProgressUpdate(int progress, const QString& message);
    void onProcessingFinished();
    void onError(const QString& error);
    void onModelUpdated(QStandardItemModel* model);
    void onResultsReceived(const QString& dstNode, const QStringList& h5Paths, const QStringList& originNames);
    bool validateInputs() const;
    QString generateDefaultOutputName() const;
    void executeProcessing();

    // Get project context interface
    QStandardItemModel* projectModel() const;
    QString projectPath() const;
    QString projectName() const;
    XMLFile* projectXml() const;

    // Executable interface implementation
    void execute() override;
    void stopExecution() override;
    void processAutomatically() override;
};

} // namespace QtNodes

#endif // ORBITREFINEMENTNODE_H
