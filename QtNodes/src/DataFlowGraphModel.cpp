#include "DataFlowGraphModel.hpp"
#include "ConnectionIdHash.hpp"
#include "ExecutableNodeDelegateModel.hpp"
#include "NodeUtils.h"

#include <QJsonArray>
#include <QDebug>
#include <QStringList>

#include <stdexcept>

namespace QtNodes {

namespace {
bool nodeExecutionIsDisabled(const std::unordered_map<NodeId, std::unique_ptr<NodeDelegateModel>>& models,
                             NodeId nodeId)
{
    const auto model = models.find(nodeId);
    const auto* executable = model == models.end()
        ? nullptr : dynamic_cast<const ExecutableNodeDelegateModel*>(model->second.get());
    return executable && executable->executionMode() == ExecutionMode::Disabled;
}
}

DataFlowGraphModel::DataFlowGraphModel(std::shared_ptr<NodeDelegateModelRegistry> registry)
    : _registry(std::move(registry))
    , _nextNodeId{0}
{}

std::unordered_set<NodeId> DataFlowGraphModel::allNodeIds() const
{
    std::unordered_set<NodeId> nodeIds;
    for_each(_models.begin(), _models.end(), [&nodeIds](auto const &p) { nodeIds.insert(p.first); });

    return nodeIds;
}

std::unordered_set<ConnectionId> DataFlowGraphModel::allConnectionIds(NodeId const nodeId) const
{
    std::unordered_set<ConnectionId> result;

    std::copy_if(_connectivity.begin(),
                 _connectivity.end(),
                 std::inserter(result, std::end(result)),
                 [&nodeId](ConnectionId const &cid) {
                     return cid.inNodeId == nodeId || cid.outNodeId == nodeId;
                 });

    return result;
}

std::unordered_set<ConnectionId> DataFlowGraphModel::connections(NodeId nodeId,
                                                                 PortType portType,
                                                                 PortIndex portIndex) const
{
    std::unordered_set<ConnectionId> result;

    std::copy_if(_connectivity.begin(),
                 _connectivity.end(),
                 std::inserter(result, std::end(result)),
                 [&portType, &portIndex, &nodeId](ConnectionId const &cid) {
                     return (getNodeId(portType, cid) == nodeId
                             && getPortIndex(portType, cid) == portIndex);
                 });

    return result;
}

bool DataFlowGraphModel::connectionExists(ConnectionId const connectionId) const
{
    return (_connectivity.find(connectionId) != _connectivity.end());
}

bool DataFlowGraphModel::isConnectionDormant(ConnectionId const connectionId) const
{
    return _dormantConnections.find(connectionId) != _dormantConnections.end();
}

std::unordered_set<ConnectionId> DataFlowGraphModel::activeConnections(NodeId nodeId,
                                                                        PortType portType,
                                                                        PortIndex portIndex) const
{
    std::unordered_set<ConnectionId> result;
    for (const ConnectionId& connection : connections(nodeId, portType, portIndex)) {
        if (!isConnectionDormant(connection)) result.insert(connection);
    }
    return result;
}

NodeId DataFlowGraphModel::addNode(QString const nodeType)
{
    std::unique_ptr<NodeDelegateModel> model = _registry->create(nodeType);

    if (model) {
        NodeId newId = newNodeId();

        connect(model.get(),
                &NodeDelegateModel::dataUpdated,
                [newId, this](PortIndex const portIndex) {
                    onOutPortDataUpdated(newId, portIndex);
                });

        connect(model.get(),
                &NodeDelegateModel::embeddedWidgetSizeUpdated,
                [newId, this]() {
                    Q_EMIT nodeUpdated(newId);
                });

        connect(model.get(),
                &NodeDelegateModel::portsAboutToBeDeleted,
                this,
                [newId, this](PortType const portType, PortIndex const first, PortIndex const last) {
                    portsAboutToBeDeleted(newId, portType, first, last);
                });

        connect(model.get(),
                &NodeDelegateModel::portsDeleted,
                this,
                &DataFlowGraphModel::portsDeleted);

        connect(model.get(),
                &NodeDelegateModel::portsAboutToBeInserted,
                this,
                [newId, this](PortType const portType, PortIndex const first, PortIndex const last) {
                    portsAboutToBeInserted(newId, portType, first, last);
                });

        connect(model.get(),
                &NodeDelegateModel::portsInserted,
                this,
                &DataFlowGraphModel::portsInserted);

        _models[newId] = std::move(model);

        Q_EMIT nodeCreated(newId);

        return newId;
    }

    return InvalidNodeId;
}

bool DataFlowGraphModel::connectionPossible(ConnectionId const connectionId) const
{
    auto portVacant = [&](PortType const portType) {
        NodeId const nodeId = getNodeId(portType, connectionId);
        PortIndex const portIndex = getPortIndex(portType, connectionId);
        auto const connected = activeConnections(nodeId, portType, portIndex);

        auto policy = portData(nodeId, portType, portIndex, PortRole::ConnectionPolicyRole)
                          .value<ConnectionPolicy>();

        return connected.empty() || (policy == ConnectionPolicy::Many);
    };

    return validateConnection(connectionId).accepted && portVacant(PortType::Out) &&
           portVacant(PortType::In);
}

ProductValidationResult DataFlowGraphModel::validateConnection(ConnectionId const connectionId) const
{
    auto source = _models.find(connectionId.outNodeId);
    auto destination = _models.find(connectionId.inNodeId);
    if (source == _models.end() || destination == _models.end()) {
        return {false, QStringLiteral("Connection references a missing node.")};
    }
    if (connectionId.outPortIndex >= source->second->nPorts(PortType::Out) ||
        connectionId.inPortIndex >= destination->second->nPorts(PortType::In)) {
        return {false, QStringLiteral("Connection references a missing port.")};
    }
    return QtNodes::validateConnection(source->second->productOutputContract(connectionId.outPortIndex),
                                       destination->second->productInputContract(connectionId.inPortIndex));
}

void DataFlowGraphModel::addConnection(ConnectionId const connectionId)
{
    if (!connectionPossible(connectionId)) {
        const ProductValidationResult validation = validateConnection(connectionId);
        Q_EMIT connectionRejected(connectionId, validation.accepted
            ? QStringLiteral("A connection policy rejects this port.") : validation.reason);
        return;
    }
    if (nodeExecutionIsDisabled(_models, connectionId.outNodeId) ||
        nodeExecutionIsDisabled(_models, connectionId.inNodeId)) {
        addDormantConnection(connectionId);
        return;
    }
    _connectivity.insert(connectionId);

    sendConnectionCreation(connectionId);

    QVariant const portDataToPropagate = portData(connectionId.outNodeId,
                                                  PortType::Out,
                                                  connectionId.outPortIndex,
                                                  PortRole::Data);

    setPortData(connectionId.inNodeId,
                PortType::In,
                connectionId.inPortIndex,
                portDataToPropagate,
                PortRole::Data);
}

bool DataFlowGraphModel::addDormantConnection(ConnectionId const connectionId)
{
    auto source = _models.find(connectionId.outNodeId);
    auto destination = _models.find(connectionId.inNodeId);
    if (source == _models.end() || destination == _models.end() ||
        connectionId.outPortIndex >= source->second->nPorts(PortType::Out) ||
        connectionId.inPortIndex >= destination->second->nPorts(PortType::In) ||
        _connectivity.find(connectionId) != _connectivity.end()) {
        return false;
    }
    _connectivity.insert(connectionId);
    _dormantConnections.insert(connectionId);
    Q_EMIT connectionCreated(connectionId);
    return true;
}

bool DataFlowGraphModel::setConnectionDormant(ConnectionId const connectionId, bool dormant,
                                              QString* failureReason)
{
    if (!connectionExists(connectionId)) {
        if (failureReason) *failureReason = QStringLiteral("Connection does not exist.");
        return false;
    }
    if (dormant) {
        if (!_dormantConnections.insert(connectionId).second) return true;
        propagateEmptyDataTo(connectionId.inNodeId, connectionId.inPortIndex);
        return true;
    }
    if (_dormantConnections.find(connectionId) == _dormantConnections.end()) return true;
    if (nodeExecutionIsDisabled(_models, connectionId.outNodeId) ||
        nodeExecutionIsDisabled(_models, connectionId.inNodeId)) {
        const QString reason = QStringLiteral("Cannot re-enable a dormant edge while an endpoint is disabled.");
        if (failureReason) *failureReason = reason;
        Q_EMIT connectionRejected(connectionId, reason);
        return false;
    }
    const ProductValidationResult validation = validateConnection(connectionId);
    if (!validation.accepted) {
        if (failureReason) *failureReason = validation.reason;
        Q_EMIT connectionRejected(connectionId, validation.reason);
        return false;
    }
    const auto activePortVacant = [&](PortType portType) {
        const NodeId nodeId = getNodeId(portType, connectionId);
        const PortIndex portIndex = getPortIndex(portType, connectionId);
        const ConnectionPolicy policy = portData(nodeId, portType, portIndex,
                                                 PortRole::ConnectionPolicyRole)
                                            .value<ConnectionPolicy>();
        return policy == ConnectionPolicy::Many ||
               activeConnections(nodeId, portType, portIndex).empty();
    };
    if (!activePortVacant(PortType::Out) || !activePortVacant(PortType::In)) {
        const QString reason = QStringLiteral("Cannot re-enable a dormant edge because an active One-port binding exists.");
        if (failureReason) *failureReason = reason;
        Q_EMIT connectionRejected(connectionId, reason);
        return false;
    }
    _dormantConnections.erase(connectionId);
    sendConnectionCreation(connectionId);
    const QVariant sourceData = portData(connectionId.outNodeId, PortType::Out,
                                         connectionId.outPortIndex, PortRole::Data);
    const auto destination = _models.find(connectionId.inNodeId);
    if (!setPortData(connectionId.inNodeId, PortType::In,
                     connectionId.inPortIndex, sourceData, PortRole::Data) ||
        destination == _models.end() ||
        !destination->second->isInputBindingValid(connectionId.inPortIndex)) {
        _dormantConnections.insert(connectionId);
        sendConnectionDeletion(connectionId);
        propagateEmptyDataTo(connectionId.inNodeId, connectionId.inPortIndex);
        QString reason = QStringLiteral("Cannot re-enable a dormant edge because its input binding failed runtime validation.");
        const auto* executable = destination == _models.end()
            ? nullptr : dynamic_cast<const ExecutableNodeDelegateModel*>(destination->second.get());
        if (executable && !executable->lastErrorMessage().isEmpty()) reason = executable->lastErrorMessage();
        if (failureReason) *failureReason = reason;
        Q_EMIT connectionRejected(connectionId, reason);
        return false;
    }
    return true;
}

bool DataFlowGraphModel::setNodeConnectionsDormant(NodeId nodeId, bool dormant,
                                                    QString* failureReason)
{
    if (_models.find(nodeId) == _models.end()) {
        if (failureReason) *failureReason = QStringLiteral("Cannot change dormant edges for a missing node.");
        return false;
    }

    const auto connectionsForNode = allConnectionIds(nodeId);
    bool success = true;
    QStringList failures;
    const auto updateConnection = [&](ConnectionId const& connection) {
        if (dormant) {
            QString reason;
            if (!setConnectionDormant(connection, true, &reason)) {
                success = false;
                failures.append(reason.isEmpty() ? QStringLiteral("Cannot disable workflow edge.") : reason);
            }
            return;
        }
        if (!isConnectionDormant(connection)) return;
        if (nodeExecutionIsDisabled(_models, connection.outNodeId) ||
            nodeExecutionIsDisabled(_models, connection.inNodeId)) {
            success = false;
            failures.append(QStringLiteral("Adjacent node is still disabled."));
            return;
        }
        QString reason;
        if (!setConnectionDormant(connection, false, &reason)) {
            success = false;
            failures.append(reason.isEmpty()
                ? QStringLiteral("Edge failed semantic or runtime binding validation.") : reason);
        }
    };

    // Restore input bindings before output edges so a re-enabled node resolves
    // its snapshots before it is allowed to publish downstream data.
    for (ConnectionId const& connection : connectionsForNode) {
        if (connection.inNodeId == nodeId) updateConnection(connection);
    }
    for (ConnectionId const& connection : connectionsForNode) {
        if (connection.inNodeId != nodeId) updateConnection(connection);
    }

    if (!success && failureReason) {
        *failureReason = failures.join(QStringLiteral(" "));
    }
    return success;
}

void DataFlowGraphModel::sendConnectionCreation(ConnectionId const connectionId)
{
    Q_EMIT connectionCreated(connectionId);

    auto iti = _models.find(connectionId.inNodeId);
    auto ito = _models.find(connectionId.outNodeId);
    if (iti != _models.end() && ito != _models.end()) {
        auto &modeli = iti->second;
        auto &modelo = ito->second;
        modeli->inputConnectionCreated(connectionId);
        modelo->outputConnectionCreated(connectionId);
    }
}

void DataFlowGraphModel::sendConnectionDeletion(ConnectionId const connectionId)
{
    Q_EMIT connectionDeleted(connectionId);

    auto iti = _models.find(connectionId.inNodeId);
    auto ito = _models.find(connectionId.outNodeId);
    if (iti != _models.end() && ito != _models.end()) {
        auto &modeli = iti->second;
        auto &modelo = ito->second;
        modeli->inputConnectionDeleted(connectionId);
        modelo->outputConnectionDeleted(connectionId);
    }
}

bool DataFlowGraphModel::nodeExists(NodeId const nodeId) const
{
    return (_models.find(nodeId) != _models.end());
}

QVariant DataFlowGraphModel::nodeData(NodeId nodeId, NodeRole role) const
{
    QVariant result;

    auto it = _models.find(nodeId);
    if (it == _models.end())
        return result;

    auto &model = it->second;

    switch (role) {
    case NodeRole::Type:
        result = model->name();
        break;

    case NodeRole::Position:
        result = _nodeGeometryData[nodeId].pos;
        break;

    case NodeRole::Size:
        result = _nodeGeometryData[nodeId].size;
        break;

    case NodeRole::CaptionVisible:
        result = model->captionVisible();
        break;

    case NodeRole::Caption:
        result = model->caption();
        break;

    case NodeRole::Style: {
        auto style = model->nodeStyle();
        result = style.toJson().toVariantMap();
    } break;

    case NodeRole::InternalData: {
        QJsonObject nodeJson;

        nodeJson["internal-data"] = _models.at(nodeId)->save();

        result = nodeJson.toVariantMap();
        break;
    }

    case NodeRole::InPortCount:
        result = model->nPorts(PortType::In);
        break;

    case NodeRole::OutPortCount:
        result = model->nPorts(PortType::Out);
        break;

    case NodeRole::Widget: {
        auto w = model->embeddedWidget();
        result = QVariant::fromValue(w);
    } break;
    }

    return result;
}

NodeFlags DataFlowGraphModel::nodeFlags(NodeId nodeId) const
{
    auto it = _models.find(nodeId);

    if (it != _models.end() && it->second->resizable())
        return NodeFlag::Resizable;

    return NodeFlag::NoFlags;
}

bool DataFlowGraphModel::setNodeData(NodeId nodeId, NodeRole role, QVariant value)
{
    Q_UNUSED(nodeId);
    Q_UNUSED(role);
    Q_UNUSED(value);

    bool result = false;

    switch (role) {
    case NodeRole::Type:
        break;
    case NodeRole::Position: {
        QPointF newPos = value.value<QPointF>();
        auto it = _nodeGeometryData.find(nodeId);
        if (it == _nodeGeometryData.end() || it->second.pos != newPos) {
            _nodeGeometryData[nodeId].pos = newPos;
            Q_EMIT nodePositionUpdated(nodeId);
            result = true;
        }
    } break;

    case NodeRole::Size: {
        _nodeGeometryData[nodeId].size = value.value<QSize>();
        result = true;
    } break;

    case NodeRole::CaptionVisible:
        break;

    case NodeRole::Caption:
        break;

    case NodeRole::Style:
        break;

    case NodeRole::InternalData:
        break;

    case NodeRole::InPortCount:
        break;

    case NodeRole::OutPortCount:
        break;

    case NodeRole::Widget:
        break;
    }

    return result;
}

QVariant DataFlowGraphModel::portData(NodeId nodeId,
                                      PortType portType,
                                      PortIndex portIndex,
                                      PortRole role) const
{
    QVariant result;

    auto it = _models.find(nodeId);
    if (it == _models.end())
        return result;

    auto &model = it->second;

    switch (role) {
    case PortRole::Data:
        if (portType == PortType::Out) {
            result = QVariant::fromValue(model->outData(portIndex));
        }
        break;

    case PortRole::DataType:
        result = QVariant::fromValue(model->dataType(portType, portIndex));
        break;

    case PortRole::ConnectionPolicyRole:
        result = QVariant::fromValue(model->portConnectionPolicy(portType, portIndex));
        break;

    case PortRole::CaptionVisible:
        result = model->portCaptionVisible(portType, portIndex);
        break;

    case PortRole::Caption:
        result = model->portCaption(portType, portIndex);
        break;

    case PortRole::IsOptional:
        result = model->portIsOptional(portType, portIndex);
        break;
    }

    return result;
}

bool DataFlowGraphModel::setPortData(
    NodeId nodeId, PortType portType, PortIndex portIndex, QVariant const &value, PortRole role)
{
    Q_UNUSED(nodeId);

    QVariant result;

    auto it = _models.find(nodeId);
    if (it == _models.end())
        return false;

    auto &model = it->second;

    switch (role) {
    case PortRole::Data:
        if (portType == PortType::In) {
            const auto data = value.value<std::shared_ptr<NodeData>>();
            if (data) {
                const ProductValidationResult validation = validateBoundDescriptor(
                    model->productInputContract(portIndex), data->productDescriptor());
                if (!validation.accepted) {
                    model->setInputBindingValid(portIndex, false, validation.reason);
                    Q_EMIT inputBindingRejected(nodeId, portIndex, validation.reason);
                    return false;
                }
            }
            model->setInputBindingValid(portIndex, true, QString());
            model->setInData(data, portIndex);

            // Triggers repainting on the scene.
            Q_EMIT inPortDataWasSet(nodeId, portType, portIndex);
        }
        break;

    default:
        break;
    }

    return true;
}

bool DataFlowGraphModel::deleteConnection(ConnectionId const connectionId)
{
    bool disconnected = false;

    auto it = _connectivity.find(connectionId);

    if (it != _connectivity.end()) {
        disconnected = true;

        _connectivity.erase(it);
        _dormantConnections.erase(connectionId);
    }

    if (disconnected) {
        sendConnectionDeletion(connectionId);

        propagateEmptyDataTo(getNodeId(PortType::In, connectionId),
                             getPortIndex(PortType::In, connectionId));
    }

    return disconnected;
}

bool DataFlowGraphModel::deleteNode(NodeId const nodeId)
{
    // Delete connections to this node first.
    auto connectionIds = allConnectionIds(nodeId);
    for (auto &cId : connectionIds) {
        deleteConnection(cId);
    }

    _nodeGeometryData.erase(nodeId);
    _models.erase(nodeId);

    Q_EMIT nodeDeleted(nodeId);

    return true;
}

QJsonObject DataFlowGraphModel::saveNode(NodeId const nodeId) const
{
    QJsonObject nodeJson;

    nodeJson["id"] = static_cast<qint64>(nodeId);

    nodeJson["internal-data"] = _models.at(nodeId)->save();

    {
        QPointF const pos = nodeData(nodeId, NodeRole::Position).value<QPointF>();

        QJsonObject posJson;
        posJson["x"] = pos.x();
        posJson["y"] = pos.y();
        nodeJson["position"] = posJson;
    }

    return nodeJson;
}

QJsonObject DataFlowGraphModel::save() const
{
    QJsonObject sceneJson;
    sceneJson[QStringLiteral("semantic_contract_version")] = 1;

    QJsonArray nodesJsonArray;
    for (auto const nodeId : allNodeIds()) {
        nodesJsonArray.append(saveNode(nodeId));
    }
    sceneJson["nodes"] = nodesJsonArray;

    QJsonArray connJsonArray;
    for (auto const &cid : _connectivity) {
        QJsonObject connection = toJson(cid);
        if (isConnectionDormant(cid)) connection.insert(QStringLiteral("dormant"), true);
        connJsonArray.append(connection);
    }
    sceneJson["connections"] = connJsonArray;

    return sceneJson;
}

void DataFlowGraphModel::loadNode(QJsonObject const &nodeJson)
{
    // Possibility of the id clash when reading it from json and not generating a
    // new value.
    // 1. When restoring a scene from a file.
    // Conflict is not possible because the scene must be cleared by the time of
    // loading.
    // 2. When undoing the deletion command.  Conflict is not possible
    // because all the new ids were created past the removed nodes.
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

void DataFlowGraphModel::load(QJsonObject const &jsonDocument)
{
    constexpr int kCurrentSemanticContractVersion = 1;
    QJsonObject migratedDocument = jsonDocument;
    const QJsonValue versionValue = jsonDocument.value(QStringLiteral("semantic_contract_version"));
    const int version = versionValue.isUndefined() ? 0 : versionValue.toInt(-1);
    if (version == 0) {
        // Version 0 is the only supported migration. Its ports are re-audited
        // against the current explicit contracts while connections are restored.
        Q_EMIT semanticContractAudit(
            QStringLiteral("Legacy workflow has no semantic_contract_version; applying v0-to-v1 contract audit."),
            false);
        migratedDocument[QStringLiteral("semantic_contract_version")] = kCurrentSemanticContractVersion;
    } else if (version < 0 || version > kCurrentSemanticContractVersion) {
        Q_EMIT semanticContractAudit(
            QStringLiteral("Workflow semantic contract version %1 is unsupported; recovery was rejected.")
                .arg(versionValue.toVariant().toString()),
            true);
        return;
    } else if (version != kCurrentSemanticContractVersion) {
        Q_EMIT semanticContractAudit(
            QStringLiteral("Workflow semantic contract version %1 has no migration path; recovery was rejected.")
                .arg(version),
            true);
        return;
    }

    QJsonArray nodesJsonArray = migratedDocument["nodes"].toArray();

    for (QJsonValueRef nodeJson : nodesJsonArray) {
        loadNode(nodeJson.toObject());
    }

    QJsonArray connectionJsonArray = migratedDocument["connections"].toArray();

    for (QJsonValueRef connection : connectionJsonArray) {
        QJsonObject connJson = connection.toObject();

        ConnectionId connId = fromJson(connJson);

        // DEMSource now exposes only entity DEM (0) and preview (1).  Older
        // preview output 2 is moved back to 1.
        const auto sourceIt = _models.find(connId.outNodeId);
        const auto targetIt = _models.find(connId.inNodeId);
        if (sourceIt != _models.end() && targetIt != _models.end() &&
            sourceIt->second->name() == QStringLiteral("DEMSource") &&
            connId.outPortIndex == 1 && connId.inPortIndex == 2) {
            // Pre-label workflows used DEMSource output 1 for a reference.
            // That edge contains no pin itself; only a saved producer binding
            // can authorize migration.  Refuse to reinterpret preview data as
            // a DEM reference when that persisted binding is unavailable.
            const QJsonObject producer = sourceIt->second->save();
            const QString resourceId = producer.value(QStringLiteral("legacyReferenceResourceId")).toString().trimmed();
            const QString provenanceId = producer.value(QStringLiteral("legacyReferencePinnedProvenanceId")).toString().trimmed();
            if (resourceId.isEmpty() || provenanceId.isEmpty()) {
                Q_EMIT semanticContractAudit(
                    QStringLiteral("Legacy DEMSource reference migration rejected: saved resource binding is unavailable."), true);
                continue;
            }
            const QString label = QStringLiteral("legacy-dem-source-%1-%2")
                .arg(QString::number(connId.outNodeId), resourceId.left(12));
            NodeUtils::registerPendingAuxiliaryDemLabel(
                NodeUtils::AuxiliaryDemLabelBinding{label, resourceId, provenanceId});
            QJsonObject consumer = targetIt->second->save();
            consumer.insert(QStringLiteral("auxiliaryDemLabel"), label);
            consumer.insert(QStringLiteral("auxiliaryDemLegacyResourceId"), resourceId);
            consumer.insert(QStringLiteral("auxiliaryDemLegacyPinnedProvenanceId"), provenanceId);
            targetIt->second->load(consumer);
            Q_EMIT semanticContractAudit(
                QStringLiteral("Migrated legacy DEMSource reference edge to project label @%1.").arg(label), false);
            continue;
        }
        if (sourceIt != _models.end() && targetIt != _models.end() &&
            (sourceIt->second->name() == QStringLiteral("DEMSource") ||
             sourceIt->second->caption() == QStringLiteral("External DEM")) &&
            connId.outPortIndex == 2 &&
            connId.inPortIndex < targetIt->second->nPorts(PortType::In) &&
            targetIt->second->dataType(PortType::In, connId.inPortIndex).id == QStringLiteral("image_info")) {
            connId.outPortIndex = 1;
            Q_EMIT semanticContractAudit(
                QStringLiteral("Migrated legacy External DEM preview connection to output port 1."), false);
        }

        if (connJson.value(QStringLiteral("dormant")).toBool(false) ||
            nodeExecutionIsDisabled(_models, connId.outNodeId) ||
            nodeExecutionIsDisabled(_models, connId.inNodeId)) {
            addDormantConnection(connId);
        } else {
            // Legacy/incompatible edges are retained as dormant records so
            // they cannot participate until an explicit re-enable validates.
            const ProductValidationResult validation = validateConnection(connId);
            if (validation.accepted) {
                addConnection(connId);
            } else if (addDormantConnection(connId)) {
                Q_EMIT semanticContractAudit(
                    QStringLiteral("Restored incompatible connection as dormant: %1").arg(validation.reason),
                    false);
            }
        }
    }
}

void DataFlowGraphModel::onOutPortDataUpdated(NodeId const nodeId, PortIndex const portIndex)
{
    const std::unordered_set<ConnectionId> connected = activeConnections(nodeId,
                                                                          PortType::Out,
                                                                          portIndex);

    QVariant const portDataToPropagate = portData(nodeId, PortType::Out, portIndex, PortRole::Data);

    for (auto const &cn : connected) {
        setPortData(cn.inNodeId, PortType::In, cn.inPortIndex, portDataToPropagate, PortRole::Data);
    }
}

void DataFlowGraphModel::propagateEmptyDataTo(NodeId const nodeId, PortIndex const portIndex)
{
    QVariant emptyData{};

    setPortData(nodeId, PortType::In, portIndex, emptyData, PortRole::Data);
}

} // namespace QtNodes
