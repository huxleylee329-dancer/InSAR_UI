#include "NodeEditorWindow.h"
#include "DockWidgets.h"
#include "NodeGroupManager.h"
#include "NodeModels.h"

// ADS (Qt Advanced Docking System)
#include "ads_globals.h"
#include "DockManager.h"
#include "DockWidget.h"

#include <QPainter>
#include <QTimer>
#include <QtWidgets/QGraphicsItem>
#include <QtWidgets/QGraphicsObject>
#include <QtNodes/DataFlowGraphicsScene>
#include <QtNodes/GraphicsView>
#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/NodeDelegateModelRegistry>
#include <QtNodes/ConnectionStyle>
#include <QtNodes/NodeStyle>
#include <QtNodes/GraphicsViewStyle>
#include <QtNodes/internal/Definitions.hpp>
#include <QtNodes/internal/NodeGraphicsObject.hpp>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QToolBar>
#include <QAction>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QStatusBar>
#include <QList>
#include <QMap>
#include <QPair>
#include <QComboBox>
#include <algorithm>
#include <memory>

// ============================================================================
// Node Palette Full Order Configuration
// ============================================================================
// 在这里修改来控制所有级别的显示顺序
// ============================================================================
NodeEditorWindow::PaletteOrder NodeEditorWindow::getPaletteFullOrder()
{
    PaletteOrder order;

    // ===== 1. 顶级分类顺序 =====
    order.topLevel = QStringList{
        "Data Import",    // 第一级分类
        "Preprocessing",  // 第二级分类
        "Test"            // 第三级分类
    };

    // ===== 2. 子分类顺序 =====
    // 格式: 顶级分类名 -> 子分类列表（按顺序）
    order.subcategories["Data Import"] = QStringList{
        "Sentinel-1",       // Data Import 下的第一个子分类
        "TerraSAR-X",       // 第二个
        "COSMO-SkyMed",     // 第三个
        "ALOS-2"            // 第四个
    };

    order.subcategories["Preprocessing"] = QStringList{
        "Sentinel-1"        // Preprocessing 下的第一个子分类
    };

    // ===== 3. 叶子项顺序 =====
    // 格式: 子分类完整路径 -> 叶子项列表（按顺序）
    order.leafItems["Data Import/Sentinel-1"] = QStringList{
        "Single Import",      // Sentinel-1 下的第一个
        "Batch Import"        // Sentinel-1 下的第二个
    };

    order.leafItems["Data Import/TerraSAR-X"] = QStringList{
        "Single Import",
        "Batch Import"
    };

    order.leafItems["Data Import/COSMO-SkyMed"] = QStringList{
        "Batch Import"
    };

    order.leafItems["Data Import/ALOS-2"] = QStringList{
        "Batch Import"
    };

    // Preprocessing 类叶子项顺序
    order.leafItems["Preprocessing/Sentinel-1"] = QStringList{
        "Deburst",          // Sentinel-1 预处理节点：去突刺
        "Frame Merge",      // 帧拼接
        "Swath Merge"       // 条带拼接
    };

    // Test 类叶子项顺序
    order.leafItems["Test"] = QStringList{
        "SimpleSource",
        "SimpleDisplay",
        "SimpleMath"
    };

    return order;
}

