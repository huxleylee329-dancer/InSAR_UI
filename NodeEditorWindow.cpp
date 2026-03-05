#include "NodeEditorWindow.h"
#include "NodeModels.h"

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
        "Test"            // 第二级分类
    };

    // ===== 2. 子分类顺序 =====
    // 格式: 顶级分类名 -> 子分类列表（按顺序）
    order.subcategories["Data Import"] = QStringList{
        "Sentinel-1",       // Data Import 下的第一个子分类
        "TerraSAR-X",       // 第二个
        "COSMO-SkyMed",     // 第三个
        "ALOS-2"            // 第四个
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

    // Test 类叶子项顺序
    order.leafItems["Test"] = QStringList{
        "SimpleSource",
        "SimpleDisplay",
        "SimpleMath"
    };

    return order;
}

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

// Forward declarations
namespace QtNodes {
class NodeGraphicsObject;
}

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
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
#include <algorithm>
#include <memory>

NodeEditorWindow::NodeEditorWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_mainLayout(nullptr)
    , m_splitter(nullptr)
    , m_toolbar(nullptr)
    , m_actionNew(nullptr)
    , m_actionSave(nullptr)
    , m_actionLoad(nullptr)
    , m_actionClear(nullptr)
    , m_actionDelete(nullptr)
    , m_actionExit(nullptr)
    , m_nodePalette(nullptr)
    , m_paletteLayout(nullptr)
    , m_searchBox(nullptr)
    , m_nodeTree(nullptr)
    , m_closePaletteButton(nullptr)
    , m_tabContainer(nullptr)
    , m_paletteTabButton(nullptr)
    , m_paletteCollapsed(false)
    , m_graphModel(nullptr)
    , m_scene(nullptr)
    , m_view(nullptr)
    , m_projectModel(nullptr)
    , m_projectPath()
    , m_projectName()
{
    setWindowTitle("InSAR Node Editor");
    resize(1200, 800);

    setupUi();
    setupToolbar();
    setupMenu();

    // Initialize node registry
    m_registry = QtNodes::registerInSARNodeModels();

    setupSceneInternal();
}

NodeEditorWindow::~NodeEditorWindow()
{
    delete m_view;
    delete m_scene;
    delete m_graphModel;
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
    m_splitter->addWidget(m_view);

    // Setup node palette
    setupNodePalette();

    // Connect to scene modification signal to update view when nodes are created
    connect(m_scene, &QtNodes::BasicGraphicsScene::modified, this, &NodeEditorWindow::onSceneModified);

    // Set view properties to ensure proper display
    m_view->setRenderHint(QPainter::Antialiasing);
    m_view->setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    m_view->setDragMode(QGraphicsView::ScrollHandDrag);  // Allow panning with mouse drag
    m_view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
}

void NodeEditorWindow::setupUi()
{
    auto *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    m_mainLayout = new QHBoxLayout(centralWidget);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    // Create splitter
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_mainLayout->addWidget(m_splitter);
}

