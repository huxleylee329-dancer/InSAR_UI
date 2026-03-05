#ifndef NODEEDITORWINDOW_H
#define NODEEDITORWINDOW_H

#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLineEdit>
#include <QPushButton>
#include <QToolBar>
#include <QAction>
#include <QMenuBar>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QStatusBar>
#include <QStandardItemModel>
#include <QDrag>
#include <QMimeData>
#include <QEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMouseEvent>
#include <QPixmap>
#include <QPainter>
#include <memory>

// QtNodes headers
#include <QtNodes/DataFlowGraphicsScene>
#include <QtNodes/GraphicsView>
#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/NodeDelegateModelRegistry>
#include <QtNodes/ConnectionStyle>
#include <QtNodes/NodeStyle>
#include <QtNodes/GraphicsViewStyle>
#include <QtNodes/internal/UndoCommands.hpp>

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
        // Ignore double-click on leaf items
        QTreeWidget::mouseDoubleClickEvent(event);
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

private:
    QPoint m_dragStartPos;
};

// Custom GraphicsView to handle drops from palette
class PaletteGraphicsView : public QtNodes::GraphicsView
{
    Q_OBJECT
public:
    explicit PaletteGraphicsView(QtNodes::BasicGraphicsScene *scene, QWidget *parent = nullptr)
        : QtNodes::GraphicsView(scene, parent)
    {
        setAcceptDrops(true);
    }

protected:
    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (event->mimeData()->hasFormat("application/x-node-palette")) {
            event->acceptProposedAction();
            return;
        }
        QtNodes::GraphicsView::dragEnterEvent(event);
    }

    void dragMoveEvent(QDragMoveEvent *event) override
    {
        if (event->mimeData()->hasFormat("application/x-node-palette")) {
            event->acceptProposedAction();
            return;
        }
        QtNodes::GraphicsView::dragMoveEvent(event);
    }

    void dropEvent(QDropEvent *event) override
    {
        if (event->mimeData()->hasFormat("application/x-node-palette")) {
            QString modelName = QString::fromUtf8(
                event->mimeData()->data("application/x-node-palette"));

            QPointF scenePos = mapToScene(event->pos());

            QtNodes::BasicGraphicsScene *scene = nodeScene();
            if (scene) {
                scene->undoStack().push(
                    new QtNodes::CreateCommand(scene, modelName, scenePos));
            }

            event->acceptProposedAction();
            return;
        }
        QtNodes::GraphicsView::dropEvent(event);
    }
};

class NodeEditorWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit NodeEditorWindow(QWidget *parent = nullptr);
    ~NodeEditorWindow();

    // Project context methods
    void setProjectContext(QStandardItemModel* model, const QString& path, const QString& name);
    QStandardItemModel* projectModel() const;
    QString projectPath() const;
    QString projectName() const;

private slots:
    void onNew();
    void onSave();
    void onLoad();
    void onClear();
    void onDelete();
    void onSceneModified(QtNodes::BasicGraphicsScene *);
    void onSceneLoaded();
    void onSearchTextChanged(const QString &text);
    void onNodeItemDoubleClicked(QTreeWidgetItem *item, int column);
    void onNodeItemClicked(QTreeWidgetItem *item, int column);
    void onTogglePaletteCollapsed();

private:
    void setupUi();
    void setupToolbar();
    void setupMenu();
    void setupSceneInternal();
    void setupNodePalette();
    void populateNodeTree();
    void applyStyles();

    // Node palette full order configuration
    static struct PaletteOrder {
        QStringList topLevel;           // 顶级分类顺序
        QMap<QString, QStringList> subcategories;  // 顶级分类 -> 子分类顺序
        QMap<QString, QStringList> leafItems;      // 子分类路径 -> 叶子项顺序
    } getPaletteFullOrder();
    QString getSaveFilePath();
    QString getOpenFilePath();

private:
    // UI components
    QHBoxLayout *m_mainLayout;
    QSplitter *m_splitter;
    QToolBar *m_toolbar;
    QAction *m_actionNew;
    QAction *m_actionSave;
    QAction *m_actionLoad;
    QAction *m_actionClear;
    QAction *m_actionDelete;
    QAction *m_actionExit;

    // Node palette
    QWidget *m_nodePalette;
    QVBoxLayout *m_paletteLayout;
    QLineEdit *m_searchBox;
    NodeTreeWidget *m_nodeTree;
    QPushButton *m_closePaletteButton;
    QWidget *m_tabContainer;
    QPushButton *m_paletteTabButton;
    bool m_paletteCollapsed;

    // Node Editor components
    std::shared_ptr<QtNodes::NodeDelegateModelRegistry> m_registry;
    QtNodes::DataFlowGraphModel *m_graphModel;
    QtNodes::DataFlowGraphicsScene *m_scene;
    PaletteGraphicsView *m_view;

    // State
    QString m_currentFilePath;

    // Project context
    QStandardItemModel* m_projectModel;
    QString m_projectPath;
    QString m_projectName;
};

#endif // NODEEDITORWINDOW_H
