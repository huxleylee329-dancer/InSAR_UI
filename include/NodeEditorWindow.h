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
#include <QComboBox>
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

// Forward declarations
class LeftSidebar;
class RightPanel;
class NodeGroupManager;

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

    // Getters for new components
    LeftSidebar* leftSidebar() const { return m_leftSidebar; }
    RightPanel* rightPanel() const { return m_rightPanel; }

private slots:
    // File operations
    void onNew();
    void onSave();
    void onLoad();

    // Edit operations
    void onClear();
    void onDelete();

    // Scene operations
    void onSceneModified(QtNodes::BasicGraphicsScene *);
    void onSceneLoaded();

    // Left sidebar signals
    void onNodeDoubleClicked(const QString &modelName);
    void onNodeSearchTextChanged(const QString &text);
    void onNodeItemClicked(const QString &modelName);
    void onWorkflowLoadRequested(const QString &filePath);

    // Right panel signals
    void onPropertyChanged(QtNodes::NodeId nodeId, const QString &property, const QVariant &value);

    // Toolbar operations
    void onWorkflowComboChanged(int index);
    void onBrowseWorkflows();
    void onRefreshNodes();
    void onQueueExecute();
    void onInterruptExecution();
    void onClearQueue();
    void onShowHistory();

    // Panel toggle operations
    void onToggleLeftSidebar();
    void onToggleRightPanel();

    // Group operations
    void onGroupSelection();

private:
    void setupUi();
    void setupToolbar();
    void setupMenu();
    void setupSceneInternal();
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
    // UI layout components
    QSplitter *m_splitter;

    // Sidebars
    LeftSidebar *m_leftSidebar;
    RightPanel *m_rightPanel;

    // Toolbar components
    QToolBar *m_toolbar;
    QComboBox *m_workflowCombo;
    QAction *m_actionNew;
    QAction *m_actionSave;
    QAction *m_actionLoad;
    QAction *m_actionClear;
    QAction *m_actionDelete;
    QAction *m_actionExit;
    QAction *m_actionBrowse;
    QAction *m_actionFavorite;
    QAction *m_actionRefresh;
    QAction *m_actionQueue;
    QAction *m_actionInterrupt;
    QAction *m_actionClearQueue;
    QAction *m_actionHistory;

    // Node Editor components
    std::shared_ptr<QtNodes::NodeDelegateModelRegistry> m_registry;
    QtNodes::DataFlowGraphModel *m_graphModel;
    QtNodes::DataFlowGraphicsScene *m_scene;
    PaletteGraphicsView *m_view;

    // Node Group Manager
    NodeGroupManager *m_groupManager;

    // State
    QString m_currentFilePath;

    // Project context
    QStandardItemModel* m_projectModel;
    QString m_projectPath;
    QString m_projectName;
};

#endif // NODEEDITORWINDOW_H
