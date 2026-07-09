#include "GraphicsView.hpp"

#include "BasicGraphicsScene.hpp"
#include "ConnectionGraphicsObject.hpp"
#include "DataFlowGraphModel.hpp"
#include "NodeDelegateModelRegistry.hpp"
#include "NodeGraphicsObject.hpp"
#include "NodeSearchPopup.h"
#include "StyleCollection.hpp"
#include "UndoCommands.hpp"

#include <QtWidgets/QGraphicsScene>

#include <QtGui/QBrush>
#include <QtGui/QPen>
#include <QtGui/QPainter>
#include <QtGui/QImage>

#include <QtWidgets/QMenu>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QMessageBox>
#include <QtGui/QClipboard>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QMimeData>
#include <QtCore/QTimer>
#include <QtWidgets/QApplication>

#include <QtCore/QDebug>
#include <QtCore/QPointF>
#include <QtCore/QRectF>

#include <QtWidgets>

#include <cmath>
#include <iostream>

using QtNodes::BasicGraphicsScene;
using QtNodes::GraphicsView;

GraphicsView::GraphicsView(QWidget *parent)
    : QGraphicsView(parent)
    , _clearSelectionAction(Q_NULLPTR)
    , _deleteSelectionAction(Q_NULLPTR)
    , _duplicateSelectionAction(Q_NULLPTR)
    , _copySelectionAction(Q_NULLPTR)
    , _pasteAction(Q_NULLPTR)
{
    setDragMode(QGraphicsView::ScrollHandDrag);
    setRenderHint(QPainter::Antialiasing);

    auto const &flowViewStyle = StyleCollection::flowViewStyle();

    setBackgroundBrush(flowViewStyle.BackgroundColor);

    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);

    setCacheMode(QGraphicsView::CacheBackground);
    setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);

    setScaleRange(0.3, 2);

    // Sets the scene rect to its maximum possible ranges to avoid autu scene range
    // re-calculation when expanding the all QGraphicsItems common rect.
    int maxSize = 32767;
    setSceneRect(-maxSize, -maxSize, (maxSize * 2), (maxSize * 2));

    // 启用输入法支持以允许输入中文
    setAttribute(Qt::WA_InputMethodEnabled, true);
    if (viewport()) {
        viewport()->setAttribute(Qt::WA_InputMethodEnabled, true);
    }
}

GraphicsView::GraphicsView(BasicGraphicsScene *scene, QWidget *parent)
    : GraphicsView(parent)
{
    setScene(scene);
}

QAction *GraphicsView::clearSelectionAction() const
{
    return _clearSelectionAction;
}

QAction *GraphicsView::deleteSelectionAction() const
{
    return _deleteSelectionAction;
}

QAction *GraphicsView::groupSelectionAction() const
{
    return _groupSelectionAction;
}

