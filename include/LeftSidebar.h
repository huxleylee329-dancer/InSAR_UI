#ifndef LEFTSIDEBAR_H
#define LEFTSIDEBAR_H

#include <QWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QDrag>
#include <QMimeData>
#include <QEvent>
#include <QMouseEvent>
#include <QPixmap>
#include <QPainter>
#include <memory>

// QtNodes headers
#include <QtNodes/NodeDelegateModelRegistry>

// Forward declarations
class NodeEditorWindow;

// WorkflowBrowser - 工作流浏览器，用于显示和加载本地工作流文件
class WorkflowBrowser : public QWidget
{
    Q_OBJECT
public:
    explicit WorkflowBrowser(QWidget *parent = nullptr);

    void setWorkflowPath(const QString &path) { m_workflowPath = path; refresh(); }
    void refresh();

signals:
    void loadWorkflow(const QString &filePath);

private slots:
    void onSearchTextChanged(const QString &text);
    void onItemDoubleClicked(QTreeWidgetItem *item, int column);

private:
    QLineEdit *m_searchBox;
    QTreeWidget *m_workflowList;
    QString m_workflowPath;
};

// Custom tree widget for node palette with drag support
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
        // Ignore double-click on category items - let parent handle expand/collapse
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

/**
 * @brief LeftSidebar - 左侧边栏组件
 *
 * 包含两个标签页：
 * 1. 节点库 (Node Library) - 显示所有可用节点，支持拖拽到画布
 * 2. 工作流 (Workflows) - 浏览和加载本地保存的工作流文件
 */
class LeftSidebar : public QWidget
{
    Q_OBJECT

public:
    explicit LeftSidebar(QWidget *parent = nullptr);
    ~LeftSidebar();

    // 设置节点注册表
    void setRegistry(std::shared_ptr<QtNodes::NodeDelegateModelRegistry> registry);

    // 获取节点树控件（用于拖拽事件）
    NodeTreeWidget* nodeTreeWidget() const { return m_nodeTree; }

    // 工作流文件路径
    void setWorkflowPath(const QString &path) { m_workflowPath = path; }
    QString workflowPath() const { return m_workflowPath; }

signals:
    // 节点库相关信号
    void nodeDoubleClicked(const QString &modelName);
    void nodeSearchTextChanged(const QString &text);
    void nodeItemClicked(const QString &modelName);

    // 工作流相关信号
    void workflowLoadRequested(const QString &filePath);

private slots:
    void onNodeSearchTextChanged(const QString &text);
    void onNodeItemDoubleClicked(const QString &modelName);
    void onNodeItemClicked(QTreeWidgetItem *item, int column);

private:
    void setupUi();
    void setupNodeLibraryTab();
    void setupWorkflowsTab();
    void populateNodeTree();

    // UI components
    QTabWidget *m_tabWidget;
    QLineEdit *m_searchBox;
    NodeTreeWidget *m_nodeTree;
    WorkflowBrowser *m_workflowBrowser;

    // Data
    std::shared_ptr<QtNodes::NodeDelegateModelRegistry> m_registry;
    QString m_workflowPath;
};

#endif // LEFTSIDEBAR_H