void NodeEditorWindow::setupToolbar()
{
    m_toolbar = addToolBar("Main Toolbar");

    m_actionNew = new QAction("New", this);
    m_actionNew->setShortcut(QKeySequence::New);
    m_actionNew->setStatusTip("Create new graph");
    connect(m_actionNew, &QAction::triggered, this, &NodeEditorWindow::onNew);

    m_actionSave = new QAction("Save", this);
    m_actionSave->setShortcut(QKeySequence::Save);
    m_actionSave->setStatusTip("Save graph to file");
    connect(m_actionSave, &QAction::triggered, this, &NodeEditorWindow::onSave);

    m_actionLoad = new QAction("Load", this);
    m_actionLoad->setShortcut(QKeySequence::Open);
    m_actionLoad->setStatusTip("Load graph from file");
    connect(m_actionLoad, &QAction::triggered, this, &NodeEditorWindow::onLoad);

    m_actionClear = new QAction("Clear", this);
    m_actionClear->setStatusTip("Clear all nodes");
    connect(m_actionClear, &QAction::triggered, this, &NodeEditorWindow::onClear);

    m_actionDelete = new QAction("Delete", this);
    m_actionDelete->setShortcut(QKeySequence::Delete);
    m_actionDelete->setStatusTip("Delete selected nodes and connections");
    connect(m_actionDelete, &QAction::triggered, this, &NodeEditorWindow::onDelete);

    m_actionExit = new QAction("Exit", this);
    m_actionExit->setShortcut(QKeySequence::Quit);
    m_actionExit->setStatusTip("Exit node editor");
    connect(m_actionExit, &QAction::triggered, this, &NodeEditorWindow::close);

    m_toolbar->addAction(m_actionNew);
    m_toolbar->addAction(m_actionSave);
    m_toolbar->addAction(m_actionLoad);
    m_toolbar->addSeparator();
    m_toolbar->addAction(m_actionClear);
    m_toolbar->addAction(m_actionDelete);
    m_toolbar->addSeparator();
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

void NodeEditorWindow::setupNodePalette()
{
    // Main palette widget
    m_nodePalette = new QWidget();
    m_paletteLayout = new QVBoxLayout(m_nodePalette);
    m_paletteLayout->setContentsMargins(0, 0, 0, 0);
    m_paletteLayout->setSpacing(0);

    // Set size for the palette
    m_nodePalette->setMinimumWidth(200);
    m_nodePalette->setMaximumWidth(300);

    // Add right border effect (raised 3D style like popup menu)
    m_nodePalette->setStyleSheet(
        "QWidget {"
        "    border-right: 2px solid #888888;"
        "    border-top: 1px solid #e0e0e0;"
        "    border-bottom: 1px solid #e0e0e0;"
        "    background-color: #f5f5f5;"
        "}"
    );

    // Title bar with close button
    auto *titleBar = new QWidget();
    auto *titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(4, 4, 4, 4);
    titleLayout->setSpacing(0);

    auto *titleLabel = new QLabel("Node Palette");
    titleLayout->addWidget(titleLabel);

    titleLayout->addStretch();

    // Collapse button - fixed size
    m_closePaletteButton = new QPushButton();
    m_closePaletteButton->setText(">");
    m_closePaletteButton->setFixedSize(16, 16);
    m_closePaletteButton->setStyleSheet("QPushButton { border: none; padding: 0px; }");
    m_closePaletteButton->setToolTip("Collapse palette");
    connect(m_closePaletteButton, &QPushButton::clicked,
            this, &NodeEditorWindow::onTogglePaletteCollapsed);
    titleLayout->addWidget(m_closePaletteButton);

    titleBar->setLayout(titleLayout);
    m_paletteLayout->addWidget(titleBar);

    // Search box
    m_searchBox = new QLineEdit();
    m_searchBox->setPlaceholderText("Search nodes...");
    m_searchBox->setClearButtonEnabled(true);
    m_paletteLayout->addWidget(m_searchBox);

    // Node tree (custom widget with drag support)
    m_nodeTree = new NodeTreeWidget();
    m_nodeTree->setHeaderHidden(true);
    m_nodeTree->setIndentation(12);
    m_nodeTree->setSortingEnabled(false);  // Keep creation order, not alphabetical
    m_paletteLayout->addWidget(m_nodeTree);

    // Initialize node tree
    populateNodeTree();

    // Connect signals
    connect(m_searchBox, &QLineEdit::textChanged,
            this, &NodeEditorWindow::onSearchTextChanged);
    connect(m_nodeTree, &QTreeWidget::itemDoubleClicked,
            this, &NodeEditorWindow::onNodeItemDoubleClicked);
    connect(m_nodeTree, &QTreeWidget::itemClicked,
            this, &NodeEditorWindow::onNodeItemClicked);

    // Collapsed tab button (small "ear" dock-style)
    // Use a container with button fixed at top-right
    m_tabContainer = new QWidget();
    m_tabContainer->setFixedWidth(8);
    auto *tabLayout = new QVBoxLayout(m_tabContainer);
    tabLayout->setContentsMargins(0, 0, 0, 0);
    tabLayout->setSpacing(0);

    m_paletteTabButton = new QPushButton();
    m_paletteTabButton->setText("<");
    m_paletteTabButton->setFixedSize(8, 60);
    m_paletteTabButton->setStyleSheet(
        "QPushButton {"
        "    border: none;"
        "    padding: 0px;"
        "    background-color: #e0e0e0;"
        "}"
        "QPushButton:hover {"
        "    background-color: #d0d0d0;"
        "}"
    );
    m_paletteTabButton->setToolTip("Expand palette");

    // Button at top, rest stretches
    tabLayout->addWidget(m_paletteTabButton);
    tabLayout->addStretch();

    m_tabContainer->hide();
    connect(m_paletteTabButton, &QPushButton::clicked,
            this, &NodeEditorWindow::onTogglePaletteCollapsed);

    // Add to splitter
    m_splitter->addWidget(m_nodePalette);
    m_splitter->addWidget(m_tabContainer);

    // Default: palette expanded
    m_paletteCollapsed = false;
}

void NodeEditorWindow::populateNodeTree()
{
    m_nodeTree->clear();

    auto models = m_registry->registeredModelsCategoryAssociation();
    PaletteOrder order = getPaletteFullOrder();

    // Group models by their paths
    // Map: path -> list of (modelName, leafName) or (modelName, modelName) for 1-level
    QMap<QString, QList<QPair<QString, QString>>> pathModels;
    QMap<QString, QStringList> allPaths;  // All paths that have models

    for (const auto &pair : models)
    {
        const QString &modelName = pair.first;
        const QString &categoryPath = pair.second;

        QStringList parts = categoryPath.split('/', Qt::SkipEmptyParts);
        if (parts.isEmpty())
            continue;

        if (parts.size() == 1)
        {
            // 1-level: path is category, leafName is modelName
            pathModels[categoryPath].append(qMakePair(modelName, modelName));
        }
        else
        {
            // Multi-level: last part is leaf, rest is subcategory path
            QString subcategory = QStringList(parts.mid(0, parts.size() - 1)).join('/');
            QString leafName = parts.last();
            pathModels[subcategory].append(qMakePair(modelName, leafName));
        }

        // Track all paths
        allPaths[categoryPath] = QStringList();
    }

    // Helper function to find or create a tree item
    auto findOrCreateItem = [this](QTreeWidgetItem *parent, const QString &text, bool isLeaf) -> QTreeWidgetItem* {
        QTreeWidgetItem *targetParent = parent ? parent : nullptr;

        int count = targetParent ? targetParent->childCount() : m_nodeTree->topLevelItemCount();
        for (int i = 0; i < count; ++i) {
            QTreeWidgetItem *item = targetParent ? targetParent->child(i) : m_nodeTree->topLevelItem(i);
            if (item->text(0) == text) {
                return item;
            }
        }

        QTreeWidgetItem *newItem = targetParent ? new QTreeWidgetItem(targetParent) : new QTreeWidgetItem(m_nodeTree);
        newItem->setText(0, text);
        newItem->setExpanded(!isLeaf);
        newItem->setFlags(isLeaf ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable) : Qt::ItemIsEnabled);
        return newItem;
    };

    // Process top-level categories in defined order
    for (const QString &topLevel : order.topLevel)
    {
        QTreeWidgetItem *topItem = findOrCreateItem(nullptr, topLevel, false);

        // Get subcategories for this top-level category
        QStringList subcategories = order.subcategories.value(topLevel);

        // If no subcategories defined, this is a 1-level category
        if (subcategories.isEmpty())
        {
            QList<QPair<QString, QString>> &modelsList = pathModels[topLevel];

            // Sort according to leafItems order, or alphabetically as fallback
            const QStringList &leafOrder = order.leafItems.value(topLevel);
            if (!leafOrder.isEmpty())
            {
                QList<QPair<QString, QString>> sortedModels;
                for (const QString &leafName : leafOrder)
                {
                    for (const auto &modelInfo : modelsList)
                    {
                        if (modelInfo.second == leafName)
                            sortedModels.append(modelInfo);
                    }
                }
                // Add any models not in order list
                for (const auto &modelInfo : modelsList)
                {
                    if (!leafOrder.contains(modelInfo.second))
                        sortedModels.append(modelInfo);
                }
                modelsList = sortedModels;
            }
            else
            {
                // Sort alphabetically
                std::sort(modelsList.begin(), modelsList.end(),
                    [](const QPair<QString, QString> &a, const QPair<QString, QString> &b) {
                        return a.second < b.second;
                    });
            }

            // Add models
            for (const auto &modelInfo : modelsList)
            {
                QTreeWidgetItem *item = new QTreeWidgetItem(topItem);
                item->setText(0, modelInfo.second);
                item->setData(0, Qt::UserRole, modelInfo.first);
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            }
            topItem->setExpanded(true);
            continue;
        }

        // Process each subcategory in defined order
        for (const QString &subcategory : subcategories)
        {
            QString subcategoryPath = topLevel + "/" + subcategory;
            QTreeWidgetItem *subItem = findOrCreateItem(topItem, subcategory, false);

            QList<QPair<QString, QString>> &modelsList = pathModels[subcategoryPath];

            // Sort according to leafItems order
            const QStringList &leafOrder = order.leafItems.value(subcategoryPath);
            if (!leafOrder.isEmpty())
            {
                QList<QPair<QString, QString>> sortedModels;
                for (const QString &leafName : leafOrder)
                {
                    for (const auto &modelInfo : modelsList)
                    {
                        if (modelInfo.second == leafName)
                            sortedModels.append(modelInfo);
                    }
                }
                // Add any models not in order list
                for (const auto &modelInfo : modelsList)
                {
                    if (!leafOrder.contains(modelInfo.second))
                        sortedModels.append(modelInfo);
                }
                modelsList = sortedModels;
            }
            else
            {
                // Fallback to alphabetical sort
                std::sort(modelsList.begin(), modelsList.end(),
                    [](const QPair<QString, QString> &a, const QPair<QString, QString> &b) {
                        return a.second < b.second;
                    });
            }

            // Add leaf items
            for (const auto &modelInfo : modelsList)
            {
                QTreeWidgetItem *item = new QTreeWidgetItem(subItem);
                item->setText(0, modelInfo.second);
                item->setData(0, Qt::UserRole, modelInfo.first);
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            }
            subItem->setExpanded(true);
        }
        topItem->setExpanded(true);
    }
}