void GraphicsView::setScene(BasicGraphicsScene *scene)
{
    QGraphicsView::setScene(scene);

    if (scene) {
        // 监听场景焦点变化，强制在每次焦点切换后重新开启输入法，防止被 Qt 内部机制误关
        connect(scene, &QGraphicsScene::focusItemChanged, this, [this](QGraphicsItem *, QGraphicsItem *, Qt::FocusReason) {
            setAttribute(Qt::WA_InputMethodEnabled, true);
            if (viewport()) {
                viewport()->setAttribute(Qt::WA_InputMethodEnabled, true);
            }
        });
    }

    {
        // setup actions
        delete _clearSelectionAction;
        _clearSelectionAction = new QAction(QStringLiteral("Clear Selection"), this);
        _clearSelectionAction->setShortcut(Qt::Key_Escape);

        connect(_clearSelectionAction, &QAction::triggered, scene, &QGraphicsScene::clearSelection);

        addAction(_clearSelectionAction);
    }

    {
        delete _deleteSelectionAction;
        _deleteSelectionAction = new QAction(QStringLiteral("Delete Selection"), this);
        _deleteSelectionAction->setShortcutContext(Qt::ShortcutContext::WindowShortcut);
        // Support both Delete key (Windows) and Backspace key (Mac)
        QList<QKeySequence> shortcuts;
        shortcuts << QKeySequence(QKeySequence::Delete) << QKeySequence(Qt::Key_Backspace);
        _deleteSelectionAction->setShortcuts(shortcuts);
        _deleteSelectionAction->setAutoRepeat(false);
        connect(_deleteSelectionAction,
                &QAction::triggered,
                this,
                &GraphicsView::onDeleteSelectedObjects);

        addAction(_deleteSelectionAction);
    }

    {
        delete _duplicateSelectionAction;
        _duplicateSelectionAction = new QAction(QStringLiteral("Duplicate Selection"), this);
        _duplicateSelectionAction->setShortcutContext(Qt::ShortcutContext::WidgetShortcut);
        _duplicateSelectionAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
        _duplicateSelectionAction->setAutoRepeat(false);
        connect(_duplicateSelectionAction,
                &QAction::triggered,
                this,
                &GraphicsView::onDuplicateSelectedObjects);

        addAction(_duplicateSelectionAction);
    }

    {
        delete _copySelectionAction;
        _copySelectionAction = new QAction(QStringLiteral("Copy Selection"), this);
        _copySelectionAction->setShortcutContext(Qt::ShortcutContext::WidgetShortcut);
        _copySelectionAction->setShortcut(QKeySequence(QKeySequence::Copy));
        _copySelectionAction->setAutoRepeat(false);
        connect(_copySelectionAction,
                &QAction::triggered,
                this,
                &GraphicsView::onCopySelectedObjects);

        addAction(_copySelectionAction);
    }

    {
        delete _pasteAction;
        _pasteAction = new QAction(QStringLiteral("Paste"), this);
        _pasteAction->setShortcutContext(Qt::ShortcutContext::WidgetShortcut);
        _pasteAction->setShortcut(QKeySequence(QKeySequence::Paste));
        _pasteAction->setAutoRepeat(false);
        connect(_pasteAction, &QAction::triggered, this, &GraphicsView::onPasteObjects);

        addAction(_pasteAction);
    }

    {
        delete _groupSelectionAction;
        _groupSelectionAction = new QAction(QStringLiteral("Group Selection"), this);
        _groupSelectionAction->setShortcutContext(Qt::ShortcutContext::WidgetShortcut);
        _groupSelectionAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_G));
        _groupSelectionAction->setAutoRepeat(false);
        connect(_groupSelectionAction, &QAction::triggered, this, &GraphicsView::onGroupSelectedObjects);

        addAction(_groupSelectionAction);
    }

    // 监听剪贴板变化，实时更新粘贴 Action 的可用状态
    connect(QApplication::clipboard(), &QClipboard::changed,
            this, &GraphicsView::updatePasteActionState);
    // 初始化粘贴 Action 状态
    updatePasteActionState();

    auto undoAction = scene->undoStack().createUndoAction(this, tr("&Undo"));
    undoAction->setShortcuts(QKeySequence::Undo);
    addAction(undoAction);

    auto redoAction = scene->undoStack().createRedoAction(this, tr("&Redo"));
    redoAction->setShortcuts(QKeySequence::Redo);
    addAction(redoAction);
}

void GraphicsView::centerScene()
{
    if (scene()) {
        scene()->setSceneRect(QRectF());

        QRectF sceneRect = scene()->sceneRect();

        if (sceneRect.width() > this->rect().width() || sceneRect.height() > this->rect().height()) {
            fitInView(sceneRect, Qt::KeepAspectRatio);
        }

        centerOn(sceneRect.center());
    }
}

void GraphicsView::contextMenuEvent(QContextMenuEvent *event)
{
    if (itemAt(event->pos())) {
        // 点击在节点上，交给基类处理（节点自行管理右键菜单）
        QGraphicsView::contextMenuEvent(event);
        return;
    }

    // 点击在空白区域，弹出统一画布菜单
    auto const scenePos = mapToScene(event->pos());
    QMenu *menu = createCanvasContextMenu(scenePos, event->globalPos());
    if (menu) {
        menu->exec(event->globalPos());
        delete menu;
    }
}

