#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

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

void ExecutableDataFlowGraphModel::loadNode(QJsonObject const &nodeJson)
{
    // 在 DataFlowGraphModel::loadNode 调用 model->load(internalDataJson) 之前，
    // 我们需要预先创建模型并设置 context 和 restoring 标志。
    // 但 DataFlowGraphModel::loadNode 内部处理了创建和 load()。
    
    // 我们手动处理 loadNode 以便注入 restoring 标志
    NodeId restoredNodeId = nodeJson["id"].toInt();
    _nextNodeId = std::max(_nextNodeId, restoredNodeId + 1);

    QJsonObject const internalDataJson = nodeJson["internal-data"].toObject();
    QString delegateModelName = internalDataJson["model-name"].toString();

    std::unique_ptr<NodeDelegateModel> model = _registry->create(delegateModelName);

    if (model) {
        connect(model.get(),
                &NodeDelegateModel::dataUpdated,
                [restoredNodeId, this](PortIndex const portIndex) {
                    onOutPortDataUpdated(restoredNodeId, portIndex);
                });

        auto *execModel = dynamic_cast<ExecutableNodeDelegateModel*>(model.get());
        if (execModel) {
            execModel->setRestoring(_isRestoring);
            if (_scene) {
                execModel->setNodeContext(restoredNodeId, _scene);
            }
        }

        _models[restoredNodeId] = std::move(model);

        Q_EMIT nodeCreated(restoredNodeId);

        QJsonObject posJson = nodeJson["position"].toObject();
        QPointF const pos(posJson["x"].toDouble(), posJson["y"].toDouble());

        setNodeData(restoredNodeId, NodeRole::Position, pos);

        _models[restoredNodeId]->load(internalDataJson);
    } else {
        qCritical() << "Error: No registered model with name" << delegateModelName
                   << ". Skipping node with ID" << restoredNodeId;
    }
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

void ExecutableDataFlowGraphModel::load(QJsonObject const &json)
{
    _isRestoring = true;
    DataFlowGraphModel::load(json);
    _isRestoring = false;

    // After restoration is complete, clear the restoring flag on all nodes
    // and trigger visual updates to reflect the restored state
    for (auto const nodeId : allNodeIds()) {
        auto *execModel = delegateModel<ExecutableNodeDelegateModel>(nodeId);
        if (execModel) {
            execModel->setRestoring(false);
            execModel->triggerVisualUpdate();
        }
    }
}

void ExecutableDataFlowGraphModel::addConnection(ConnectionId const connectionId)
{
    // Always call base class to create the connection
    DataFlowGraphModel::addConnection(connectionId);

    // If we are restoring from project, skip automatic execution triggers
    if (_isRestoring) {
        return;
    }

    // For Manual mode nodes, clear the input data to prevent automatic processing
    if (isExecutableNode(connectionId.outNodeId)) {
        ExecutionMode mode = getNodeExecutionMode(connectionId.outNodeId);
        if (mode == ExecutionMode::Manual) {
            // Clear the input port data of the target node to prevent auto-execution
            // Note: DataFlowGraphModel::addConnection already propagated data if source was completed
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
    if (_isRestoring) return;

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
    if (_isRestoring) return;

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
