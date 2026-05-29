#ifndef NODETREEWIDGET_H
#define NODETREEWIDGET_H

#include <QTreeWidget>

class QMouseEvent;

/**
 * @brief NodeTreeWidget - Custom tree widget for node palette with drag support
 *
 * Used by NodeLibraryWidget to provide drag-and-drop functionality for node items
 */
class NodeTreeWidget : public QTreeWidget
{
    Q_OBJECT
public:
    explicit NodeTreeWidget(QWidget *parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

signals:
    void leafItemDoubleClicked(const QString &modelName);

private:
    QPoint m_dragStartPos;
};

#endif // NODETREEWIDGET_H
