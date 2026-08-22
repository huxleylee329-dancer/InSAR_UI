
#include "QtNodes/internal/ExecutableDataFlowGraphModel.hpp"
#include "QtNodes/internal/BasicGraphicsScene.hpp"
#include "QtNodes/internal/NodeDelegateModelRegistry.hpp"
#include "QtNodes/internal/Definitions.hpp"
#include "QtNodes/internal/DataFlowGraphModel.hpp"

#include <QVariant>
#include <QDir>
#include <QFileInfo>
#include <QStandardItemModel>

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
        // Configuration/reference nodes have no execution lifecycle; their
        // current port data remains valid regardless of execution state.
        // Processing nodes must not expose a stale artifact downstream.
        if (source->hasExecutionControls() &&
            source->executionState() != ExecutionState::Completed &&
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

void ExecutableDataFlowGraphModel::setProjectOutputContext(QStandardItemModel* projectModel,
                                                            const QString& projectDirectory)
{
    _projectModel = projectModel;
    _projectDirectory = projectDirectory.isEmpty()
        ? QString()
        : QDir::cleanPath(projectDirectory);
}

void ExecutableDataFlowGraphModel::beginPasteConfigurationClone()
{
    _activePasteContext = pasteContext();
    _pasteConfigurationCloneActive = true;
}

void ExecutableDataFlowGraphModel::endPasteConfigurationClone()
{
    _pasteConfigurationCloneActive = false;
    _activePasteContext = PasteContext();
}

namespace {
void reserveProjectTreeNames(QStandardItem* parent, QSet<QString>& reservedNames)
{
    if (parent == nullptr) return;

    for (int row = 0; row < parent->rowCount(); ++row) {
        QStandardItem* item = parent->child(row, 0);
        if (item == nullptr) continue;

        const QString name = item->text().trimmed();
        if (!name.isEmpty()) reservedNames.insert(name.toCaseFolded());
        reserveProjectTreeNames(item, reservedNames);
    }
}
}

PasteContext ExecutableDataFlowGraphModel::pasteContext() const
{
    PasteContext context;
    context.projectDirectory = _projectDirectory;

    for (NodeId nodeId : allNodeIds()) {
        const auto* model = delegateModelConst<ExecutableNodeDelegateModel>(this, nodeId);
        if (model == nullptr) continue;

        const QJsonObject modelJson = model->save();
        const QString outputName = model->outputNodeNameForPaste(modelJson);
        if (!outputName.isEmpty()) {
            context.reservedOutputNodeNames.insert(outputName.toCaseFolded());
        }

        for (const QString& artifactPath : model->outputArtifactPathsForPaste(modelJson)) {
            if (!artifactPath.isEmpty()) {
                const QString reservationKey = QDir::cleanPath(
                    QFileInfo(artifactPath).absoluteFilePath()).toCaseFolded();
                context.reservedOutputArtifactPaths.insert(reservationKey);
            }
        }
    }

    if (_projectModel != nullptr) {
        reserveProjectTreeNames(_projectModel->invisibleRootItem(),
                                context.reservedOutputNodeNames);
    }

    return context;
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
    const bool isConfigurationClone = nodeJson
        .value(QStringLiteral("paste-configuration-clone")).toBool(false);
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

        QJsonObject dataToLoad = internalDataJson;
        if (isConfigurationClone && execModel) {
            if (_pasteConfigurationCloneActive) {
                execModel->prepareForPaste(dataToLoad, _activePasteContext);
            } else {
                PasteContext context = pasteContext();
                execModel->prepareForPaste(dataToLoad, context);
            }
        }

        _models[restoredNodeId] = std::move(model);

        Q_EMIT nodeCreated(restoredNodeId);

        QJsonObject posJson = nodeJson["position"].toObject();
        QPointF const pos(posJson["x"].toDouble(), posJson["y"].toDouble());

        setNodeData(restoredNodeId, NodeRole::Position, pos);

        _models[restoredNodeId]->load(dataToLoad);
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

    // 先统一解除全图节点的恢复标记，确保后续 setPortData(nullptr) 能够正常穿透到子类执行 UI 清空
    for (auto const nodeId : allNodeIds()) {
        auto *execModel = delegateModel<ExecutableNodeDelegateModel>(nodeId);
        if (execModel) {
            execModel->setRestoring(false);
        }
    }

    // 后置拓扑依赖校验 Pass：限定检查 0 输出端口的纯展示/叶子节点（保护处理节点合法的离线成果）
    if (_scene) {
        for (auto const nodeId : allNodeIds()) {
            auto *execModel = delegateModel<ExecutableNodeDelegateModel>(nodeId);
            if (!execModel || execModel->nPorts(PortType::Out) != 0 ||
                (execModel->executionState() != ExecutionState::Completed &&
                 execModel->executionState() != ExecutionState::Warning)) {
                continue;
            }

            // 1. 连线未连齐时直接降级为 Idle，并清空非可选输入端口数据
            if (!execModel->allRequiredPortsConnected()) {
                execModel->setState(ExecutionState::Idle);
                for (PortIndex inIdx = 0; inIdx < execModel->nPorts(PortType::In); ++inIdx) {
                    if (!execModel->portIsOptional(PortType::In, inIdx)) {
                        setPortData(nodeId, PortType::In, inIdx, QVariant{}, PortRole::Data);
                    }
                }
                continue;
            }

            // 2. 检查所有必需输入端口连接的上游生产者状态（严格镜像 outputDataForPropagation）
            bool allUpstreamValid = true;
            for (PortIndex idx = 0; idx < execModel->nPorts(PortType::In); ++idx) {
                if (execModel->portIsOptional(PortType::In, idx)) continue;
                const auto connections = activeConnections(nodeId, PortType::In, idx);
                for (const ConnectionId &conn : connections) {
                    auto *sourceModel = delegateModel<ExecutableNodeDelegateModel>(conn.outNodeId);
                    if (sourceModel && sourceModel->hasExecutionControls() &&
                        sourceModel->executionState() != ExecutionState::Completed &&
                        sourceModel->executionState() != ExecutionState::Warning) {
                        allUpstreamValid = false;
                        break;
                    }
                }
                if (!allUpstreamValid) break;
            }

            // 3. 上游无效时降级并对所有非可选输入端口广播清空信号
            if (!allUpstreamValid) {
                const bool shouldBePending = (execModel->executionMode() == ExecutionMode::Automatic);
                execModel->setState(shouldBePending ? ExecutionState::Pending : ExecutionState::Idle);
                for (PortIndex inIdx = 0; inIdx < execModel->nPorts(PortType::In); ++inIdx) {
                    if (!execModel->portIsOptional(PortType::In, inIdx)) {
                        setPortData(nodeId, PortType::In, inIdx, QVariant{}, PortRole::Data);
                    }
                }
            }
        }
    }

    // 最终就绪状态刷新与视图重绘
    for (auto const nodeId : allNodeIds()) {
        auto *execModel = delegateModel<ExecutableNodeDelegateModel>(nodeId);
        if (execModel) {
            execModel->refreshStateAfterRestoration();
            execModel->triggerVisualUpdate();
        }
    }
}

void ExecutableDataFlowGraphModel::addConnection(ConnectionId const connectionId)
{
    // Keep connection creation and its initial propagation on the executable
    // path so a Running source cannot expose a stale artifact to a new child.
    if (!connectionPossible(connectionId)) {
        const ProductValidationResult validation = validateConnection(connectionId);
        Q_EMIT connectionRejected(connectionId, validation.accepted
            ? QStringLiteral("A connection policy rejects this port.") : validation.reason);
        return;
    }

    // A disabled endpoint retains the edge structurally, but it must remain
    // dormant until the endpoint is explicitly re-enabled and revalidated.
    if (getNodeExecutionMode(connectionId.outNodeId) == ExecutionMode::Disabled ||
        getNodeExecutionMode(connectionId.inNodeId) == ExecutionMode::Disabled) {
        addDormantConnection(connectionId);
        return;
    }

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

    const std::unordered_set<ConnectionId> connected = activeConnections(nodeId,
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
    const std::unordered_set<ConnectionId> connected = activeConnections(nodeId,
                                                                          PortType::Out,
                                                                          portIndex);

    QVariant const portDataToPropagate = outputDataForPropagation(this, nodeId, portIndex);

    for (auto const &cn : connected) {
        setPortData(cn.inNodeId, PortType::In, cn.inPortIndex, portDataToPropagate, PortRole::Data);
    }
}

} // namespace QtNodes
