
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

static QVariant outputDataForPropagation(ExecutableDataFlowGraphModel const *graph,
                                          NodeId nodeId,
                                          PortIndex portIndex)
{
    auto *source = delegateModelConst<ExecutableNodeDelegateModel>(graph, nodeId);
    if (source != nullptr) {
        // A non-terminal node must never expose its previous artifact downstream.
        if (source->executionState() != ExecutionState::Completed &&
            source->executionState() != ExecutionState::Warning) {
            return QVariant{};
        }
        source->synchronizeOutputRevision(portIndex);
    }

    return graph->portData(nodeId, PortType::Out, portIndex, PortRole::Data);
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

        connect(model.get(),
                &NodeDelegateModel::embeddedWidgetSizeUpdated,
                [restoredNodeId, this]() {
                    Q_EMIT nodeUpdated(restoredNodeId);
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

    // Input and connection callbacks are suppressed during restoration. Once
    // all connections exist, recalculate readiness for automatic nodes.
    for (auto const nodeId : allNodeIds()) {
        auto *execModel = delegateModel<ExecutableNodeDelegateModel>(nodeId);
        if (execModel) {
            execModel->setRestoring(false);
            execModel->refreshStateAfterRestoration();
            execModel->triggerVisualUpdate();
        }
    }
}

void ExecutableDataFlowGraphModel::addConnection(ConnectionId const connectionId)
{
    // Keep connection creation and its initial propagation on the executable
    // path so a Running source cannot expose a stale artifact to a new child.
    _connectivity.insert(connectionId);
    sendConnectionCreation(connectionId);

    QVariant const portDataToPropagate = outputDataForPropagation(this,
                                                                   connectionId.outNodeId,
                                                                   connectionId.outPortIndex);
    setPortData(connectionId.inNodeId,
                PortType::In,
                connectionId.inPortIndex,
                portDataToPropagate,
                PortRole::Data);

    // If we are restoring from project, skip automatic execution triggers
    if (_isRestoring) {
        return;
    }

    // For Manual mode nodes, clear the input data to prevent automatic processing ONLY if not completed
    if (isExecutableNode(connectionId.outNodeId)) {
        ExecutionMode mode = getNodeExecutionMode(connectionId.outNodeId);
        auto *execModel = delegateModel<ExecutableNodeDelegateModel>(connectionId.outNodeId);
        if (mode == ExecutionMode::Manual && execModel && execModel->executionState() != ExecutionState::Completed) {
            // Clear the input port data of the target node to prevent auto-execution
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

    QVariant const portDataToPropagate = outputDataForPropagation(this, nodeId, portIndex);

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

    QVariant const portDataToPropagate = outputDataForPropagation(this, nodeId, portIndex);

    for (auto const &cn : connected) {
        setPortData(cn.inNodeId, PortType::In, cn.inPortIndex, portDataToPropagate, PortRole::Data);
    }
}

} // namespace QtNodes