NodeEditorWindow::NodeEditorWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_dockManager(nullptr)
    , m_nodesDockWidget(nullptr)
    , m_workflowsDockWidget(nullptr)
    , m_canvasDockWidget(nullptr)
    , m_propertiesDockWidget(nullptr)
    , m_queueDockWidget(nullptr)
    , m_workflowBrowser(nullptr)
    , m_nodeLibrary(nullptr)
    , m_propertyEditor(nullptr)
    , m_queueManager(nullptr)
    , m_toolbar(nullptr)
    , m_workflowCombo(nullptr)
    , m_actionNew(nullptr)
    , m_actionSave(nullptr)
    , m_actionLoad(nullptr)
    , m_actionClear(nullptr)
    , m_actionDelete(nullptr)
    , m_actionExit(nullptr)
    , m_actionBrowse(nullptr)
    , m_actionFavorite(nullptr)
    , m_actionRefresh(nullptr)
    , m_actionQueue(nullptr)
    , m_actionInterrupt(nullptr)
    , m_actionClearQueue(nullptr)
    , m_actionHistory(nullptr)
    , m_graphModel(nullptr)
    , m_scene(nullptr)
    , m_view(nullptr)
    , m_groupManager(nullptr)
    , m_projectModel(nullptr)
    , m_projectPath()
    , m_projectName()
{
    setWindowTitle("InSAR Node Editor");
    resize(1400, 900);

    setupUi();
    setupToolbar();
    setupMenu();

    // Initialize node registry
    m_registry = QtNodes::registerInSARNodeModels();

    setupSceneInternal();
}

NodeEditorWindow::~NodeEditorWindow()
{
    // Dock widgets are managed by CDockManager
    delete m_groupManager;
}

void NodeEditorWindow::setupSceneInternal()
{
    if (m_scene) {
        return;
    }

    // Create graph model
    m_graphModel = new QtNodes::DataFlowGraphModel(m_registry);

    // Create scene
    m_scene = new QtNodes::DataFlowGraphicsScene(*m_graphModel, this);

    // Create view (custom subclass to handle drag & drop)
    m_view = new PaletteGraphicsView(m_scene);

    // Setup ADS Dock Manager with dock widgets

    // 1. Create canvas dock widget (central widget)
    m_canvasDockWidget = new ads::CDockWidget("Canvas", this);
    m_canvasDockWidget->setWidget(m_view);
    m_canvasDockWidget->setFeature(ads::CDockWidget::DockWidgetFloatable, false);
    m_dockManager->setCentralWidget(m_canvasDockWidget);

    // 2. Create Nodes dock widget (left side)
    m_nodesDockWidget = new ads::CDockWidget("Nodes", this);
    m_nodesDockWidget->setWidget(m_nodeLibrary);
    m_nodesDockWidget->setFeature(ads::CDockWidget::DockWidgetFloatable, false);
    m_dockManager->addDockWidget(ads::LeftDockWidgetArea, m_nodesDockWidget);

    // 3. Create Workflows dock widget (left side)
    m_workflowsDockWidget = new ads::CDockWidget("Workflows", this);
    m_workflowsDockWidget->setWidget(m_workflowBrowser);
    m_workflowsDockWidget->setFeature(ads::CDockWidget::DockWidgetFloatable, false);
    m_dockManager->addDockWidget(ads::LeftDockWidgetArea, m_workflowsDockWidget, m_nodesDockWidget->dockAreaWidget());

    // 4. Create Properties dock widget (right side)
    m_propertiesDockWidget = new ads::CDockWidget("Properties", this);
    m_propertiesDockWidget->setWidget(m_propertyEditor);
    m_propertiesDockWidget->setFeature(ads::CDockWidget::DockWidgetFloatable, false);
    m_dockManager->addDockWidget(ads::RightDockWidgetArea, m_propertiesDockWidget);

    // 5. Create Queue dock widget (right side)
    m_queueDockWidget = new ads::CDockWidget("Queue", this);
    m_queueDockWidget->setWidget(m_queueManager);
    m_queueDockWidget->setFeature(ads::CDockWidget::DockWidgetFloatable, false);
    m_dockManager->addDockWidget(ads::RightDockWidgetArea, m_queueDockWidget, m_propertiesDockWidget->dockAreaWidget());

    // Pass registry to node library
    m_nodeLibrary->setRegistry(m_registry);

    // Pass graph model to property editor
    m_propertyEditor->setGraphModel(m_graphModel);

    // Set workflow path for workflow browser
    if (!m_projectPath.isEmpty())
    {
        QDir dir(m_projectPath);
        dir.cdUp();
        QString workflowDir = dir.filePath("workflows");
        m_workflowBrowser->setWorkflowPath(workflowDir);
    }

    // Create and configure group manager
    m_groupManager = new NodeGroupManager(this);
    m_groupManager->setGraphModel(m_graphModel);
    m_groupManager->setScene(m_scene);

    // Connect to scene modification signal
    connect(m_scene, &QtNodes::BasicGraphicsScene::modified, this, &NodeEditorWindow::onSceneModified);

    // Connect to scene selection changes for property panel
    connect(m_scene, &QtNodes::BasicGraphicsScene::nodeSelected, this, [this](QtNodes::NodeId nodeId) {
        m_propertyEditor->setSelectedNode(nodeId);
    });

    // Connect to node clicked to update property panel
    connect(m_scene, &QtNodes::BasicGraphicsScene::nodeClicked, this, [this](QtNodes::NodeId nodeId) {
        auto selectedNodes = m_scene->selectedNodes();
        if (selectedNodes.size() == 1) {
            m_propertyEditor->setSelectedNode(selectedNodes[0]);
        } else {
            m_propertyEditor->clearSelection();
        }
    });

    // Connect to group selection signal
    connect(m_view, &QtNodes::GraphicsView::groupSelected, this, &NodeEditorWindow::onGroupSelection);

    // Set view properties
    m_view->setRenderHint(QPainter::Antialiasing);
    m_view->setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    m_view->setDragMode(QGraphicsView::ScrollHandDrag);
    m_view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
}

