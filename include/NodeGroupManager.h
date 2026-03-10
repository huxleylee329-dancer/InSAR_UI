#ifndef NODEGROUPMANAGER_H
#define NODEGROUPMANAGER_H

#include <QObject>
#include <QRectF>
#include <QColor>
#include <QVector>
#include <QMap>
#include <QString>
#include <memory>

// QtNodes headers
#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/BasicGraphicsScene>
#include <QtNodes/NodeGraphicsObject>
#include <QtNodes/Definitions>

/**
 * @brief NodeGroup - 节点组数据结构
 *
 * 表示一组组节点的视觉分组
 */
struct NodeGroup
{
    QString id;                          // 组的唯一标识符
    QString name;                        // 组的名称
    QRectF bounds;                       // 组的边界矩形
    QColor color;                        // 组的背景颜色
    QVector<QtNodes::NodeId> nodes;      // 组内包含的节点ID列表
    bool collapsed;                       // 是否折叠
    QPointF originalPosition;             // 原始位置（用于展开时恢复）
};

/**
 * @brief NodeGroupManager - 节点组管理器
 *
 * 管理节点编组功能，支持：
 * - 创建节点组 (Ctrl+G)
 * - 删除节点组
 * - 选择节点组
 * - 移动节点组
 * - 折叠/展开节点组
 */
class NodeGroupManager : public QObject
{
    Q_OBJECT

public:
    explicit NodeGroupManager(QObject *parent = nullptr);
    ~NodeGroupManager();

    // 设置图模型
    void setGraphModel(QtNodes::DataFlowGraphModel *model);
    void setScene(QtNodes::BasicGraphicsScene *scene);
    QtNodes::DataFlowGraphModel* graphModel() const { return m_graphModel; }

    // 创建节点组
    QtNodes::NodeId createGroup(const QString &name,
                              const QVector<QtNodes::NodeId> &nodeIds);

    // 删除节点组
    void deleteGroup(const QString &groupId);

    // 获取节点所在的组ID
    QString getGroupForNode(QtNodes::NodeId nodeId) const;

    // 获取组信息
    NodeGroup* getGroup(const QString &groupId);
    const NodeGroup* getGroup(const QString &groupId) const;

    // 获取所有组
    QMap<QString, NodeGroup>& groups() { return m_groups; }
    const QMap<QString, NodeGroup>& groups() const { return m_groups; }

    // 更新组的边界
    void updateGroupBounds(const QString &groupId);

    // 折叠/展开组
    void toggleGroupCollapse(const QString &groupId);

    // 移动组
    void moveGroup(const QString &groupId, const QPointF &delta);

    // 清除所有组
    void clearAllGroups();

signals:
    void groupCreated(const QString &groupId);
    void groupDeleted(const QString &groupId);
    void groupUpdated(const QString &groupId);
    void selectionChanged();

private:
    // 生成唯一的组ID
    QString generateGroupId() const;

    // 计算一组节点的边界矩形
    QRectF calculateBounds(const QVector<QtNodes::NodeId> &nodeIds) const;

    // 数据成员
    QtNodes::DataFlowGraphModel *m_graphModel;
    QtNodes::BasicGraphicsScene *m_scene;
    QMap<QString, NodeGroup> m_groups;
    QMap<QtNodes::NodeId, QString> m_nodeToGroupMap;  // 节点ID -> 组ID 映射
};

#endif // NODEGROUPMANAGER_H