void NodeEditorWindow::onSearchTextChanged(const QString &text)
{
    QTreeWidgetItemIterator it(m_nodeTree);
    while (*it)
    {
        QTreeWidgetItem *item = *it;
        QString itemName = item->text(0);

        bool match = text.isEmpty() ||
                   itemName.contains(text, Qt::CaseInsensitive);

        // Leaf nodes (items without children) - these are the clickable nodes
        if (item->childCount() == 0)
        {
            // Show/hide based on match
            item->setHidden(!match);
            // If matching, make sure all parent categories are visible and expanded
            if (match)
            {
                QTreeWidgetItem *parent = item->parent();
                while (parent)
                {
                    parent->setHidden(false);
                    parent->setExpanded(true);
                    parent = parent->parent();
                }
            }
        }
        // Category items (items with children)
        else
        {
            // Categories are always shown if they have any visible children
            // We'll determine this after processing all items
            if (text.isEmpty())
            {
                item->setHidden(false);
                // Expand all by default when search is empty
                item->setExpanded(true);
            }
            else
            {
                // During search, expand to show matching children
                item->setExpanded(true);
            }
        }

        ++it;
    }

    // Second pass: hide categories that have no visible children
    if (!text.isEmpty())
    {
        QTreeWidgetItemIterator it2(m_nodeTree);
        while (*it2)
        {
            QTreeWidgetItem *item = *it2;
            if (item->childCount() > 0)
            {
                bool hasVisibleChildren = false;
                for (int i = 0; i < item->childCount(); ++i)
                {
                    if (!item->child(i)->isHidden())
                    {
                        hasVisibleChildren = true;
                        break;
                    }
                }
                item->setHidden(!hasVisibleChildren);
            }
            ++it2;
        }
    }
}