void NodeEditorWindow::setupUi()
{
    // Create ADS Dock Manager
    m_dockManager = new ads::CDockManager(this);

    // Create dock widget components
    m_nodeLibrary = new NodeLibraryWidget();
    m_workflowBrowser = new WorkflowBrowser();
    m_propertyEditor = new PropertyEditor();
    m_queueManager = new QueueManagerWidget();

    // Connect node library signals
    connect(m_nodeLibrary, &NodeLibraryWidget::nodeDoubleClicked,
            this, &NodeEditorWindow::onNodeDoubleClicked);
    connect(m_nodeLibrary, &NodeLibraryWidget::nodeSearchTextChanged,
            this, &NodeEditorWindow::onNodeSearchTextChanged);
    connect(m_nodeLibrary, &NodeLibraryWidget::nodeItemClicked,
            this, &NodeEditorWindow::onNodeItemClicked);

    // Connect workflow browser signals
    connect(m_workflowBrowser, &WorkflowBrowser::loadWorkflow,
            this, &NodeEditorWindow::onWorkflowLoadRequested);

    // Connect property editor signals
    connect(m_propertyEditor, &PropertyEditor::propertyChanged,
            this, &NodeEditorWindow::onPropertyChanged);
}

void NodeEditorWindow::setupToolbar()
{
    m_toolbar = addToolBar("Main Toolbar");
    m_toolbar->setMovable(false);  // Keep toolbar fixed at top

    // Workflow dropdown
    QLabel *workflowLabel = new QLabel("Workflow:");
    m_toolbar->addWidget(workflowLabel);

    m_workflowCombo = new QComboBox();
    m_workflowCombo->setMinimumWidth(120);
    m_workflowCombo->addItem("Blank");
    m_workflowCombo->addItem("Default");
    m_workflowCombo->addItem("Open...");
    m_toolbar->addWidget(m_workflowCombo);
    connect(m_workflowCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &NodeEditorWindow::onWorkflowComboChanged);

    m_toolbar->addSeparator();

    // File actions
    m_actionNew = new QAction("New", this);
    m_actionNew->setShortcut(QKeySequence::New);
    m_actionNew->setStatusTip("Create new graph");
    connect(m_actionNew, &QAction::triggered, this, &NodeEditorWindow::onNew);
    m_toolbar->addAction(m_actionNew);

    m_actionSave = new QAction("Save", this);
    m_actionSave->setShortcut(QKeySequence::Save);
    m_actionSave->setStatusTip("Save graph to file");
    connect(m_actionSave, &QAction::triggered, this, &NodeEditorWindow::onSave);
    m_toolbar->addAction(m_actionSave);

    m_actionLoad = new QAction("Load", this);
    m_actionLoad->setShortcut(QKeySequence::Open);
    m_actionLoad->setStatusTip("Load graph from file");
    connect(m_actionLoad, &QAction::triggered, this, &NodeEditorWindow::onLoad);
    m_toolbar->addAction(m_actionLoad);

    m_toolbar->addSeparator();

    // Browse and Favorite
    m_actionBrowse = new QAction("Browse", this);
    m_actionBrowse->setStatusTip("Browse local workflows");
    connect(m_actionBrowse, &QAction::triggered, this, &NodeEditorWindow::onBrowseWorkflows);
    m_toolbar->addAction(m_actionBrowse);

    m_actionFavorite = new QAction("Favorite", this);
    m_actionFavorite->setStatusTip("Show favorite workflows");
    connect(m_actionFavorite, &QAction::triggered, this, [this]() {
        QMessageBox::information(this, "Favorite Workflows", "Favorite workflows feature coming soon.");
    });
    m_toolbar->addAction(m_actionFavorite);

    m_actionRefresh = new QAction("Refresh", this);
    m_actionRefresh->setStatusTip("Refresh node definitions");
    connect(m_actionRefresh, &QAction::triggered, this, &NodeEditorWindow::onRefreshNodes);
    m_toolbar->addAction(m_actionRefresh);

    m_toolbar->addSeparator();

    // Panel toggle actions
    QAction *actionToggleNodes = new QAction("< Nodes", this);
    actionToggleNodes->setStatusTip("Toggle Nodes panel");
    actionToggleNodes->setCheckable(false);
    connect(actionToggleNodes, &QAction::triggered, this, &NodeEditorWindow::onToggleNodesDock);
    m_toolbar->addAction(actionToggleNodes);

    QAction *actionToggleWorkflows = new QAction("< Workflows", this);
    actionToggleWorkflows->setStatusTip("Toggle Workflows panel");
    actionToggleWorkflows->setCheckable(false);
    connect(actionToggleWorkflows, &QAction::triggered, this, &NodeEditorWindow::onToggleWorkflowsDock);
    m_toolbar->addAction(actionToggleWorkflows);

    QAction *actionToggleProperties = new QAction("Properties >", this);
    actionToggleProperties->setStatusTip("Toggle Properties panel");
    actionToggleProperties->setCheckable(false);
    connect(actionToggleProperties, &QAction::triggered, this, &NodeEditorWindow::onTogglePropertiesDock);
    m_toolbar->addAction(actionToggleProperties);

    QAction *actionToggleQueue = new QAction("Queue >", this);
    actionToggleQueue->setStatusTip("Toggle Queue panel");
    actionToggleQueue->setCheckable(false);
    connect(actionToggleQueue, &QAction::triggered, this, &NodeEditorWindow::onToggleQueueDock);
    m_toolbar->addAction(actionToggleQueue);

    m_toolbar->addSeparator();

    // Queue actions
    m_actionQueue = new QAction("Queue", this);
    m_actionQueue->setStatusTip("Queue prompt - execute current workflow");
    connect(m_actionQueue, &QAction::triggered, this, &NodeEditorWindow::onQueueExecute);
    m_toolbar->addAction(m_actionQueue);

    m_actionInterrupt = new QAction("Interrupt", this);
    m_actionInterrupt->setStatusTip("Interrupt current execution");
    connect(m_actionInterrupt, &QAction::triggered, this, &NodeEditorWindow::onInterruptExecution);
    m_toolbar->addAction(m_actionInterrupt);

    m_actionClearQueue = new QAction("Clear Queue", this);
    m_actionClearQueue->setStatusTip("Clear all queued tasks");
    connect(m_actionClearQueue, &QAction::triggered, this, &NodeEditorWindow::onClearQueue);
    m_toolbar->addAction(m_actionClearQueue);

    m_actionHistory = new QAction("History", this);
    m_actionHistory->setStatusTip("Show execution history");
    connect(m_actionHistory, &QAction::triggered, this, &NodeEditorWindow::onShowHistory);
    m_toolbar->addAction(m_actionHistory);

    m_toolbar->addSeparator();

    // Edit actions
    m_actionClear = new QAction("Clear", this);
    m_actionClear->setStatusTip("Clear all nodes");
    connect(m_actionClear, &QAction::triggered, this, &NodeEditorWindow::onClear);
    m_toolbar->addAction(m_actionClear);

    m_actionDelete = new QAction("Delete", this);
    m_actionDelete->setShortcut(QKeySequence::Delete);
    m_actionDelete->setStatusTip("Delete selected nodes and connections");
    connect(m_actionDelete, &QAction::triggered, this, &NodeEditorWindow::onDelete);
    m_toolbar->addAction(m_actionDelete);

    m_toolbar->addSeparator();

    m_actionExit = new QAction("Exit", this);
    m_actionExit->setShortcut(QKeySequence::Quit);
    m_actionExit->setStatusTip("Exit node editor");
    connect(m_actionExit, &QAction::triggered, this, &NodeEditorWindow::close);
    m_toolbar->addAction(m_actionExit);

    statusBar()->showMessage("Ready");
}

