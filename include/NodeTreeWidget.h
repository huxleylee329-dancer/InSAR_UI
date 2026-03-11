#ifndef NODETREEWIDGET_H
#define NODETREEWIDGET_H

#include <QTreeWidget>
#include <QMouseEvent>
#include <QMimeData>
#include <QDrag>
#include <QPixmap>
#include <QPainter>

/**
 * @brief NodeTreeWidget - Custom tree widget for node palette with drag support
 *
 * Used by NodeLibraryWidget to provide drag-and-drop functionality for node items
 */
class NodeTreeWidget : public QTreeWidget
{
    Q_OBJECT
public:
    explicit NodeTreeWidget(QWidget *parent = nullptr) : QTreeWidget(parent)
    {
        setSelectionMode(QAbstractItemView::SingleSelection);
        setDragEnabled(false);  // Disable built-in drag
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        m_dragStartPos = event->pos();
        QTreeWidget::mousePressEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
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

    void mouseMoveEvent(QMouseEvent *event) override
    {
        QTreeWidgetItem *item = itemAt(event->pos());
        // Don't drag category items
        if (item && item->childCount() > 0)
        {
            QTreeWidget::mouseMoveEvent(event);
            return;
        }
        // Start drag for leaf items (only after moving more than 10 pixels)
        if (event->buttons() & Qt::LeftButton && (event->pos() - m_dragStartPos).manhattanLength() > 10)
        {
            QTreeWidgetItem *current = currentItem();
            if (current && current->childCount() == 0)
            {
                QString modelName = current->data(0, Qt::UserRole).toString();
                if (!modelName.isEmpty())
                {
                    QMimeData *mimeData = new QMimeData();
                    mimeData->setText(modelName);
                    mimeData->setData("application/x-node-palette", modelName.toUtf8());

                    QDrag *drag = new QDrag(this);
                    drag->setMimeData(mimeData);

                    // Create a simple drag pixmap
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

signals:
    void leafItemDoubleClicked(const QString &modelName);

private:
    QPoint m_dragStartPos;
};

#endif // NODETREEWIDGET_H