void GraphicsView::wheelEvent(QWheelEvent *event)
{
    QPoint delta = event->angleDelta();

    if (delta.y() == 0) {
        event->ignore();
        return;
    }

    double const d = delta.y() / std::abs(delta.y());

    if (d > 0.0)
        scaleUp();
    else
        scaleDown();

    // 缩放后，将模拟鼠标移动事件延迟到下一轮事件循环（队列尾部）执行
    // 确保在 QGraphicsScene 坐标系统与 BSP 树完全更新后，进行 100% 精确的悬停检测与光标刷新
    QTimer::singleShot(0, this, [this]() {
        if (viewport()) {
            QPoint const mousePos = mapFromGlobal(QCursor::pos());
            QMouseEvent moveEvent(QEvent::MouseMove, mousePos, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(viewport(), &moveEvent);
        }
    });
}

double GraphicsView::getScale() const
{
    return transform().m11();
}

void GraphicsView::setScaleRange(double minimum, double maximum)
{
    if (maximum < minimum)
        std::swap(minimum, maximum);
    minimum = std::max(0.0, minimum);
    maximum = std::max(0.0, maximum);

    _scaleRange = {minimum, maximum};

    setupScale(transform().m11());
}

void GraphicsView::setScaleRange(ScaleRange range)
{
    setScaleRange(range.minimum, range.maximum);
}

void GraphicsView::scaleUp()
{
    double const step = 1.2;
    double const factor = std::pow(step, 1.0);

    if (_scaleRange.maximum > 0) {
        QTransform t = transform();
        t.scale(factor, factor);
        if (t.m11() >= _scaleRange.maximum) {
            setupScale(t.m11());
            return;
        }
    }

    scale(factor, factor);
    Q_EMIT scaleChanged(transform().m11());
}

void GraphicsView::scaleDown()
{
    double const step = 1.2;
    double const factor = std::pow(step, -1.0);

    if (_scaleRange.minimum > 0) {
        QTransform t = transform();
        t.scale(factor, factor);
        if (t.m11() <= _scaleRange.minimum) {
            setupScale(t.m11());
            return;
        }
    }

    scale(factor, factor);
    Q_EMIT scaleChanged(transform().m11());
}

void GraphicsView::setupScale(double scale)
{
    scale = std::max(_scaleRange.minimum, std::min(_scaleRange.maximum, scale));

    if (scale <= 0)
        return;

    if (scale == transform().m11())
        return;

    QTransform matrix;
    matrix.scale(scale, scale);
    setTransform(matrix, false);

    Q_EMIT scaleChanged(scale);
}

void GraphicsView::onDeleteSelectedObjects()
{
    nodeScene()->undoStack().push(new DeleteCommand(nodeScene()));
}

void GraphicsView::onDuplicateSelectedObjects()
{
    QPointF const pastePosition = scenePastePosition();

    nodeScene()->undoStack().push(new CopyCommand(nodeScene()));
    nodeScene()->undoStack().push(new PasteCommand(nodeScene(), pastePosition));
}

void GraphicsView::onCopySelectedObjects()
{
    nodeScene()->undoStack().push(new CopyCommand(nodeScene()));
}

void GraphicsView::onPasteObjects()
{
    QPointF const pastePosition = scenePastePosition();
    nodeScene()->undoStack().push(new PasteCommand(nodeScene(), pastePosition));
}

void GraphicsView::onGroupSelectedObjects()
{
    emit groupSelected();
}

void GraphicsView::keyPressEvent(QKeyEvent *event)
{
    // 检查当前是否有文本框等控件正在编辑，如果是则不改变拖拽模式，防止干扰中文输入法切换
    QWidget *focusW = QApplication::focusWidget();
    bool isEditing = focusW && focusW != this && focusW != viewport();

    switch (event->key()) {
    case Qt::Key_Shift:
        if (!isEditing) {
            setDragMode(QGraphicsView::RubberBandDrag);
        }
        break;

    case Qt::Key_Tab: {
        // 仅在没有其他文本框获取焦点时触发搜索弹窗
        if (!isEditing) {
            showNodeSearchPopup(QCursor::pos());
        }
        break;
    }

    default:
        break;
    }

    QGraphicsView::keyPressEvent(event);
}

void GraphicsView::keyReleaseEvent(QKeyEvent *event)
{
    // 检查当前是否有文本框等控件正在编辑
    QWidget *focusW = QApplication::focusWidget();
    bool isEditing = focusW && focusW != this && focusW != viewport();

    switch (event->key()) {
    case Qt::Key_Shift:
        if (!isEditing) {
            setDragMode(QGraphicsView::ScrollHandDrag);
        }
        break;

    default:
        break;
    }
    QGraphicsView::keyReleaseEvent(event);
}

void GraphicsView::mousePressEvent(QMouseEvent *event)
{
    QGraphicsView::mousePressEvent(event);
    if (event->button() == Qt::LeftButton) {
        _clickPos = mapToScene(event->pos());
    }
}

void GraphicsView::mouseMoveEvent(QMouseEvent *event)
{
    QGraphicsView::mouseMoveEvent(event);
    if (scene()->mouseGrabberItem() == nullptr && event->buttons() == Qt::LeftButton) {
        // Make sure shift is not being pressed
        if ((event->modifiers() & Qt::ShiftModifier) == 0) {
            QPointF difference = _clickPos - mapToScene(event->pos());
            setSceneRect(sceneRect().translated(difference.x(), difference.y()));
        }
    }
}

void GraphicsView::drawBackground(QPainter *painter, const QRectF &r)
{
    QGraphicsView::drawBackground(painter, r);

    auto drawGrid = [&](double gridStep) {
        QRect windowRect = rect();
        QPointF tl = mapToScene(windowRect.topLeft());
        QPointF br = mapToScene(windowRect.bottomRight());

        double left = std::floor(tl.x() / gridStep - 0.5);
        double right = std::floor(br.x() / gridStep + 1.0);
        double bottom = std::floor(tl.y() / gridStep - 0.5);
        double top = std::floor(br.y() / gridStep + 1.0);

        // vertical lines
        for (int xi = int(left); xi <= int(right); ++xi) {
            QLineF line(xi * gridStep, bottom * gridStep, xi * gridStep, top * gridStep);

            painter->drawLine(line);
        }

        // horizontal lines
        for (int yi = int(bottom); yi <= int(top); ++yi) {
            QLineF line(left * gridStep, yi * gridStep, right * gridStep, yi * gridStep);
            painter->drawLine(line);
        }
    };

    auto const &flowViewStyle = StyleCollection::flowViewStyle();

    QPen pfine(flowViewStyle.FineGridColor, 1.0);

    painter->setPen(pfine);
    drawGrid(15);

    QPen p(flowViewStyle.CoarseGridColor, 1.0);

    painter->setPen(p);
    drawGrid(150);
}

void GraphicsView::showEvent(QShowEvent *event)
{
    QGraphicsView::showEvent(event);

    centerScene();
}

BasicGraphicsScene *GraphicsView::nodeScene()
{
    return dynamic_cast<BasicGraphicsScene *>(scene());
}

QPointF GraphicsView::scenePastePosition()
{
    QPoint origin = mapFromGlobal(QCursor::pos());

    QRect const viewRect = rect();
    if (!viewRect.contains(origin))
        origin = viewRect.center();

    return mapToScene(origin);
}

void GraphicsView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (itemAt(event->pos())) {
        // 双击在节点上，交给基类处理
        QGraphicsView::mouseDoubleClickEvent(event);
        return;
    }
    // 空白区域双击 → 弹出搜索框
    showNodeSearchPopup(event->globalPos());
}

