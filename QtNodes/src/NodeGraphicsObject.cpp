#include "NodeGraphicsObject.hpp"

#include <cstdlib>
#include <iostream>

#include <QtWidgets/QGraphicsEffect>
#include <QtWidgets/QtWidgets>

#include "AbstractGraphModel.hpp"
#include "AbstractNodeGeometry.hpp"
#include "AbstractNodePainter.hpp"
#include "BasicGraphicsScene.hpp"
#include "ConnectionGraphicsObject.hpp"
#include "ConnectionIdUtils.hpp"
#include "NodeConnectionInteraction.hpp"
#include "StyleCollection.hpp"
#include "UndoCommands.hpp"
#include "ExecutableNodeGeometry.hpp"
#include "ExecutableNodeDelegateModel.hpp"
#include "DataFlowGraphModel.hpp"

namespace QtNodes {

NodeGraphicsObject::NodeGraphicsObject(BasicGraphicsScene &scene, NodeId nodeId)
    : _nodeId(nodeId)
    , _graphModel(scene.graphModel())
    , _nodeState(*this)
    , _proxyWidget(nullptr)
{
    scene.addItem(this);

    setFlag(QGraphicsItem::ItemDoesntPropagateOpacityToChildren, true);
    setFlag(QGraphicsItem::ItemIsFocusable, true);

    setLockedState();

    setCacheMode(QGraphicsItem::NoCache);

    QJsonObject nodeStyleJson = _graphModel.nodeData(_nodeId, NodeRole::Style).toJsonObject();

    NodeStyle nodeStyle(nodeStyleJson);

    {
        auto effect = new QGraphicsDropShadowEffect;
        effect->setOffset(4, 4);
        effect->setBlurRadius(20);
        effect->setColor(nodeStyle.ShadowColor);

        setGraphicsEffect(effect);
    }

    setOpacity(nodeStyle.Opacity);

    setAcceptHoverEvents(true);

    setZValue(0);

    embedQWidget();

    nodeScene()->nodeGeometry().recomputeSize(_nodeId);

    QPointF const pos = _graphModel.nodeData<QPointF>(_nodeId, NodeRole::Position);
    setPos(pos);

    // Connect to position updates (use 'this' as context for auto-disconnect on destruction)
    connect(&_graphModel, &AbstractGraphModel::nodePositionUpdated, this, [this, nodeId](NodeId const id) {
        if (id == nodeId) {
            QPointF newPos = _graphModel.nodeData<QPointF>(nodeId, NodeRole::Position);
            setPos(newPos);
        }
    });

    connect(&_graphModel, &AbstractGraphModel::nodeFlagsUpdated, this, [this](NodeId const nodeId) {
        if (_nodeId == nodeId)
            setLockedState();
    });
}

AbstractGraphModel &NodeGraphicsObject::graphModel() const
{
    return _graphModel;
}

BasicGraphicsScene *NodeGraphicsObject::nodeScene() const
{
    return dynamic_cast<BasicGraphicsScene *>(scene());
}

void NodeGraphicsObject::updateQWidgetEmbedPos()
{
  if (_proxyWidget) {
    AbstractNodeGeometry &geometry = nodeScene()->nodeGeometry();
    _proxyWidget->setPos(geometry.widgetPosition(_nodeId));
  }
}

void NodeGraphicsObject::embedQWidget()
{
    AbstractNodeGeometry &geometry = nodeScene()->nodeGeometry();
    geometry.recomputeSize(_nodeId);

    if (auto w = _graphModel.nodeData(_nodeId, NodeRole::Widget).value<QWidget *>()) {
        // Set object name for QSS targeting
        w->setObjectName("NodeEmbeddedWidget");
        w->setAttribute(Qt::WA_InputMethodEnabled, true);

        _proxyWidget = new QGraphicsProxyWidget(this);
        _proxyWidget->setWidget(w);

        // 显式在 setWidget 之后设置代理部件的输入法和焦点标志，防止被 setWidget 覆盖
        _proxyWidget->setFlag(QGraphicsItem::ItemAcceptsInputMethod, true);
        _proxyWidget->setFlag(QGraphicsItem::ItemIsFocusable, true);
        _proxyWidget->setFocusPolicy(Qt::StrongFocus);

        _proxyWidget->setPreferredWidth(5);

        geometry.recomputeSize(_nodeId);

        if (w->sizePolicy().verticalPolicy() & QSizePolicy::ExpandFlag) {
            // Widget starts at captionBottom (captionPosition.y + captionRect.height),
            // so proxy height = total height - captionBottom
            QPointF captionPos = geometry.captionPosition(_nodeId);
            double captionBottom = captionPos.y() + geometry.captionRect(_nodeId).height();
            unsigned int widgetHeight = geometry.size(_nodeId).height() - captionBottom;

            // If the widget wants to use as much vertical space as possible, set
            // it to have the geom's equivalentWidgetHeight.
            _proxyWidget->setMinimumHeight(widgetHeight);
        }

        updateQWidgetEmbedPos();

        //update();

        _proxyWidget->setOpacity(1.0);
        _proxyWidget->setFlag(QGraphicsItem::ItemIgnoresParentOpacity);
    }
}

void NodeGraphicsObject::setLockedState()
{
    NodeFlags flags = _graphModel.nodeFlags(_nodeId);

    bool const locked = flags.testFlag(NodeFlag::Locked);

    setFlag(QGraphicsItem::ItemIsMovable, !locked);
    setFlag(QGraphicsItem::ItemIsSelectable, !locked);
    setFlag(QGraphicsItem::ItemSendsScenePositionChanges, !locked);
}

QRectF NodeGraphicsObject::boundingRect() const
{
    AbstractNodeGeometry &geometry = nodeScene()->nodeGeometry();
    return geometry.boundingRect(_nodeId);
}

QPainterPath NodeGraphicsObject::shape() const
{
    QPainterPath path;
    path.setFillRule(Qt::WindingFill); // 设置为非零环绕规则（WindingFill）以对图形求并集，避免默认的奇偶规则（OddEvenFill）在端口圆圈与节点矩形重叠区域产生镂空
    AbstractNodeGeometry &geometry = nodeScene()->nodeGeometry();
    QSize s = geometry.size(_nodeId);

    // 1. 添加节点主体部分（使用带圆角的矩形以完美贴合视窗边界）
    QRectF bodyRect(0, 0, s.width(), s.height());
    double radius = 3.0; // 与 DefaultNodePainter 中的画圆角半径一致
    path.addRoundedRect(bodyRect, radius, radius);

    // 2. 如果是可执行节点，且节点处于选中状态（因为耳朵只有在选中时才会被绘制），则将顶部耳朵加入判定区域
    auto *execGeo = dynamic_cast<ExecutableNodeGeometry*>(&geometry);
    if (execGeo) {
        auto *dfModel = dynamic_cast<DataFlowGraphModel*>(&_graphModel);
        auto *execModel = dfModel
            ? dfModel->delegateModel<ExecutableNodeDelegateModel>(_nodeId)
            : nullptr;
        if (isSelected() && execModel && execModel->useExternalLayout()) {
            if (execModel->hasExecutionControls()) {
                path.addRect(execGeo->leftEarRect(_nodeId));
            }
            path.addRect(execGeo->rightEarRect(_nodeId));
        }
        path.addRect(execGeo->progressBarRect(_nodeId));
    }

    // 3. 将所有插孔（Ports）的圆形感应区域添加进 Path 中以确保它们能准确响应鼠标 Hover 和连线
    for (PortType portType : {PortType::In, PortType::Out}) {
        unsigned int nPorts = 0;
        if (portType == PortType::In) {
            nPorts = _graphModel.nodeData<PortCount>(_nodeId, NodeRole::InPortCount);
        } else {
            nPorts = _graphModel.nodeData<PortCount>(_nodeId, NodeRole::OutPortCount);
        }

        QJsonDocument json = QJsonDocument::fromVariant(_graphModel.nodeData(_nodeId, NodeRole::Style));
        NodeStyle nodeStyle(json.object());
        double diameter = nodeStyle.ConnectionPointDiameter;
        // 增加连线判定容差缓冲垫，使用与 checkPortHit 一致的 2.0 * ConnectionPointDiameter 容差半径
        double r = 2.0 * diameter;

        for (PortIndex portIndex = 0; portIndex < nPorts; ++portIndex) {
            QPointF p = geometry.portPosition(_nodeId, portType, portIndex);
            path.addEllipse(p, r, r);
        }
    }

    return path;
}

void NodeGraphicsObject::setGeometryChanged()
{
    prepareGeometryChange();
}

void NodeGraphicsObject::moveConnections() const
{
    auto const &connected = _graphModel.allConnectionIds(_nodeId);

    for (auto &cnId : connected) {
        auto cgo = nodeScene()->connectionGraphicsObject(cnId);

        if (cgo)
            cgo->move();
    }
}

void NodeGraphicsObject::reactToConnection(ConnectionGraphicsObject const *cgo)
{
    _nodeState.storeConnectionForReaction(cgo);

    update();
}

void NodeGraphicsObject::paint(QPainter *painter, QStyleOptionGraphicsItem const *option, QWidget *)
{
    nodeScene()->nodePainter().paint(painter, *this);
}

QVariant NodeGraphicsObject::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == ItemScenePositionHasChanged && scene()) {
        moveConnections();
    }

    return QGraphicsObject::itemChange(change, value);
}

