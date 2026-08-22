#pragma once

#include "DataFlowGraphModel.hpp"
#include "ExecutableNodeDelegateModel.hpp"
#include "BasicGraphicsScene.hpp"
#include "Export.hpp"

class QStandardItemModel;

namespace QtNodes {

class NODE_EDITOR_PUBLIC ExecutableDataFlowGraphModel : public DataFlowGraphModel
{
    Q_OBJECT

public:
    ExecutableDataFlowGraphModel(std::shared_ptr<NodeDelegateModelRegistry> registry,
                                  BasicGraphicsScene *scene = nullptr);

    ~ExecutableDataFlowGraphModel() override = default;

    /// Set the scene pointer after construction
    void setScene(BasicGraphicsScene *scene) { _scene = scene; }

    /// Supplies the project ownership boundary used to allocate paste-safe
    /// output names without involving overwrite or load-existing prompts.
    void setProjectOutputContext(QStandardItemModel* projectModel,
                                 const QString& projectDirectory);

    /// Brackets a single paste operation so sibling clones reserve output
    /// names against one another as well as against the existing project.
    void beginPasteConfigurationClone();
    void endPasteConfigurationClone();

    /// Set whether the model is currently being restored from a project
    void setRestoring(bool restoring) { _isRestoring = restoring; }

    /// Check if the model is currently being restored from a project
    bool isRestoring() const { return _isRestoring; }

public:
    NodeId addNode(QString const nodeType = QString()) override;

    void load(QJsonObject const &json) override;

    void loadNode(QJsonObject const &nodeJson) override;

    void addConnection(ConnectionId const connectionId) override;

    bool isExecutableNode(NodeId nodeId) const;

    ExecutionMode getNodeExecutionMode(NodeId nodeId) const;

    void propagateFromNode(NodeId nodeId, PortIndex portIndex);

private Q_SLOTS:
    void onOutPortDataUpdated(NodeId const nodeId, PortIndex const portIndex);

private:
    PasteContext pasteContext() const;

    BasicGraphicsScene *_scene = nullptr;
    QStandardItemModel* _projectModel = nullptr;
    QString _projectDirectory;
    PasteContext _activePasteContext;
    bool _pasteConfigurationCloneActive = false;
    bool _isRestoring = false;
};

} // namespace QtNodes