void NodeEditorWindow::setupMenu()
{
    auto *menuFile = menuBar()->addMenu("File");
    menuFile->addAction(m_actionNew);
    menuFile->addAction(m_actionSave);
    menuFile->addAction(m_actionLoad);
    menuFile->addSeparator();
    menuFile->addAction(m_actionExit);

    auto *menuEdit = menuBar()->addMenu("Edit");
    menuEdit->addAction(m_actionDelete);
    menuEdit->addSeparator();
    menuEdit->addAction(m_actionClear);

    auto *menuView = menuBar()->addMenu("View");
    menuEdit->addAction(m_actionBrowse);
    menuEdit->addAction(m_actionHistory);

    auto *menuHelp = menuBar()->addMenu("Help");
    auto *actionAbout = new QAction("About", this);
    connect(actionAbout, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, "About",
                         "InSAR Node Editor\n\n"
                         "Visual node-based workflow editor\n"
                         "For InSAR data processing workflow design");
    });
    menuHelp->addAction(actionAbout);
}

void NodeEditorWindow::applyStyles()
{
    // Set dark theme styles
    QtNodes::ConnectionStyle::setConnectionStyle(
        "{"
        "\"ConnectionStyle\": {"
        "\"ConstructionColor\": \"gray\","
        "\"NormalColor\": \"#00AA00\","
        "\"SelectedColor\": \"#FF0000\","
        "\"SelectedHaloColor\": \"#FFAA00\","
        "\"HoveredColor\": \"#55FF55\","
        "\"LineWidth\": 3.0,"
        "\"UseDataDefinedColors\": false"
        "}"
        "}");

    QtNodes::NodeStyle::setNodeStyle(
        "{"
        "\"NodeStyle\": {"
        "\"NormalBoundaryColor\": \"#2a5a8f\","
        "\"SelectedBoundaryColor\": \"#00FF00\","
        "\"GradientColor0\": \"#1a3a5f\","
        "\"GradientColor1\": \"#2a5a8f\","
        "\"GradientColor2\": \"#3a7aaf\","
        "\"GradientColor3\": \"#4a9acf\","
        "\"ShadowColor\": \"#000000\","
        "\"FontColor\": \"white\","
        "\"FontFamily\": \"Arial\","
        "\"FontSize\": 12,"
        "\"Opacity\": 0.8"
        "}"
        "}");


    QtNodes::GraphicsViewStyle::setStyle(
        "{"
        "\"GraphicsViewStyle\": {"
        "\"BackgroundColor\": \"#2b2b2b\","
        "\"GridColor\": \"#404040\","
        "\"FineGridColor\": \"#303030\","
        "\"ConnectionPointColor\": \"#AAAAAA\","
        "\"HoveredConnectionPointColor\": \"#FF0000\","
        "\"FlowViewColor\": \"#2b2b2b\","
        "\"FontSize\": 10"
        "}"
        "}");
}