void NodeGraphicsObject::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    //if (_nodeState.locked())
    //return;

    AbstractNodeGeometry &geometry = nodeScene()->nodeGeometry();

    for (PortType portToCheck : {PortType::In, PortType::Out}) {
        QPointF nodeCoord = sceneTransform().inverted().map(event->scenePos());

        PortIndex const portIndex = geometry.checkPortHit(_nodeId, portToCheck, nodeCoord);

        if (portIndex == InvalidPortIndex)
            continue;

        auto const &connected = _graphModel.connections(_nodeId, portToCheck, portIndex);

        // Start dragging existing connection.
        if (!connected.empty() && portToCheck == PortType::In) {
            auto const &cnId = *connected.begin();

            // Need ConnectionGraphicsObject

            NodeConnectionInteraction interaction(*this,
                                                  *nodeScene()->connectionGraphicsObject(cnId),
                                                  *nodeScene());

            if (_graphModel.detachPossible(cnId))
                interaction.disconnect(portToCheck);
        } else // initialize new Connection
        {
            if (portToCheck == PortType::Out) {
                auto const outPolicy = _graphModel
                                           .portData(_nodeId,
                                                     portToCheck,
                                                     portIndex,
                                                     PortRole::ConnectionPolicyRole)
                                           .value<ConnectionPolicy>();

                if (!connected.empty() && outPolicy == ConnectionPolicy::One) {
                    for (auto &cnId : connected) {
                        _graphModel.deleteConnection(cnId);
                    }
                }
            } // if port == out

            ConnectionId const incompleteConnectionId = makeIncompleteConnectionId(_nodeId,
                                                                                   portToCheck,
                                                                                   portIndex);

            nodeScene()->makeDraftConnection(incompleteConnectionId);
        }
    }

    if (_graphModel.nodeFlags(_nodeId) & NodeFlag::Resizable) {
        auto pos = event->pos();
        bool const hit = geometry.resizeHandleRect(_nodeId).contains(QPoint(pos.x(), pos.y()));
        _nodeState.setResizing(hit);
    }

    // 右键点击节点时自动选中（支持 Ctrl 追加选择）
    if (event->button() == Qt::RightButton) {
        if (!(event->modifiers() & Qt::ControlModifier)) {
            // 未按 Ctrl：清空其他选择，仅选中当前节点
            if (!isSelected()) {
                nodeScene()->clearSelection();
                setSelected(true);
            }
        } else {
            // 按住 Ctrl：追加/切换当前节点的选择状态
            setSelected(!isSelected());
        }
    }

    QGraphicsObject::mousePressEvent(event);

    if (isSelected()) {
        Q_EMIT nodeScene()->nodeSelected(_nodeId);
    }
}