void NodeEditorWindow::onTogglePaletteCollapsed()
{
    QList<int> sizes = m_splitter->sizes();

    if (m_paletteCollapsed)
    {
        // Expand palette
        m_nodePalette->show();
        m_tabContainer->hide();
        m_closePaletteButton->setText(">");
        m_closePaletteButton->setToolTip("Collapse palette");
        m_paletteCollapsed = false;
        // Set sizes: canvas keeps current, panel gets 200px, tab gets 0
        sizes[1] = 200;
        sizes[2] = 0;
        m_splitter->setSizes(sizes);
    }
    else
    {
        // Collapse palette - only show the small "ear"
        m_nodePalette->hide();
        m_tabContainer->show();
        m_paletteCollapsed = true;
        // Set sizes: canvas keeps current + panel width, panel gets 0, tab gets 8px
        sizes[0] = sizes[0] + sizes[1];  // Canvas gets panel's space
        sizes[1] = 0;
        sizes[2] = 8;
        m_splitter->setSizes(sizes);
    }
}

void NodeEditorWindow::onNodeItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    if (item->parent() == nullptr)
        return;  // Ignore category items

    QString modelName = item->data(0, Qt::UserRole).toString();

    // Get canvas center position
    QPointF scenePos = m_view->mapToScene(m_view->viewport()->rect().center());

    // Create node directly through graphModel
    QtNodes::NodeId nodeId = m_graphModel->addNode(modelName);
    if (nodeId != QtNodes::InvalidNodeId) {
        m_graphModel->setNodeData(nodeId, QtNodes::NodeRole::Position, scenePos);
        statusBar()->showMessage(QString("Added node: %1").arg(modelName));
    }
}