QString NodeEditorWindow::getSaveFilePath()
{
    return QFileDialog::getSaveFileName(
        this,
        "Save Graph",
        m_currentFilePath.isEmpty() ? QDir::currentPath() : m_currentFilePath,
        "JSON Files (*.json);;All Files (*)"
    );
}

QString NodeEditorWindow::getOpenFilePath()
{
    return QFileDialog::getOpenFileName(
        this,
        "Open Graph",
        QDir::currentPath(),
        "JSON Files (*.json);;All Files (*)"
    );
}

// ============================================================================
// File Operations
// ============================================================================

void NodeEditorWindow::onNew()
{
    onClear();
    m_currentFilePath.clear();
    statusBar()->showMessage("New graph created");
}

void NodeEditorWindow::onSave()
{
    QString filePath = getSaveFilePath();
    if (filePath.isEmpty())
        return;

    if (m_scene->save())
    {
        m_currentFilePath = filePath;
        setWindowModified(false);
        statusBar()->showMessage("Saved: " + filePath);
    }
    else
    {
        QMessageBox::warning(this, "Error", "Cannot save file: " + filePath);
    }
}

void NodeEditorWindow::onLoad()
{
    QString filePath = getOpenFilePath();
    if (filePath.isEmpty())
        return;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        QMessageBox::warning(this, "Error", "Cannot open file: " + filePath);
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc(QJsonDocument::fromJson(data));
    if (!doc.isObject())
    {
        QMessageBox::warning(this, "Error", "Invalid file format");
        return;
    }

    onClear();
    if (m_scene->load())
    {
        m_currentFilePath = filePath;
        setWindowModified(false);
        statusBar()->showMessage("Loaded: " + filePath);
    }
    else
    {
        QMessageBox::warning(this, "Error", "Cannot load file: " + filePath);
    }
}