void NodeGraphicsObject::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    // Deselect all other items after this one is selected.
    // Unless we press a CTRL button to add the item to the selected group before
    // starting moving.
    if (!isSelected()) {
        if (!event->modifiers().testFlag(Qt::ControlModifier))
            scene()->clearSelection();

        setSelected(true);
    }

    if (_nodeState.resizing()) {
        auto diff = event->pos() - event->lastPos();

        if (auto w = _graphModel.nodeData<QWidget *>(_nodeId, NodeRole::Widget)) {
            prepareGeometryChange();

            auto oldSize = w->size();

            oldSize += QSize(diff.x(), diff.y());

            w->resize(oldSize);

            AbstractNodeGeometry &geometry = nodeScene()->nodeGeometry();

            // Passes the new size to the model.
            geometry.recomputeSize(_nodeId);

            update();

            moveConnections();

            event->accept();
        }
    } else {
        auto diff = event->pos() - event->lastPos();
        if (diff.x() != 0.0 || diff.y() != 0.0) {
            nodeScene()->undoStack().push(new MoveNodeCommand(nodeScene(), diff));
        }

        event->accept();
    }

    QRectF r = nodeScene()->sceneRect();

    r = r.united(mapToScene(boundingRect()).boundingRect());

    nodeScene()->setSceneRect(r);
}

void NodeGraphicsObject::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    _nodeState.setResizing(false);

    QGraphicsObject::mouseReleaseEvent(event);

    // position connections precisely after fast node move
    moveConnections();

    nodeScene()->nodeClicked(_nodeId);
}

