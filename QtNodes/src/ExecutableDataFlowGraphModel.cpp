#include "QtNodes/internal/ExecutableDataFlowGraphModel.hpp"
#include "QtNodes/internal/BasicGraphicsScene.hpp"
#include "QtNodes/internal/NodeDelegateModelRegistry.hpp"
#include "QtNodes/internal/Definitions.hpp"
#include "QtNodes/internal/DataFlowGraphModel.hpp"

#include <QVariant>

namespace QtNodes {

// Helper to get delegate model in const context
template<typename NodeDelegateModelType>
NodeDelegateModelType *delegateModelConst(const DataFlowGraphModel* model, NodeId const nodeId)
{
    // Since we can't call non-const delegateModel from const context,
    // we use const_cast to get access - this is safe because we don't modify anything
    return const_cast<DataFlowGraphModel*>(model)->delegateModel<NodeDelegateModelType>(nodeId);
}

ExecutableDataFlowGraphModel::ExecutableDataFlowGraphModel(std::shared_ptr<NodeDelegateModelRegistry> registry,
                                  BasicGraphicsScene *scene)
    : DataFlowGraphModel(std::move(registry))
    , _scene(scene)
{
}

NodeId ExecutableDataFlowGraphModel::addNode(QString const nodeType)
{
    NodeId newId = DataFlowGraphModel::addNode(nodeType);

    // If it's an ExecutableNodeDelegateModel, set the node context for visual updates
    if (_scene != nullptr) {
        auto *execModel = delegateModel<ExecutableNodeDelegateModel>(newId);
        if (execModel != nullptr) {
            execModel->setNodeContext(newId, _scene);
        }
    }

    return newId;
}

void ExecutableDataFlowGraphModel::addConnection(ConnectionId const connectionId)
{
    // Always call base class to create the connection
    DataFlowGraphModel::addConnection(connectionId);

    // For Manual mode nodes, clear the input data to prevent automatic processing
    if (isExecutableNode(connectionId.outNodeId)) {
        ExecutionMode mode = getNodeExecutionMode(connectionId.outNodeId);
        if (mode == ExecutionMode::Manual) {
            // Clear the input port data of the target node
            QVariant emptyData{};
            setPortData(connectionId.inNodeId,
                       PortType::In,
                       connectionId.inPortIndex,
                       emptyData,
                       PortRole::Data);
        }
        else {
            // Auto mode: trigger automatic execution for source nodes (nodes with no input ports)
            auto *execModel = delegateModel<ExecutableNodeDelegateModel>(connectionId.outNodeId);
            if (execModel && execModel->nPorts(PortType::In) == 0) {
                // This is a source node being connected in auto mode, trigger automatic execution
                execModel->triggerAutoExecution();
            }
        }
    }
}

bool ExecutableDataFlowGraphModel::isExecutableNode(NodeId nodeId) const
{
    auto *execModel = delegateModelConst<ExecutableNodeDelegateModel>(this, nodeId);
    return execModel != nullptr;
}

ExecutionMode ExecutableDataFlowGraphModel::getNodeExecutionMode(NodeId nodeId) const
{
    auto *executableModel = delegateModelConst<ExecutableNodeDelegateModel>(this, nodeId);
    if (executableModel) {
        return executableModel->executionMode();
    }
    return ExecutionMode::Automatic;
}

void ExecutableDataFlowGraphModel::propagateFromNode(NodeId nodeId, PortIndex portIndex)
{
    std::unordered_set<ConnectionId> const &connected = connections(nodeId,
                                                                    PortType::Out,
                                                                    portIndex);

    QVariant const portDataToPropagate = portData(nodeId, PortType::Out, portIndex, PortRole::Data);

    for (auto const &cn : connected) {
        setPortData(cn.inNodeId, PortType::In, cn.inPortIndex, portDataToPropagate, PortRole::Data);
    }
}

void ExecutableDataFlowGraphModel::onOutPortDataUpdated(NodeId const nodeId, PortIndex const portIndex)
{
    // Always propagate when a node emits dataUpdated (either automatic execution or manual start)
    // The Manual mode only prevents automatic propagation on connection creation
    std::unordered_set<ConnectionId> const &connected = connections(nodeId,
                                                                    PortType::Out,
                                                                    portIndex);

    QVariant const portDataToPropagate = portData(nodeId, PortType::Out, portIndex, PortRole::Data);

    for (auto const &cn : connected) {
        setPortData(cn.inNodeId, PortType::In, cn.inPortIndex, portDataToPropagate, PortRole::Data);
    }
}

} // namespace QtNodes
