#include "include/NodeGroupManager.h"

#include <QUuid>
#include <algorithm>


NodeGroupManager::NodeGroupManager(QObject *parent)
    : QObject(parent)
    , m_graphModel(nullptr)
    , m_scene(nullptr)
{
}

NodeGroupManager::~NodeGroupManager()
{
    clearAllGroups();
}

void NodeGroupManager::setGraphModel(QtNodes::DataFlowGraphModel *model)
{
    m_graphModel = model;
}

void NodeGroupManager::setScene(QtNodes::BasicGraphicsScene *scene)
{
    m_scene = scene;
}

QString NodeGroupManager::generateGroupId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QRectF NodeGroupManager::calculateBounds(const QVector<QtNodes::NodeId> &nodeIds) const
{
    if (nodeIds.isEmpty() || !m_graphModel)
        return QRectF();

    QRectF bounds;
    bool first = true;

    for (QtNodes::NodeId nodeId : nodeIds)
    {
        QPointF pos = m_graphModel->nodeData(nodeId, QtNodes::NodeRole::Position).toPointF();

        if (first)
        {
            bounds = QRectF(pos, QSizeF(0, 0));
            first = false;
        }
        else
        {
            bounds = bounds.united(QRectF(pos, QSizeF(0, 0)));
        }

        // Get node size for more accurate bounds
        QtNodes::NodeGraphicsObject *nodeGraphicsObj = nullptr;
        if (m_scene)
        {
            nodeGraphicsObj = m_scene->nodeGraphicsObject(nodeId);
        }

        if (nodeGraphicsObj)
        {
            QRectF nodeRect = nodeGraphicsObj->boundingRect();
            QRectF translatedRect = nodeRect.translated(pos);
            bounds = bounds.united(translatedRect);
        }
    }

    // Add padding
    bounds.adjust(-10, -10, 10, 10);

    return bounds;
}

QtNodes::NodeId NodeGroupManager::createGroup(const QString &name,
                                            const QVector<QtNodes::NodeId> &nodeIds)
{
    if (nodeIds.isEmpty() || !m_graphModel)
        return QtNodes::InvalidNodeId;

    QString groupId = generateGroupId();

    NodeGroup group;
    group.id = groupId;
    group.name = name.isEmpty() ? "Group" : name;
    group.bounds = calculateBounds(nodeIds);
    group.nodes = nodeIds;
    group.collapsed = false;
    group.color = QColor(255, 255, 200, 100);  // Semi-transparent yellow
    group.originalPosition = QPointF();

    m_groups[groupId] = group;

    // Update node to group mapping
    for (QtNodes::NodeId nodeId : nodeIds)
    {
        m_nodeToGroupMap[nodeId] = groupId;
    }

    emit groupCreated(groupId);
    emit groupUpdated(groupId);

    return QtNodes::NodeId(groupId.toULongLong());  // Return dummy node ID for compatibility
}

void NodeGroupManager::deleteGroup(const QString &groupId)
{
    if (!m_groups.contains(groupId))
        return;

    // Remove node to group mapping
    const NodeGroup &group = m_groups[groupId];
    for (QtNodes::NodeId nodeId : group.nodes)
    {
        m_nodeToGroupMap.remove(nodeId);
    }

    m_groups.remove(groupId);

    emit groupDeleted(groupId);
}

QString NodeGroupManager::getGroupForNode(QtNodes::NodeId nodeId) const
{
    return m_nodeToGroupMap.value(nodeId, QString());
}

NodeGroup* NodeGroupManager::getGroup(const QString &groupId)
{
    return m_groups.contains(groupId) ? &m_groups[groupId] : nullptr;
}

const NodeGroup* NodeGroupManager::getGroup(const QString &groupId) const
{
    return m_groups.contains(groupId) ? &m_groups.value(groupId) : nullptr;
}

void NodeGroupManager::updateGroupBounds(const QString &groupId)
{
    if (!m_groups.contains(groupId))
        return;

    NodeGroup &group = m_groups[groupId];
    group.bounds = calculateBounds(group.nodes);

    emit groupUpdated(groupId);
}

void NodeGroupManager::toggleGroupCollapse(const QString &groupId)
{
    if (!m_groups.contains(groupId) || !m_graphModel)
        return;

    NodeGroup &group = m_groups[groupId];
    group.collapsed = !group.collapsed;

    if (group.collapsed)
    {
        // Save original positions
        for (QtNodes::NodeId nodeId : group.nodes)
        {
            QPointF pos = m_graphModel->nodeData(nodeId, QtNodes::NodeRole::Position).toPointF();
            // Store in node user data or elsewhere
        }
    }
    else
    {
        // Restore original positions
    }

    emit groupUpdated(groupId);
}

void NodeGroupManager::moveGroup(const QString &groupId, const QPointF &delta)
{
    if (!m_groups.contains(groupId) || !m_graphModel)
        return;

    NodeGroup &group = m_groups[groupId];

    // Move all nodes in the group
    for (QtNodes::NodeId nodeId : group.nodes)
    {
        QPointF pos = m_graphModel->nodeData(nodeId, QtNodes::NodeRole::Position).toPointF();
        m_graphModel->setNodeData(nodeId, QtNodes::NodeRole::Position, pos + delta);
    }

    // Update group bounds
    group.bounds.translate(delta);

    emit groupUpdated(groupId);
}

void NodeGroupManager::clearAllGroups()
{
    m_groups.clear();
    m_nodeToGroupMap.clear();
}
