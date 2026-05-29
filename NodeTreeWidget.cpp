#include "include/NodeTreeWidget.h"
#include <QMouseEvent>
#include <QMimeData>
#include <QDrag>
#include <QPixmap>
#include <QPainter>

NodeTreeWidget::NodeTreeWidget(QWidget *parent)
    : QTreeWidget(parent)
{
    setSelectionMode(QAbstractItemView::SingleSelection);
    setDragEnabled(false);  // Disable built-in drag
}

void NodeTreeWidget::mousePressEvent(QMouseEvent *event)
{
    m_dragStartPos = event->pos();
    QTreeWidget::mousePressEvent(event);
}

void NodeTreeWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    // Ignore double-click on category items
    QTreeWidgetItem *item = itemAt(event->pos());
    if (item && item->childCount() > 0)
    {
        QTreeWidget::mouseDoubleClickEvent(event);
        return;
    }
    // Emit signal for leaf items
    if (item && item->childCount() == 0)
    {
        QString modelName = item->data(0, Qt::UserRole).toString();
        emit leafItemDoubleClicked(modelName);
    }
}

void NodeTreeWidget::mouseMoveEvent(QMouseEvent *event)
{
    // Start drag for leaf items (only when left button is pressed and after moving more than 10 pixels)
    if (event->buttons() & Qt::LeftButton && (event->pos() - m_dragStartPos).manhattanLength() > 10)
    {
        // Use the item at the original click position
        QTreeWidgetItem *pressItem = itemAt(m_dragStartPos);
        if (pressItem && pressItem->childCount() == 0)
        {
            QString modelName = pressItem->data(0, Qt::UserRole).toString();
            if (!modelName.isEmpty())
            {
                QMimeData *mimeData = new QMimeData();
                mimeData->setText(modelName);
                mimeData->setData("application/x-node-palette", modelName.toUtf8());

                QDrag *drag = new QDrag(this);
                drag->setMimeData(mimeData);

                // Create a simple drag preview
                QPixmap pixmap(120, 24);
                pixmap.fill(Qt::white);
                QPainter painter(&pixmap);
                painter.setPen(Qt::black);
                painter.drawText(pixmap.rect(), Qt::AlignCenter, modelName);
                drag->setPixmap(pixmap);

                drag->exec(Qt::CopyAction);
                return;
            }
        }
    }
    QTreeWidget::mouseMoveEvent(event);
}
