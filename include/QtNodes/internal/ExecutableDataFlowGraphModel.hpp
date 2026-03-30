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

public:
    NodeId addNode(QString const nodeType = QString()) override;

    void addConnection(ConnectionId const connectionId) override;

    bool isExecutableNode(NodeId nodeId) const;

    ExecutionMode getNodeExecutionMode(NodeId nodeId) const;

    void propagateFromNode(NodeId nodeId, PortIndex portIndex);

private Q_SLOTS:
    void onOutPortDataUpdated(NodeId const nodeId, PortIndex const portIndex);

private:
    BasicGraphicsScene *_scene = nullptr;
};

} // namespace QtNodes