QMenu *GraphicsView::createCanvasContextMenu(QPointF scenePos, QPoint globalPos)
{
    auto *menu = new QMenu(this);

    // 添加节点
    QAction *addNodeAction = menu->addAction(QStringLiteral("添加节点 (Add Node...)"));
    QObject::connect(addNodeAction, &QAction::triggered, [this, globalPos]() {
        showNodeSearchPopup(globalPos);
    });

    menu->addSeparator();

    // 复制
    QAction *copyAction = menu->addAction(QStringLiteral("复制 (Copy)\tCtrl+C"));
    QObject::connect(copyAction, &QAction::triggered, this, &GraphicsView::onCopySelectedObjects);

    // 粘贴（根据剪贴板状态自动 enable/disable）
    QAction *pasteAction = menu->addAction(QStringLiteral("粘贴 (Paste)\tCtrl+V"));
    pasteAction->setEnabled(hasValidPasteData());
    QObject::connect(pasteAction, &QAction::triggered, this, &GraphicsView::onPasteObjects);

    // 全选
    QAction *selectAllAction = menu->addAction(QStringLiteral("全选 (Select All)\tCtrl+A"));
    QObject::connect(selectAllAction, &QAction::triggered, this, &GraphicsView::onSelectAll);

    // 清除选择
    QAction *clearSelAction = menu->addAction(QStringLiteral("清除选择 (Clear Selection)\tEsc"));
    QObject::connect(clearSelAction, &QAction::triggered, scene(), &QGraphicsScene::clearSelection);

    menu->addSeparator();

    // 自适应大小
    QAction *zoomFitAction = menu->addAction(QStringLiteral("自适应大小 (Zoom to Fit)"));
    QObject::connect(zoomFitAction, &QAction::triggered, this, &GraphicsView::onZoomToFit);

    // 恢复100%缩放
    QAction *resetZoomAction = menu->addAction(QStringLiteral("恢复100%缩放 (Reset Zoom)"));
    QObject::connect(resetZoomAction, &QAction::triggered, this, &GraphicsView::onResetZoom);

    menu->addSeparator();

    // 清空画布
    QAction *clearCanvasAction = menu->addAction(QStringLiteral("清空画布 (Clear Canvas...)"));
    QObject::connect(clearCanvasAction, &QAction::triggered, this, &GraphicsView::onClearCanvas);

    // 导出为图片
    QAction *exportAction = menu->addAction(QStringLiteral("导出为图片 (Export as Image...)"));
    QObject::connect(exportAction, &QAction::triggered, this, &GraphicsView::onExportAsImage);

    return menu;
}