void NodeEditorWindow::onNodeItemClicked(QTreeWidgetItem *item, int column)
{
    if (item->parent() == nullptr)
        return;

    QString modelName = item->data(0, Qt::UserRole).toString();
    statusBar()->showMessage(QString("Double-click to add: %1").arg(modelName));
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

void NodeEditorWindow::onNew()
{
    // Clear scene
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

void NodeEditorWindow::onClear()
{
    if (!m_graphModel)
        return;

    // Delete all nodes
    auto nodeIds = m_graphModel->allNodeIds();
    for (auto nodeId : nodeIds)
    {
        m_graphModel->deleteNode(nodeId);
    }

    // All connections are automatically removed when nodes are deleted
    statusBar()->showMessage("Cleared all nodes");
}

void NodeEditorWindow::onDelete()
{
    // Delete selected nodes and connections
    if (!m_scene || !m_graphModel)
        return;

    // Get selected nodes using the scene's method
    auto selectedNodeIds = m_scene->selectedNodes();
    int nodeCount = selectedNodeIds.size();

    // Delete selected nodes
    for (auto nodeId : selectedNodeIds)
    {
        m_graphModel->deleteNode(nodeId);
    }

    // Connections are automatically removed when nodes are deleted

    if (nodeCount > 0)
    {
        statusBar()->showMessage(QString("Deleted %1 node(s)").arg(nodeCount));
    }
    else
    {
        statusBar()->showMessage("No nodes selected");
    }
}

void NodeEditorWindow::onSceneModified(QtNodes::BasicGraphicsScene *)
{
    if (!m_graphModel)
        return;

    int nodeCount = m_graphModel->allNodeIds().size();

    // Count all connections
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

void NodeEditorWindow::setProjectContext(QStandardItemModel* model, const QString& path, const QString& name)
{
    m_projectModel = model;
    m_projectPath = path;
    m_projectName = name;
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
