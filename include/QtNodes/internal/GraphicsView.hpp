#pragma once

#include <QtWidgets/QGraphicsView>

#include "Export.hpp"

class QMenu;

namespace QtNodes {

class BasicGraphicsScene;

/**
 * @brief A central view able to render objects from `BasicGraphicsScene`.
 */
class NODE_EDITOR_PUBLIC GraphicsView : public QGraphicsView
{
    Q_OBJECT
public:
    struct ScaleRange
    {
        double minimum = 0;
        double maximum = 0;
    };

public:
    GraphicsView(::QWidget *parent = Q_NULLPTR);
    GraphicsView(BasicGraphicsScene *scene, ::QWidget *parent = Q_NULLPTR);

    GraphicsView(const GraphicsView &) = delete;
    GraphicsView operator=(const GraphicsView &) = delete;

    QAction *clearSelectionAction() const;

    QAction *deleteSelectionAction() const;

    QAction *groupSelectionAction() const;

    void setScene(BasicGraphicsScene *scene);

    void centerScene();

    /// @brief max=0/min=0 indicates infinite zoom in/out
    void setScaleRange(double minimum = 0, double maximum = 0);

    void setScaleRange(ScaleRange range);

    double getScale() const;

public Q_SLOTS:
    void scaleUp();

    void scaleDown();

    void setupScale(double scale);

    void onDeleteSelectedObjects();

    void onDuplicateSelectedObjects();

    void onCopySelectedObjects();

    void onPasteObjects();

    void onGroupSelectedObjects();

    void onZoomToFit();

    void onResetZoom();

    void onClearCanvas();

    void onExportAsImage();

    void onSelectAll();

    /// 检查剪贴板是否包含合法的节点图数据
    bool hasValidPasteData() const;

    /// 根据剪贴板状态更新粘贴 Action 的 enable/disable
    void updatePasteActionState();

Q_SIGNALS:
    void scaleChanged(double scale);

    void groupSelected();

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;

    void wheelEvent(QWheelEvent *event) override;

    void keyPressEvent(QKeyEvent *event) override;

    void keyReleaseEvent(QKeyEvent *event) override;

    void mousePressEvent(QMouseEvent *event) override;

    void mouseMoveEvent(QMouseEvent *event) override;

    void mouseDoubleClickEvent(QMouseEvent *event) override;

    void drawBackground(QPainter *painter, const QRectF &r) override;

    void showEvent(QShowEvent *event) override;

protected:
    BasicGraphicsScene *nodeScene();

    /// Computes scene position for pasting the copied/duplicated node groups.
    QPointF scenePastePosition();

private:
    // 显示节点搜索弹窗
    void showNodeSearchPopup(QPoint globalPos);
    // 创建画布右键菜单
    QMenu *createCanvasContextMenu(QPointF scenePos, QPoint globalPos);
    // 获取所有已注册的节点模型名称
    QStringList getAllRegisteredModelNames() const;

    QAction *_clearSelectionAction = nullptr;
    QAction *_deleteSelectionAction = nullptr;
    QAction *_duplicateSelectionAction = nullptr;
    QAction *_copySelectionAction = nullptr;
    QAction *_pasteAction = nullptr;
    QAction *_groupSelectionAction = nullptr;

    QPointF _clickPos;
    ScaleRange _scaleRange;
};
} // namespace QtNodes
