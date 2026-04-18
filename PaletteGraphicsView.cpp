#include "include/PaletteGraphicsView.h"
#include <QtNodes/internal/BasicGraphicsScene.hpp>
#include <QtNodes/internal/DataFlowGraphModel.hpp>
#include <QtNodes/internal/Definitions.hpp>

PaletteGraphicsView::PaletteGraphicsView(QtNodes::BasicGraphicsScene *scene, QWidget *parent)
    : QtNodes::GraphicsView(scene, parent)
{
    // Enable drop events for this view
    setAcceptDrops(true);
}

void PaletteGraphicsView::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasFormat("application/x-node-palette")) {
        event->acceptProposedAction();
    } else {
        QtNodes::GraphicsView::dragEnterEvent(event);
    }
}

void PaletteGraphicsView::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasFormat("application/x-node-palette")) {
        event->acceptProposedAction();
    } else {
        QtNodes::GraphicsView::dragMoveEvent(event);
    }
}

void PaletteGraphicsView::dropEvent(QDropEvent *event)
{
    if (event->mimeData()->hasFormat("application/x-node-palette")) {
        QByteArray data = event->mimeData()->data("application/x-node-palette");
        QString modelName = QString::fromUtf8(data);

        QPointF scenePos = mapToScene(event->pos());

        // Get the graph model and add the node
        auto* basicScene = qobject_cast<QtNodes::BasicGraphicsScene*>(scene());
        if (!basicScene) {
            event->ignore();
            return;
        }

        auto& graphModel = basicScene->graphModel();
        auto* dataFlowModel = dynamic_cast<QtNodes::DataFlowGraphModel*>(&graphModel);
        if (!dataFlowModel) {
            event->ignore();
            return;
        }

        QtNodes::NodeId nodeId = dataFlowModel->addNode(modelName);

        // Set node position
        QVariant posVar = QVariant::fromValue(scenePos);
        dataFlowModel->setNodeData(nodeId, QtNodes::NodeRole::Position, posVar);

        emit nodeDropped(nodeId, modelName);

        event->acceptProposedAction();
    } else {
        QtNodes::GraphicsView::dropEvent(event);
    }
}

void PaletteGraphicsView::mousePressEvent(QMouseEvent *event)
{
    QtNodes::GraphicsView::mousePressEvent(event);

    if (scene()->itemAt(mapToScene(event->pos()), transform()) == nullptr) {
        emit backgroundClicked();
    }
}