void NodeGraphicsObject::hoverEnterEvent(QGraphicsSceneHoverEvent *event)
{
    // bring all the colliding nodes to background
    QList<QGraphicsItem *> overlapItems = collidingItems();

    for (QGraphicsItem *item : overlapItems) {
        if (item->zValue() > 0.0) {
            item->setZValue(0.0);
        }
    }

    // bring this node forward
    setZValue(1.0);

    _nodeState.setHovered(true);

    update();

    Q_EMIT nodeScene()->nodeHovered(_nodeId, event->screenPos());

    event->accept();
}

void NodeGraphicsObject::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
    _nodeState.setHovered(false);

    setProperty("executionControlHoverKind", QString());
    setProperty("executionControlHoverLocalPos", QVariant());
    setProperty("executionControlHoverExternal", false);

    setZValue(0.0);

    update();

    Q_EMIT nodeScene()->nodeHoverLeft(_nodeId);

    event->accept();
}

void NodeGraphicsObject::hoverMoveEvent(QGraphicsSceneHoverEvent *event)
{
    auto pos = event->pos();

    //NodeGeometry geometry(_nodeId, _graphModel, nodeScene());
    AbstractNodeGeometry &geometry = nodeScene()->nodeGeometry();

    if ((_graphModel.nodeFlags(_nodeId) | NodeFlag::Resizable)
        && geometry.resizeHandleRect(_nodeId).contains(QPoint(pos.x(), pos.y()))) {
        setCursor(QCursor(Qt::SizeFDiagCursor));
    } else {
        setCursor(QCursor());
    }

    auto *execGeo = dynamic_cast<ExecutableNodeGeometry*>(&geometry);
    if (execGeo) {
        auto *dfModel = dynamic_cast<DataFlowGraphModel*>(&_graphModel);
        ExecutableNodeDelegateModel* execModel = nullptr;
        if (dfModel) {
            auto *delegateModel = dfModel->delegateModel<NodeDelegateModel>(_nodeId);
            execModel = dynamic_cast<ExecutableNodeDelegateModel*>(delegateModel);
        }

        const bool useExternal = execModel && execModel->useExternalLayout();
        const bool layoutEligible = execModel && (!useExternal || isSelected());
        const bool startHit = layoutEligible && execModel->hasExecutionControls()
            && (useExternal ? execGeo->hitTestStartButton(_nodeId, pos)
                            : execGeo->hitTestCardStartButton(_nodeId, pos));
        const bool modeHit = layoutEligible && execModel->hasExecutionControls()
            && (useExternal ? execGeo->hitTestModeButton(_nodeId, pos)
                            : execGeo->hitTestCardModeButton(_nodeId, pos));
        const bool detailHit = layoutEligible
            && (useExternal ? execGeo->hitTestDetailButton(_nodeId, pos)
                            : execGeo->hitTestCardDetailButton(_nodeId, pos));

        QString hoverKind;
        if (startHit) {
            hoverKind = QStringLiteral("start");
            if (execModel->executionState() == ExecutionState::Completed) {
                setToolTip("Re-run");
            } else if (execModel->executionState() == ExecutionState::Running) {
                setToolTip("Stop");
            } else {
                setToolTip("Run");
            }
        } else if (modeHit) {
            hoverKind = QStringLiteral("mode");
            if (execModel->executionMode() == ExecutionMode::Automatic) {
                setToolTip("Auto Mode");
            } else if (execModel->executionMode() == ExecutionMode::Manual) {
                setToolTip("Manual Mode");
            } else {
                setToolTip("Disabled Mode");
            }
        } else if (detailHit) {
            hoverKind = QStringLiteral("detail");
            setToolTip("Detail View");
        } else {
            if (execModel && execModel->executionState() == ExecutionState::Error) {
                QString errMsg = execModel->lastErrorMessage();
                setToolTip(errMsg.isEmpty() ? QObject::tr("Execution Error") : errMsg);
            } else if (execModel && execModel->executionState() == ExecutionState::Warning) {
                QString warningMsg = execModel->lastWarningMessage();
                setToolTip(warningMsg.isEmpty()
                    ? QObject::tr("Execution completed with warnings")
                    : warningMsg);
            } else {
                setToolTip("");
            }
        }

        setProperty("executionControlHoverKind", hoverKind);
        setProperty("executionControlHoverLocalPos", pos);
        setProperty("executionControlHoverExternal", useExternal);
    }

    event->accept();
}

void NodeGraphicsObject::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
    QGraphicsItem::mouseDoubleClickEvent(event);

    Q_EMIT nodeScene()->nodeDoubleClicked(_nodeId);
}

void NodeGraphicsObject::contextMenuEvent(QGraphicsSceneContextMenuEvent *event)
{
    Q_EMIT nodeScene()->nodeContextMenu(_nodeId, mapToScene(event->pos()));
}

} // namespace QtNodes