// ============================================================================
// Edit Operations
// ============================================================================

void NodeEditorWindow::onClear()
{
    if (!m_graphModel)
        return;

    auto nodeIds = m_graphModel->allNodeIds();
    for (auto nodeId : nodeIds)
    {
        m_graphModel->deleteNode(nodeId);
    }

    m_propertyEditor->clearSelection();
    statusBar()->showMessage("Cleared all nodes");
}

void NodeEditorWindow::onDelete()
{
    if (!m_scene || !m_graphModel)
        return;

    auto selectedNodeIds = m_scene->selectedNodes();
    int nodeCount = selectedNodeIds.size();

    for (auto nodeId : selectedNodeIds)
    {
        m_graphModel->deleteNode(nodeId);
    }

    m_propertyEditor->clearSelection();

    if (nodeCount > 0)
    {
        statusBar()->showMessage(QString("Deleted %1 node(s)").arg(nodeCount));
    }
    else
    {
        statusBar()->showMessage("No nodes selected");
    }
}

// ============================================================================
// Scene Operations
// ============================================================================

void NodeEditorWindow::onSceneModified(QtNodes::BasicGraphicsScene *)
{
    if (!m_graphModel)
        return;

    int nodeCount = m_graphModel->allNodeIds().size();

    int connectionCount = 0;
    auto nodeIds = m_graphModel->allNodeIds();
    for (auto nodeId : nodeIds)
    {
        auto connections = m_graphModel->allConnectionIds(nodeId);
        connectionCount += connections.size();
    }

    statusBar()->showMessage(QString("Nodes: %1, Connections: %2").arg(nodeCount).arg(connectionCount / 2));
}

