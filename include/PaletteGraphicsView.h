#ifndef PALETTEGRAPHICSVIEW_H
#define PALETTEGRAPHICSVIEW_H

#include <QObject>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMouseEvent>
#include <QString>
#include <QMimeData>
#include "QtNodes/GraphicsView"
#include "QtNodes/BasicGraphicsScene"
#include "QtNodes/internal/Definitions.hpp"

/**
 * @brief Custom GraphicsView to handle drops from node palette
 *
 * Extends QtNodes GraphicsView to handle drag-and-drop from the node palette
 * sidebar, and emits background click signal for deselection.
 */
class PaletteGraphicsView : public QtNodes::GraphicsView
{
    Q_OBJECT
public:
    explicit PaletteGraphicsView(QtNodes::BasicGraphicsScene *scene, QWidget *parent = nullptr);

signals:
    void nodeDropped(QtNodes::NodeId nodeId, const QString &modelName);
    void backgroundClicked();  // 点击画布背景时发射

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
};

#endif // PALETTEGRAPHICSVIEW_H
