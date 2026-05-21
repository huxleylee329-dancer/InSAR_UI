#pragma once

#include "DataFlowGraphModel.hpp"
#include "ExecutableNodeDelegateModel.hpp"
#include "BasicGraphicsScene.hpp"
#include "Export.hpp"

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
    BasicGraphicsScene *_scene = nullptr;
    bool _isRestoring = false;
};

} // namespace QtNodes