void NodeEditorWindow::onSceneLoaded()
{
    if (m_view)
        m_view->centerScene();
}

// ============================================================================
// Left Sidebar Signals
// ============================================================================

void NodeEditorWindow::onNodeDoubleClicked(const QString &modelName)
{
    if (!m_view || !m_graphModel)
        return;

    QPointF scenePos = m_view->mapToScene(m_view->viewport()->rect().center());

    QtNodes::NodeId nodeId = m_graphModel->addNode(modelName);
    if (nodeId != QtNodes::InvalidNodeId) {
        m_scene->clearSelection();
        m_graphModel->setNodeData(nodeId, QtNodes::NodeRole::Position, scenePos);
        m_scene->nodeGraphicsObject(nodeId)->setSelected(true);
        m_propertyEditor->setSelectedNode(nodeId);
        statusBar()->showMessage(QString("Added node: %1").arg(modelName));
    }
}

void NodeEditorWindow::onNodeSearchTextChanged(const QString &text)
{
    Q_UNUSED(text);
    // Search is handled by LeftSidebar internally
}

void NodeEditorWindow::onNodeItemClicked(const QString &modelName)
{
    statusBar()->showMessage(QString("Double-click to add: %1").arg(modelName));
}

void NodeEditorWindow::onWorkflowLoadRequested(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        QMessageBox::warning(this, "Error", "Cannot open file: " + filePath);
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc(QJsonDocument::fromJson(data));
    if (!doc.isObject())
    {
        QMessageBox::warning(this, "Error", "Invalid file format");
        return;
    }

    onClear();
    if (m_scene->load())
    {
        m_currentFilePath = filePath;
        setWindowModified(false);
        statusBar()->showMessage("Loaded workflow: " + QFileInfo(filePath).baseName());
    }
    else
    {
        QMessageBox::warning(this, "Error", "Cannot load workflow: " + filePath);
    }
}

// ============================================================================
// Right Panel Signals
// ============================================================================

void NodeEditorWindow::onPropertyChanged(QtNodes::NodeId nodeId, const QString &property, const QVariant &value)
{
    if (!m_graphModel)
        return;

    if (property == "caption")
    {
        m_graphModel->setNodeData(nodeId, QtNodes::NodeRole::Caption, value);
    }
    else if (property == "position")
    {
        m_graphModel->setNodeData(nodeId, QtNodes::NodeRole::Position, value);
    }
    // Other properties can be handled here as needed
}

// ============================================================================
// Toolbar Operations
// ============================================================================

void NodeEditorWindow::onWorkflowComboChanged(int index)
{
    if (index == 0)  // Blank
    {
        onNew();
    }
    else if (index == 1)  // Default
    {
        onNew();
        statusBar()->showMessage("Loaded default workflow template");
    }
    else if (index == 2)  // Open...
    {
        onLoad();
        m_workflowCombo->setCurrentIndex(0);  // Reset to Blank
    }
}

void NodeEditorWindow::onBrowseWorkflows()
{
    // Switch to Workflows tab in left sidebar
    // This would require exposing tab switching from LeftSidebar
    QMessageBox::information(this, "Browse Workflows", "Workflow browser available in left sidebar Workflows tab.");
}

void NodeEditorWindow::onRefreshNodes()
{
    if (m_nodeLibrary)
    {
        m_nodeLibrary->setRegistry(m_registry);
        statusBar()->showMessage("Node definitions refreshed");
    }
}

void NodeEditorWindow::onQueueExecute()
{
    // Placeholder for queue execution
    QMessageBox::information(this, "Queue Execution", "Queue execution will be implemented in a future update.");
}

void NodeEditorWindow::onInterruptExecution()
{
    // Placeholder for interrupt
    QMessageBox::information(this, "Interrupt", "Interrupt functionality will be implemented in a future update.");
}

