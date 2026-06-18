#ifndef WORKFLOWUI_H
#define WORKFLOWUI_H

#include "IApplicationInterface.h"
#include <QWidget>
#include <QVBoxLayout>
#include <QTreeWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QToolBar>
#include <QAction>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardItemModel>
#include "PaletteOrder.h"

// ADS (Qt Advanced Docking System)
#include "ads_globals.h"
#include "DockManager.h"
#include "DockWidget.h"

// QtNodes headers
#include <QtNodes/DataFlowGraphicsScene>
#include <QtNodes/GraphicsView>
#include <QtNodes/DataFlowGraphModel>
#include "QtNodes/internal/ExecutableDataFlowGraphModel.hpp"
#include "QtNodes/internal/ExecutableNodeGeometry.hpp"
#include "QtNodes/internal/ExecutableNodePainter.hpp"
#include "QtNodes/internal/NodeDetailWindow.hpp"
#include "QtNodes/internal/NodeDetailOverlay.hpp"
#include "QtNodes/internal/NodeDetailAnimationController.hpp"
#include "QtNodes/internal/ExecutableNodeDelegateModel.hpp"
#include <QtNodes/NodeDelegateModelRegistry>
#include <QtNodes/Internal/NodeGraphicsObject.hpp>
#include <QtNodes/ConnectionStyle>
#include <QtNodes/NodeStyle>
#include <QtNodes/GraphicsViewStyle>
#include <QtNodes/internal/Definitions.hpp>

#include "PaletteGraphicsView.h"

// Forward declarations
class WorkflowBrowser;
class NodeLibraryWidget;
class NodeTreeWidget;
class PropertyEditor;
class QueueManagerWidget;
class NodeGroupManager;

/**
 * @brief 工作流节点编辑界面
 *
 * 从原NodeEditorWindow重构而来，改为QWidget，实现IApplicationInterface接口
 */
class WorkflowUI : public QWidget, public IApplicationInterface
{
    Q_OBJECT

public:
    explicit WorkflowUI(QWidget *parent = nullptr);
    ~WorkflowUI() override;

    // IApplicationInterface interface
    QWidget* centralWidget() override;
    QList<QToolBar*> toolBars() override;
    void activate() override;
    void deactivate() override;
    QString id() const override;
    QString displayName() const override;

    // Project context methods
    void setProjectContext(QStandardItemModel* model, const QString& path, const QString& name, XMLFile* projectXml = nullptr) override;
    QStandardItemModel* projectModel() const override;
    QString projectPath() const override;
    QString projectName() const override;
    XMLFile* projectXml() const override;

    // Theme methods
    void initTheme() override;
    void setTheme(const QString &theme) override;

    // Refresh project tree after import
    void refreshProjectTree() override;

    // Clear interface
    void clear() override;

    // Getters
    NodeLibraryWidget* nodeLibrary() const { return m_nodeLibrary; }
    PropertyEditor* propertyEditor() const { return m_propertyEditor; }
    WorkflowBrowser* workflowBrowser() const { return m_workflowBrowser; }

    // 工作流状态保存/恢复（供项目 save/load 使用）
    QJsonObject saveWorkflowToJson() const;
    void loadWorkflowFromJson(const QJsonObject& json);

    // 获取面板显示/隐藏的Action（供主窗口添加到视图菜单）
    QList<QAction*> getViewActions() const;

private slots:
    // File operations
    void onNew();
    void onExport();
    void onImport();

    // Edit operations
    void onClear();
    void onExportCanvasAsImage();
    void onClearCanvas();
    void onDelete();

    // Scene operations
    void onSceneModified(QtNodes::BasicGraphicsScene *);
    void onSceneLoaded();

    // Left sidebar signals
    void onNodeDoubleClicked(const QString &modelName);
    void onNodeSearchTextChanged(const QString &text);
    void onNodeItemClicked(const QString &modelName);
    void onNodeDropped(QtNodes::NodeId nodeId, const QString &modelName);

    // Workflow browser signals
    void onWorkflowLoadRequested(const QString &filePath);

    // Property editor signals
    void onPropertyChanged(QtNodes::NodeId nodeId, const QString &property, const QVariant &value);
    void onPortDataChanged(QtNodes::NodeId nodeId, QtNodes::PortType portType, int portIndex, const QString& fieldKey, const QString& newValue);

    // Toolbar operations
    void onBrowseWorkflows();
    void onRefreshNodes();
    void onQueueExecute();
    void onInterruptExecution();
    void onClearQueue();
    void onShowHistory();

    // Panel toggle operations
    void onToggleNodesDock();
    void onToggleWorkflowsDock();
    void onTogglePropertiesDock();
    void onToggleQueueDock();

    // Group operations
    void onGroupSelection();
    void onNodeCreated(QtNodes::NodeId const nodeId);

signals:
    // 工作流被修改信号
    void workflowModified();

    // 节点运行状态与进度信号
    void nodeProgressUpdated(const QString& nodeCaption, int percent);
    void nodeExecutionStarted(const QString& nodeCaption);
    void nodeExecutionFinished(const QString& nodeCaption);
    void nodeExecutionError(const QString& nodeCaption, const QString& error);

private:
    void setupUi();
    void setupToolbar();
    void setupMenu();
    void setupSceneInternal();
    void applyStyles();

    // Node palette full order configuration
    PaletteOrder getPaletteFullOrder();

    QString getSaveFilePath();
    QString getOpenFilePath();

    void openDetailView(QtNodes::NodeGraphicsObject* ngo, QtNodes::ExecutableNodeDelegateModel* execModel);
    void cleanupDetailWindow();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    // Use ADS namespace alias
    typedef ads::CDockManager DockManager;
    typedef ads::CDockWidget DockWidget;

    // UI layout components
    DockManager *m_dockManager;

    // ADS dock widgets
    DockWidget *m_nodesDockWidget;
    DockWidget *m_workflowsDockWidget;
    DockWidget *m_canvasDockWidget;
    DockWidget *m_propertiesDockWidget;
    DockWidget *m_queueDockWidget;

    // Individual dock widget components
    WorkflowBrowser *m_workflowBrowser;
    NodeLibraryWidget *m_nodeLibrary;
    PropertyEditor *m_propertyEditor;
    QueueManagerWidget *m_queueManager;

    // Toolbar components
    QToolBar *m_toolbar = nullptr;
    QAction *m_actionNew;
    QAction *m_actionExport;
    QAction *m_actionImport;
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

    // Panel toggle actions
    QAction *m_actionToggleNodes;
    QAction *m_actionToggleWorkflows;
    QAction *m_actionToggleProperties;
    QAction *m_actionToggleQueue;

    // Node Editor components
    std::shared_ptr<QtNodes::NodeDelegateModelRegistry> m_registry;
    QtNodes::ExecutableDataFlowGraphModel *m_graphModel;
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
    XMLFile* m_projectXml = nullptr;
    
    // Theme
    QString m_currentTheme;

    // Track if initial layout (panel widths) has been performed
    bool m_initialLayoutDone = false;

    // Toolbar buttons that need theme-aware icon recoloring
    QToolButton *m_btnSync = nullptr;
    QToolButton *m_btnQueue = nullptr;
    QToolButton *m_btnDrop = nullptr;
    QToolButton *m_btnLogs = nullptr;
    QToolButton *m_btnDel = nullptr;

    // Detail view components for executable nodes
    QtNodes::NodeDetailWindow *_detailWindow = nullptr;
    QtNodes::NodeDetailOverlay *_detailOverlay = nullptr;
    QtNodes::NodeDetailAnimationController *_animationController = nullptr;
};

#endif // WORKFLOWUI_H