void GraphicsView::showNodeSearchPopup(QPoint globalPos)
{
    QStringList modelNames = getAllRegisteredModelNames();
    if (modelNames.isEmpty()) return;
    auto *popup = new NodeSearchPopup(modelNames, nodeScene(), mapToScene(mapFromGlobal(globalPos)), this);
    popup->move(globalPos);
    popup->show();
}

QStringList GraphicsView::getAllRegisteredModelNames() const
{
    QStringList names;
    auto *basicScene = const_cast<GraphicsView *>(this)->nodeScene();
    if (!basicScene) return names;

    auto *dataFlowModel = dynamic_cast<DataFlowGraphModel *>(&basicScene->graphModel());
    if (!dataFlowModel) return names;

    auto registry = dataFlowModel->dataModelRegistry();
    if (!registry) return names;

    auto const &creators = registry->registeredModelCreators();
    for (auto const &pair : creators) {
        names.append(pair.first);
    }
    names.sort(Qt::CaseInsensitive);
    return names;
}

void GraphicsView::onZoomToFit()
{
    centerScene();
}

void GraphicsView::onResetZoom()
{
    setupScale(1.0);
}

void GraphicsView::onClearCanvas()
{
    auto *basicScene = nodeScene();
    if (!basicScene) return;

    auto nodeIds = basicScene->graphModel().allNodeIds();
    if (nodeIds.empty()) return;

    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        QStringLiteral("清空画布"),
        QStringLiteral("确定要清空画布上的所有节点吗？此操作可通过 Ctrl+Z 撤销。"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        basicScene->clearScene();
    }
}

void GraphicsView::onExportAsImage()
{
    QString filePath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("导出画布为图片"),
        QString(),
        QStringLiteral("PNG Files (*.png);;JPEG Files (*.jpg *.jpeg)"));

    if (filePath.isEmpty()) return;

    // 获取所有节点的边界矩形
    QRectF sceneBounds = scene()->itemsBoundingRect();
    if (sceneBounds.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("导出"), QStringLiteral("画布上没有任何节点可导出。"));
        return;
    }
    // 添加边距
    sceneBounds.adjust(-50, -50, 50, 50);

    // 创建与场景等大的图像（使用 1:1 比例，不受当前缩放影响）
    QImage image(sceneBounds.size().toSize(), QImage::Format_ARGB32);
    image.fill(Qt::white);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    scene()->render(&painter, QRectF(QPointF(0, 0), sceneBounds.size()), sceneBounds);
    painter.end();

    if (!image.save(filePath)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), QStringLiteral("无法保存图片文件：") + filePath);
    }
}

void GraphicsView::onSelectAll()
{
    if (!scene()) return;
    for (QGraphicsItem *item : scene()->items()) {
        item->setSelected(true);
    }
}

bool GraphicsView::hasValidPasteData() const
{
    const QClipboard *clipboard = QApplication::clipboard();
    const QMimeData *mimeData = clipboard->mimeData();
    if (!mimeData || !mimeData->hasFormat("application/qt-nodes-graph"))
        return false;

    QByteArray data = mimeData->data("application/qt-nodes-graph");
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject())
        return false;

    QJsonObject json = doc.object();
    QJsonArray nodes = json["nodes"].toArray();
    return !nodes.isEmpty();
}

void GraphicsView::updatePasteActionState()
{
    if (_pasteAction) {
        _pasteAction->setEnabled(hasValidPasteData());
    }
}

void GraphicsView::focusInEvent(QFocusEvent *event)
{
    QGraphicsView::focusInEvent(event);
    // 强制在窗口或画布获取焦点时启用输入法
    setAttribute(Qt::WA_InputMethodEnabled, true);
    if (viewport()) {
        viewport()->setAttribute(Qt::WA_InputMethodEnabled, true);
    }
}