void NodeEditorWindow::onClearQueue()
{
    // Placeholder for clear queue
    QMessageBox::information(this, "Clear Queue", "Clear queue functionality will be implemented in a future update.");
}

void NodeEditorWindow::onShowHistory()
{
    // Placeholder for history
    QMessageBox::information(this, "Execution History", "Execution history will be implemented in a future update.");
}

// ============================================================================
// Panel Toggle Operations
// ============================================================================

void NodeEditorWindow::onToggleNodesDock()
{
    if (m_nodesDockWidget)
    {
        // Toggle dock widget visibility using ADS
        if (m_nodesDockWidget->isVisible())
        {
            m_nodesDockWidget->closeDockWidget();
            statusBar()->showMessage("Nodes panel hidden");
        }
        else
        {
            m_nodesDockWidget->toggleView(true);
            statusBar()->showMessage("Nodes panel shown");
        }
    }
}

void NodeEditorWindow::onToggleWorkflowsDock()
{
    if (m_workflowsDockWidget)
    {
        // Toggle dock widget visibility using ADS
        if (m_workflowsDockWidget->isVisible())
        {
            m_workflowsDockWidget->closeDockWidget();
            statusBar()->showMessage("Workflows panel hidden");
        }
        else
        {
            m_workflowsDockWidget->toggleView(true);
            statusBar()->showMessage("Workflows panel shown");
        }
    }
}

void NodeEditorWindow::onTogglePropertiesDock()
{
    if (m_propertiesDockWidget)
    {
        // Toggle dock widget visibility using ADS
        if (m_propertiesDockWidget->isVisible())
        {
            m_propertiesDockWidget->closeDockWidget();
            statusBar()->showMessage("Properties panel hidden");
        }
        else
        {
            m_propertiesDockWidget->toggleView(true);
            statusBar()->showMessage("Properties panel shown");
        }
    }
}

void NodeEditorWindow::onToggleQueueDock()
{
    if (m_queueDockWidget)
    {
        // Toggle dock widget visibility using ADS
        if (m_queueDockWidget->isVisible())
        {
            m_queueDockWidget->closeDockWidget();
            statusBar()->showMessage("Queue panel hidden");
        }
        else
        {
            m_queueDockWidget->toggleView(true);
            statusBar()->showMessage("Queue panel shown");
        }
    }
}

// ============================================================================
// Group Operations
// ============================================================================

void NodeEditorWindow::onGroupSelection()
{
    if (!m_scene || !m_groupManager)
        return;

    auto selectedNodes = m_scene->selectedNodes();
    if (selectedNodes.size() < 2)
    {
        statusBar()->showMessage("Select at least 2 nodes to group (Ctrl+click to select multiple)");
        return;
    }

    // Create group with selected nodes
    QVector<QtNodes::NodeId> nodeIds;
    for (QtNodes::NodeId nodeId : selectedNodes)
    {
        nodeIds.append(nodeId);
    }

    // Convert to QVector for group manager
    m_groupManager->createGroup("New Group", nodeIds);

    statusBar()->showMessage(QString("Created group with %1 nodes").arg(nodeIds.size()));
}

// ============================================================================
// Project Context
// ============================================================================

void NodeEditorWindow::setProjectContext(QStandardItemModel* model, const QString& path, const QString& name)
{
    m_projectModel = model;
    m_projectPath = path;
    m_projectName = name;

    // Set workflow path for workflow browser
    if (m_workflowBrowser && !path.isEmpty())
    {
        QDir dir(path);
        dir.cdUp();  // Go to project directory
        QString workflowDir = dir.filePath("workflows");
        m_workflowBrowser->setWorkflowPath(workflowDir);
    }
}

QStandardItemModel* NodeEditorWindow::projectModel() const
{
    return m_projectModel;
}

QString NodeEditorWindow::projectPath() const
{
    return m_projectPath;
}

QString NodeEditorWindow::projectName() const
{
    return m_projectName;
}
