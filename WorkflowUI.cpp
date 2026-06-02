
#include "WorkflowUI.h"
#include "DockWidgets.h"
#include "NodeGroupManager.h"
#include "NodeModels.h"
#include "MainWindow.h"
#include "WorkspaceUI.h"
#include <QSignalBlocker>

// ADS (Qt Advanced Docking System)
#include "ads_globals.h"
#include "DockManager.h"
#include "DockWidget.h"
#include "DockAreaWidget.h"

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
#include <QtNodes/internal/ExecutableNodeDelegateModel.hpp>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QToolBar>
#include <QAction>
#include "icon_utils.h"
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QStatusBar>
#include <QList>
#include <QMap>
#include <QPair>
#include <QComboBox>
#include <QDir>
#include <algorithm>
#include <memory>
#include <QToolButton>
#include <QPointer>
#include <QLayout>

#include "InSARLogManager.h"
// ============================================================================
// Node Palette Full Order Configuration
// ============================================================================
// 在这里修改来控制所有级别的显示顺序
// ============================================================================
PaletteOrder WorkflowUI::getPaletteFullOrder()
{
    PaletteOrder order;

    // ===== 1. 顶级分类顺序 =====
    order.topLevel = QStringList{
        "SAR",            // SAR处理分类
        "Data Import",    // 第一级分类
        "Preprocessing",  // 第二级分类
        "InSAR",          // InSAR分类
        "Display",        // 图像显示/预览分类
        "Information",    // 信息/工具节点分类
        "Test"           // 测试节点分类
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
        "Sentinel-1",        // Preprocessing 下的第一个子分类
        "Region Crop"
    };

    order.subcategories["SAR"] = QStringList{
        "Import",           // SAR 下的第一个子分类
        "Enhancement",      // SAR 下的第二个子分类
        "Detection",        // SAR 下的第三个子分类
        "Evaluation"        // SAR 下的第四个子分类
    };

    // ===== 3. 叶子项顺序 =====
    // 格式: 子分类完整路径 -> 叶子项列表（按顺序）
    // LeafItem 结构: {显示名称, 实际 caption}
    order.leafItems["Data Import/Sentinel-1"] = QList<PaletteOrder::LeafItem>{
        {"Single Import", "Sentinel-1 Import"},      // Sentinel-1 单文件导入
        {"Batch Import", "Sentinel-1 Batch Import"} // Sentinel-1 批量导入
    };

    order.leafItems["Data Import/TerraSAR-X"] = QList<PaletteOrder::LeafItem>{
        {"Single Import", "TerraSAR-X Import"},       // TerraSAR-X 单文件导入
        {"Batch Import", "TerraSAR-X Batch Import"} // TerraSAR-X 批量导入
    };

    order.leafItems["Data Import/COSMO-SkyMed"] = QList<PaletteOrder::LeafItem>{
        {"Batch Import", "COSMO-SkyMed Import"}     // COSMO-SkyMed 批量导入
    };

    order.leafItems["Data Import/ALOS-2"] = QList<PaletteOrder::LeafItem>{
        {"Batch Import", "ALOS-2 Import"}           // ALOS-2 批量导入
    };

    order.leafItems["SAR/Import"] = QList<PaletteOrder::LeafItem>{
        {"Single Import", "Generic SAR Import"},
        {"Batch Import", "Generic SAR Batch Import"},
        {"Imported Re-loading", "General SAR Loading"}
    };

    // Display 类叶子项顺序
    order.leafItems["Display"] = QList<PaletteOrder::LeafItem>{
        {"Image Viewer", "Image Preview"}
    };


    // Preprocessing 类叶子项顺序
    order.leafItems["Preprocessing/Region Crop"] = QList<PaletteOrder::LeafItem>{
        {"AOI Crop", "AOI Crop"}
    };

    order.leafItems["Preprocessing/Sentinel-1"] = QList<PaletteOrder::LeafItem>{
        {"Deburst", "S1 Deburst"},     // Sentinel-1 预处理：去突刺
        {"Frame Merge", "S1 Frame Merge"},  // 帧拼接
        {"Swath Merge", "S1 Swath Merge"},   // 条带拼接
        {"TOPS Back-Geocoding", "S1 TOPS Back-Geocoding"} // 后向地理编码/配准
    };

    // SAR Enhancement 类叶子项顺序
    order.leafItems["SAR/Enhancement"] = QList<PaletteOrder::LeafItem>{
        {"Speckle Denoise", "Speckle Denoise"},     // Speckle Denoise节点
        {"Clutter Suppression", "Clutter Suppression"}  // Clutter Suppression节点
    };

    // SAR Detection 类叶子项顺序
    order.leafItems["SAR/Detection"] = QList<PaletteOrder::LeafItem>{
        {"Target Detection", "Target Detection"}    // 目标检测节点
    };

    // SAR Evaluation 类叶子项顺序
    order.leafItems["SAR/Evaluation"] = QList<PaletteOrder::LeafItem>{
        {"Evaluation-ENL", "Evaluation-ENL"},
        {"Evaluation-SCR", "Evaluation-SCR"}
    };

    // InSAR 类叶子项顺序
    order.leafItems["InSAR"] = QList<PaletteOrder::LeafItem>{
        {"Coregistration", "Coregistration"}
    };

    // Test 类叶子项顺序
    order.leafItems["Test"] = QList<PaletteOrder::LeafItem>{
        {"Source", "Source"},              // SimpleSourceNode (传统样式)
        {"Display", "Display"},            // SimpleDisplayNode (传统样式)
        {"Math (Concat)", "Math (Concat)"}, // SimpleMathNode (传统样式)
        {"Card Source", "Card Source"},    // CardSimpleSourceNode (卡片样式)
        {"Card Display", "Card Display"},  // CardSimpleDisplayNode (卡片样式)
        {"Card Math (Concat)", "Card Math (Concat)"} // CardSimpleMathNode (卡片样式)
    };

    // Information 类叶子项顺序（直接挂在顶级分类下）
    order.leafItems["Information"] = QList<PaletteOrder::LeafItem>{
        {"Note", "Note"},          // NoteNode - 文本注释节点
        {"Logger", "Logger"}       // LoggerNode - 日志记录节点
    };

    return order;
}

// ============================================================================
// Helper function to create custom toolbar button
// ============================================================================

static QToolButton* createToolbarButton(const QString &iconPath, const QString &text, const QColor &iconColor = QColor("#414752"), const QColor &textColor = QColor("#595F66"), QWidget *parent = nullptr)
{
    QToolButton *btn = new QToolButton(parent);
    btn->setMinimumSize(48, 48);
    btn->setMaximumSize(48, 48);
    
    // 加载并着色图标
    QIcon coloredIcon = createColoredIcon(iconPath, iconColor);
    
    btn->setIcon(coloredIcon);
    btn->setIconSize(QSize(24, 24));
    btn->setText(text);
    btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    
    QString textColorHex = textColor.name();
    btn->setStyleSheet(
        QString("QToolButton { "
        "  border: none; "
        "  border-radius: 4px; "
        "  background-color: transparent; "
        "  color: %1; "
        "  font-size: 9px; "
        "  font-weight: bold; "
        "  text-transform: uppercase; "
        "  letter-spacing: 0.5px; "
        "  padding: 2px; "
        "}"
        "QToolButton:hover { "
        "  background-color: #E0E0E0; "
        "}"
        "QToolButton:pressed { "
        "  background-color: #D0D0D0; "
        "}").arg(textColorHex)
    );
    return btn;
}

WorkflowUI::WorkflowUI(QWidget *parent)
    : QWidget(parent)
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
    , m_actionNew(nullptr)
    , m_actionExport(nullptr)
    , m_actionImport(nullptr)
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
    , m_actionToggleNodes(nullptr)
    , m_actionToggleWorkflows(nullptr)
    , m_actionToggleProperties(nullptr)
    , m_actionToggleQueue(nullptr)
    , m_graphModel(nullptr)
    , m_scene(nullptr)
    , m_view(nullptr)
    , m_groupManager(nullptr)
    , m_projectModel(nullptr)
    , m_projectPath()
    , m_projectName()
{
    setWindowTitle("InSAR Node Editor");

    // Ensure this widget expands to fill the central area of MainWindow
    QSizePolicy sp = sizePolicy();
    sp.setHorizontalPolicy(QSizePolicy::Expanding);
    sp.setVerticalPolicy(QSizePolicy::Expanding);
    setSizePolicy(sp);

    // Ensure background is filled, so old interface doesn't show through
    setAutoFillBackground(true);

    setupUi();

    // Initialize node registry
    m_registry = QtNodes::registerInSARNodeModels();

    // Initialize theme before creating scene
    // This ensures correct theme is applied when scene is created
    initTheme();

    setupSceneInternal();
}

WorkflowUI::~WorkflowUI()
{
    // m_graphModel has no Qt parent — must be deleted manually
    delete m_graphModel;

    // m_groupManager has Qt parent (this), auto-deleted; explicit delete is redundant but harmless
    delete m_groupManager;
}

// IApplicationInterface implementation
QWidget* WorkflowUI::centralWidget()
{
    return this;
}

QList<QToolBar*> WorkflowUI::toolBars()
{
    // Toolbar is already embedded in our own layout - no need to add to main window
    return QList<QToolBar*>();
}

void WorkflowUI::activate()
{
    show();

    // Ensure dock layout is properly initialized when first shown
    // We only enforce initial widths ONCE. After that, user can resize freely.
    if (m_dockManager && !m_initialLayoutDone) {
        const int leftWidth = 250;
        const int rightWidth = 300;

        // Fix left width
        if (m_nodesDockWidget && m_nodesDockWidget->dockAreaWidget()) {
            ads::CDockAreaWidget* dockArea = m_nodesDockWidget->dockAreaWidget();
            QList<int> splitterSizes = m_dockManager->splitterSizes(dockArea);
            if (!splitterSizes.isEmpty() && splitterSizes[0] != leftWidth) {
                splitterSizes[0] = leftWidth;
                m_dockManager->setSplitterSizes(dockArea, splitterSizes);
            }
        }

        // Fix right width
        if (m_propertiesDockWidget && m_propertiesDockWidget->dockAreaWidget()) {
            ads::CDockAreaWidget* dockArea = m_propertiesDockWidget->dockAreaWidget();
            QList<int> splitterSizes = m_dockManager->splitterSizes(dockArea);
            if (!splitterSizes.isEmpty() && splitterSizes.last() != rightWidth) {
                splitterSizes.last() = rightWidth;
                m_dockManager->setSplitterSizes(dockArea, splitterSizes);
            }
        }

        m_initialLayoutDone = true;

        // Force layout update
        m_dockManager->update();
    }
}

void WorkflowUI::deactivate()
{
    hide();
}

QString WorkflowUI::id() const
{
    return "workflow";
}

QString WorkflowUI::displayName() const
{
    return "工作流";
}

// Initialize default theme (called from constructor)
void WorkflowUI::initTheme()
{
    // Apply default (light) theme for initial load
    // This ensures proper theme is loaded before scene creation
    setTheme("light");
}

void WorkflowUI::setupSceneInternal()
{
    if (m_scene) {
        return;
    }

    // Create graph model (Executable supports manual/automatic execution modes)
    m_graphModel = new QtNodes::ExecutableDataFlowGraphModel(m_registry);

    // Create scene
    m_scene = new QtNodes::DataFlowGraphicsScene(*m_graphModel, this);

    // Create view (custom subclass to handle drag & drop)
    m_view = new PaletteGraphicsView(m_scene);

    // Connect drop event signal
    connect(m_view, &PaletteGraphicsView::nodeDropped,
            this, &WorkflowUI::onNodeDropped);


    // Setup ADS Dock Manager with dock widgets
    m_dockManager->setParent(this);

    // 1. Create canvas dock widget (central widget)
    m_canvasDockWidget = new ads::CDockWidget("Canvas", this);
    m_canvasDockWidget->setWidget(m_view);
    m_canvasDockWidget->setFeature(ads::CDockWidget::DockWidgetFloatable, false);
    // Canvas needs to expand to fill all remaining space
    QSizePolicy canvasSp = m_canvasDockWidget->sizePolicy();
    canvasSp.setHorizontalPolicy(QSizePolicy::Expanding);
    canvasSp.setVerticalPolicy(QSizePolicy::Expanding);
    m_canvasDockWidget->setSizePolicy(canvasSp);
    m_dockManager->setCentralWidget(m_canvasDockWidget);

    // 2. Create Nodes dock widget (left side) - base tab
    m_nodesDockWidget = new ads::CDockWidget("Nodes", this);
    m_nodesDockWidget->setWidget(m_nodeLibrary);
    m_nodesDockWidget->setFeature(ads::CDockWidget::DockWidgetFloatable, false);
    // Minimum width (200px), expanding height to fill available space
    m_nodesDockWidget->setMinimumWidth(200);
    m_nodesDockWidget->setMinimumHeight(0);
    m_nodesDockWidget->setMaximumHeight(QWIDGETSIZE_MAX);
    // Ensure size policy allows resizing
    QSizePolicy nodesSp = m_nodesDockWidget->sizePolicy();
    nodesSp.setHorizontalPolicy(QSizePolicy::Preferred);
    nodesSp.setVerticalPolicy(QSizePolicy::Expanding);
    m_nodesDockWidget->setSizePolicy(nodesSp);
    m_dockManager->addDockWidget(ads::LeftDockWidgetArea, m_nodesDockWidget);

    // 3. Create Workflows dock widget (left side) - add as tab to Nodes area at index 1
    m_workflowsDockWidget = new ads::CDockWidget("Workflows", this);
    m_workflowsDockWidget->setWidget(m_workflowBrowser);
    m_workflowsDockWidget->setFeature(ads::CDockWidget::DockWidgetFloatable, false);
    m_dockManager->addDockWidgetTabToArea(m_workflowsDockWidget, m_nodesDockWidget->dockAreaWidget(), 1);
    // Set Nodes as the active tab (index 0) - Workflows gets auto-activated when added
    m_nodesDockWidget->dockAreaWidget()->setCurrentIndex(0);

    // 4. Create Properties dock widget (right side) - base tab
    m_propertiesDockWidget = new ads::CDockWidget("Properties", this);
    m_propertiesDockWidget->setWidget(m_propertyEditor);
    m_propertiesDockWidget->setFeature(ads::CDockWidget::DockWidgetFloatable, false);
    // Minimum width (250px), expanding height to fill available space
    m_propertiesDockWidget->setMinimumWidth(250);
    m_propertiesDockWidget->setMinimumHeight(0);
    m_propertiesDockWidget->setMaximumHeight(QWIDGETSIZE_MAX);
    // Ensure size policy allows resizing
    QSizePolicy propSp = m_propertiesDockWidget->sizePolicy();
    propSp.setHorizontalPolicy(QSizePolicy::Preferred);
    propSp.setVerticalPolicy(QSizePolicy::Expanding);
    m_propertiesDockWidget->setSizePolicy(propSp);
    m_dockManager->addDockWidget(ads::RightDockWidgetArea, m_propertiesDockWidget);

    // 5. Create Queue dock widget (right side) - add as tab to Properties area at index 1
    m_queueDockWidget = new ads::CDockWidget("Queue", this);
    m_queueDockWidget->setWidget(m_queueManager);
    m_queueDockWidget->setFeature(ads::CDockWidget::DockWidgetFloatable, false);
    m_dockManager->addDockWidgetTabToArea(m_queueDockWidget, m_propertiesDockWidget->dockAreaWidget(), 1);
    // Set Properties as the active tab (index 0) - Queue gets auto-activated when added
    m_propertiesDockWidget->dockAreaWidget()->setCurrentIndex(0);

    // Pass registry to node library
    m_nodeLibrary->setRegistry(m_registry);
    // Pass palette order to control display order
    m_nodeLibrary->setPaletteOrder(getPaletteFullOrder());

    // 默认关闭 Workflows 和 Queue 标签页
    if (m_workflowsDockWidget) m_workflowsDockWidget->closeDockWidget();
    if (m_queueDockWidget) m_queueDockWidget->closeDockWidget();

    // After everything is added, do a delayed layout adjustment
    QTimer::singleShot(10, this, [this]() {
        // Force dock manager to update layout
        m_dockManager->update();
        m_dockManager->adjustSize();
    });

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
    connect(m_scene, &QtNodes::BasicGraphicsScene::modified, this, &WorkflowUI::onSceneModified);

    // Connect to port data changed signal from property editor
    connect(m_propertyEditor, &PropertyEditor::portDataChanged, this, &WorkflowUI::onPortDataChanged);

    // Connect to node moved signal to refresh property panel coordinates
    connect(m_scene, &QtNodes::BasicGraphicsScene::nodeMoved, this, [this](QtNodes::NodeId nodeId, QPointF const &newLocation) {
        if (m_propertyEditor->currentNodeId() == nodeId) {
            m_propertyEditor->refreshCurrentNode();
        }
    });

    // 统一监听场景选择变更信号，同步更新属性面板
    connect(m_scene, &QGraphicsScene::selectionChanged, this, [this]() {
        auto selectedNodes = m_scene->selectedNodes();
        if (selectedNodes.size() == 1) {
            m_propertyEditor->setSelectedNode(selectedNodes[0]);
        } else if (selectedNodes.empty() && _detailWindow) {
            // 当详细窗口处于打开或动画过程中时，节点隐藏会导致选择清空，此时不应清空属性面板
            return;
        } else {
            m_propertyEditor->clearSelection();
        }
    });

    // Connect to node property changed signal to refresh property panel when node execution mode/state changes
    connect(m_scene, &QtNodes::BasicGraphicsScene::nodePropertyChanged, this, [this](QtNodes::NodeId nodeId) {
        // If the node whose properties changed is currently displayed, refresh it
        if (m_propertyEditor->currentNodeId() == nodeId) {
            m_propertyEditor->refreshCurrentNode();
        }
    });

    // Connect to group selection signal
    connect(m_view, &QtNodes::GraphicsView::groupSelected, this, &WorkflowUI::onGroupSelection);

    // Set view properties
    m_view->setRenderHint(QPainter::Antialiasing);
    m_view->setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    m_view->setDragMode(QGraphicsView::ScrollHandDrag);
    m_view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);

    // Replace default geometry and painter with executable versions
    // This enables: execution mode button, start/stop button, detail button, and progress bar display
    std::unique_ptr<QtNodes::AbstractNodeGeometry> execGeometry = std::make_unique<QtNodes::ExecutableNodeGeometry>(*m_graphModel);
    m_scene->setNodeGeometry(std::move(execGeometry));

    std::unique_ptr<QtNodes::AbstractNodePainter> execPainter = std::make_unique<QtNodes::ExecutableNodePainter>();
    m_scene->setNodePainter(std::move(execPainter));

    // Set scene pointer in graph model for node context access
    m_graphModel->setScene(m_scene);

    // Install event filter to handle executable node button clicks
    // This follows the upstream nodeeditor example pattern
    m_view->viewport()->installEventFilter(this);
}

void WorkflowUI::setupUi()
{
    // Create main layout
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    setLayout(layout);

    // Create ADS Dock Manager
    m_dockManager = new ads::CDockManager(this);
    // Ensure dock manager expands to fill available space
    QSizePolicy sp = m_dockManager->sizePolicy();
    sp.setHorizontalPolicy(QSizePolicy::Expanding);
    sp.setVerticalPolicy(QSizePolicy::Expanding);
    m_dockManager->setSizePolicy(sp);

    // Create dock widget components
    m_nodeLibrary = new NodeLibraryWidget();
    m_workflowBrowser = new WorkflowBrowser();
    m_propertyEditor = new PropertyEditor();
    m_queueManager = new QueueManagerWidget();

    // Connect node library signals
    connect(m_nodeLibrary, &NodeLibraryWidget::nodeDoubleClicked,
            this, &WorkflowUI::onNodeDoubleClicked);
    connect(m_nodeLibrary, &NodeLibraryWidget::nodeSearchTextChanged,
            this, &WorkflowUI::onNodeSearchTextChanged);
    connect(m_nodeLibrary, &NodeLibraryWidget::nodeItemClicked,
            this, &WorkflowUI::onNodeItemClicked);

    // Connect workflow browser signals
    connect(m_workflowBrowser, &WorkflowBrowser::loadWorkflow,
            this, &WorkflowUI::onWorkflowLoadRequested);

    // Connect property editor signals
    connect(m_propertyEditor, &PropertyEditor::propertyChanged,
            this, &WorkflowUI::onPropertyChanged);

    // Create toolbar and add everything to layout
    setupToolbar();
    layout->addWidget(m_toolbar);
    layout->addWidget(m_dockManager);
}

void WorkflowUI::setupToolbar()
{
    m_toolbar = new QToolBar(this);
    m_toolbar->setMovable(false);
    m_toolbar->setStyleSheet(
        "QToolBar { "
        "  background-color: #F3F3F3; "
        "  border-bottom: 1px solid rgba(192, 199, 212, 0.3); "
        "  spacing: 2px; "
        "  padding: 4px 8px; "
        "}"
    );

    // 定义Material Design颜色
    const QColor COLOR_PRIMARY("#005fac");          // 蓝色
    const QColor COLOR_TERTIARY("#994700");        // 棕橙色
    const QColor COLOR_ERROR("#ba1a1a");           // 红色
    const QColor COLOR_ON_SURFACE_VARIANT("#414752"); // 灰色
    const QColor COLOR_TEXT("#595F66");            // 文本颜色

    // ======================
    // Group 1: File Operations
    // ======================
    // Import button (蓝色)
    QToolButton *btnImport = createToolbarButton(":/SatExplorer/svg/folder_open.svg", "Import", COLOR_PRIMARY, COLOR_TEXT, this);
    connect(btnImport, &QToolButton::clicked, this, &WorkflowUI::onImport);
    m_toolbar->addWidget(btnImport);

    // Export button (蓝色)
    QToolButton *btnExport = createToolbarButton(":/SatExplorer/svg/save.svg", "Export", COLOR_PRIMARY, COLOR_TEXT, this);
    connect(btnExport, &QToolButton::clicked, this, &WorkflowUI::onExport);
    m_toolbar->addWidget(btnExport);

    // Favorite button (棕橙色)
    QToolButton *btnFav = createToolbarButton(":/SatExplorer/svg/star.svg", "Fav", COLOR_TERTIARY, COLOR_TEXT, this);
    connect(btnFav, &QToolButton::clicked, this, [this]() {
        QMessageBox::information(this, "Favorite Workflows", "Favorite workflows feature coming soon.");
    });
    m_toolbar->addWidget(btnFav);

    // Vertical separator
    QWidget *sep1 = new QWidget();
    sep1->setFixedWidth(1);
    sep1->setStyleSheet("background-color: rgba(192, 199, 212, 0.3);");
    sep1->setFixedHeight(32);
    m_toolbar->addWidget(sep1);

    // ======================
    // Group 2: Execution Controls
    // ======================
    // Sync/Refresh button (灰色)
    QToolButton *btnSync = createToolbarButton(":/SatExplorer/svg/refresh-cw.svg", "Refresh", COLOR_ON_SURFACE_VARIANT, COLOR_TEXT, this);
    m_btnSync = btnSync;
    connect(btnSync, &QToolButton::clicked, this, &WorkflowUI::onRefreshNodes);
    m_toolbar->addWidget(btnSync);

    // Queue button (灰色)
    QToolButton *btnQueue = createToolbarButton(":/SatExplorer/svg/reorder.svg", "Queue", COLOR_ON_SURFACE_VARIANT, COLOR_TEXT, this);
    m_btnQueue = btnQueue;
    connect(btnQueue, &QToolButton::clicked, this, &WorkflowUI::onQueueExecute);
    m_toolbar->addWidget(btnQueue);

    // Halt/Interrupt button (红色)
    QToolButton *btnHalt = createToolbarButton(":/SatExplorer/svg/stop_circle.svg", "Halt", COLOR_ERROR, COLOR_ERROR, this);
    connect(btnHalt, &QToolButton::clicked, this, &WorkflowUI::onInterruptExecution);
    m_toolbar->addWidget(btnHalt);

    // Vertical separator
    QWidget *sep2 = new QWidget();
    sep2->setFixedWidth(1);
    sep2->setStyleSheet("background-color: rgba(192, 199, 212, 0.3);");
    sep2->setFixedHeight(32);
    m_toolbar->addWidget(sep2);

    // ======================
    // Group 3: History & Cleanup
    // ======================
    // Drop/Clear Queue button (灰色)
    QToolButton *btnDrop = createToolbarButton(":/SatExplorer/svg/playlist_remove.svg", "DeQue", COLOR_ON_SURFACE_VARIANT, COLOR_TEXT, this);
    m_btnDrop = btnDrop;
    connect(btnDrop, &QToolButton::clicked, this, &WorkflowUI::onClearQueue);
    m_toolbar->addWidget(btnDrop);

    // Logs/History button (灰色)
    QToolButton *btnLogs = createToolbarButton(":/SatExplorer/svg/history.svg", "History", COLOR_ON_SURFACE_VARIANT, COLOR_TEXT, this);
    m_btnLogs = btnLogs;
    connect(btnLogs, &QToolButton::clicked, this, &WorkflowUI::onShowHistory);
    m_toolbar->addWidget(btnLogs);

    // Del/Delete selected button (灰色)
    QToolButton *btnDel = createToolbarButton(":/SatExplorer/svg/backspace.svg", "DEL", COLOR_ON_SURFACE_VARIANT, COLOR_TEXT, this);
    m_btnDel = btnDel;
    connect(btnDel, &QToolButton::clicked, this, &WorkflowUI::onDelete);
    m_toolbar->addWidget(btnDel);

    // Purge/Clear all button (红色)
    QToolButton *btnPurge = createToolbarButton(":/SatExplorer/svg/delete_icon.svg", "PURGE", COLOR_ERROR, COLOR_ERROR, this);
    connect(btnPurge, &QToolButton::clicked, this, &WorkflowUI::onClear);
    m_toolbar->addWidget(btnPurge);
}

QString WorkflowUI::getSaveFilePath()
{
    return QFileDialog::getSaveFileName(
        this,
        "Save Graph",
        m_currentFilePath.isEmpty() ? QDir::currentPath() : m_currentFilePath,
        "JSON Files (*.json);;All Files (*)"
    );
}

QString WorkflowUI::getOpenFilePath()
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

void WorkflowUI::onNew()
{
    onClear();
    m_currentFilePath.clear();
}

void WorkflowUI::onExport()
{
    QString filePath = getSaveFilePath();
    if (filePath.isEmpty())
        return;

    if (m_scene->save(filePath))
    {
        m_currentFilePath = filePath;
        setWindowModified(false);
    }
    else
    {
        InSARLogManager::LogWarning("UI", "Cannot export file: " + filePath);
        QMessageBox::warning(this, "Error", "Cannot export file: " + filePath);
    }
}

void WorkflowUI::onImport()
{
    QString filePath = getOpenFilePath();
    if (filePath.isEmpty())
        return;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        InSARLogManager::LogWarning("UI", "Cannot open file: " + filePath);
        QMessageBox::warning(this, "Error", "Cannot open file: " + filePath);
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc(QJsonDocument::fromJson(data));
    if (!doc.isObject())
    {
        InSARLogManager::LogWarning("UI", "Invalid file format");
        QMessageBox::warning(this, "Error", "Invalid file format");
        return;
    }

    onClear();
    if (m_scene->load(filePath))
    {
        m_currentFilePath = filePath;
        setWindowModified(false);
    }
    else
    {
        InSARLogManager::LogWarning("UI", "Cannot import file: " + filePath);
        QMessageBox::warning(this, "Error", "Cannot import file: " + filePath);
    }
}

// ============================================================================
// Workflow State Save/Load (for project integration)
// ============================================================================

QJsonObject WorkflowUI::saveWorkflowToJson() const
{
    if (!m_graphModel)
        return QJsonObject();
    return m_graphModel->save();
}

void WorkflowUI::loadWorkflowFromJson(const QJsonObject& json)
{
    if (!m_graphModel || json.isEmpty())
        return;

    // 加载期间临时阻塞场景修改信号，避免不必要的"修改"状态闪烁
    const QSignalBlocker blocker(m_scene);
    onClear();
    m_graphModel->load(json);
}

QList<QAction*> WorkflowUI::getViewActions() const
{
    QList<QAction*> actions;
    if (m_nodesDockWidget) actions.append(m_nodesDockWidget->toggleViewAction());
    if (m_workflowsDockWidget) actions.append(m_workflowsDockWidget->toggleViewAction());
    if (m_propertiesDockWidget) actions.append(m_propertiesDockWidget->toggleViewAction());
    if (m_queueDockWidget) actions.append(m_queueDockWidget->toggleViewAction());
    return actions;
}

// ============================================================================
// Edit Operations
// ============================================================================

void WorkflowUI::onClear()
{
    if (!m_graphModel)
        return;

    auto nodeIds = m_graphModel->allNodeIds();
    for (auto nodeId : nodeIds)
    {
        m_graphModel->deleteNode(nodeId);
    }

    m_propertyEditor->clearSelection();
}

void WorkflowUI::refreshProjectTree()
{
    // WorkflowUI 没有项目树视图，委托给 WorkspaceUI 刷新
    foreach(::QWidget* widget, QApplication::topLevelWidgets()) {
        MainWindow* mainWin = qobject_cast<MainWindow*>(widget);
        if (mainWin && mainWin->workspaceUI()) {
            mainWin->workspaceUI()->refreshProjectTree();
            return;
        }
    }
}

void WorkflowUI::clear()
{
    onClear();
}

void WorkflowUI::onDelete()
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
}

// ============================================================================
// Scene Operations
// ============================================================================

void WorkflowUI::onSceneModified(QtNodes::BasicGraphicsScene *)
{
    if (!m_graphModel)
        return;

    // 检查PropertyEditor中显示的节点是否仍然存在
    QtNodes::NodeId currentPropNodeId = m_propertyEditor->currentNodeId();
    if (currentPropNodeId != QtNodes::InvalidNodeId)
    {
        // 检查该节点是否仍然存在于模型中
        bool nodeExists = false;
        for (auto nodeId : m_graphModel->allNodeIds())
        {
            if (nodeId == currentPropNodeId)
            {
                nodeExists = true;
                break;
            }
        }
        // 如果节点不存在（被删除），清除属性面板
        if (!nodeExists)
        {
            m_propertyEditor->clearSelection();
        }
    }

    // 发出工作流被修改信号
    emit workflowModified();
}

void WorkflowUI::onPortDataChanged(QtNodes::NodeId nodeId, QtNodes::PortType portType, int portIndex, const QString& fieldKey, const QString& newValue)
{
    if (!m_graphModel || nodeId == QtNodes::InvalidNodeId)
        return;

    auto execModel = m_graphModel->delegateModel<QtNodes::ExecutableNodeDelegateModel>(nodeId);
    if (!execModel)
        return;

    // 获取端口数据
    std::shared_ptr<QtNodes::NodeData> portData;
    if (portType == QtNodes::PortType::In) {
        portData = execModel->getInputData(portIndex);
    } else {
        portData = execModel->getOutputData(portIndex);
    }

    if (!portData)
        return;

    // 设置字段值
    if (portData->setField(fieldKey, newValue)) {
        // 刷新属性面板显示更新后的值
        m_propertyEditor->refreshCurrentNode();
    }
}

void WorkflowUI::onSceneLoaded()
{
    if (m_view)
        m_view->centerScene();
}

// ============================================================================
// Left Sidebar Signals
// ============================================================================

void WorkflowUI::onNodeDoubleClicked(const QString &modelName)
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
    }
}

void WorkflowUI::onNodeSearchTextChanged(const QString &text)
{
    Q_UNUSED(text);
    // Search is handled by LeftSidebar internally
}

void WorkflowUI::onNodeItemClicked(const QString &modelName)
{
}

void WorkflowUI::onNodeDropped(QtNodes::NodeId nodeId, const QString &modelName)
{
    m_scene->clearSelection();
    m_scene->nodeGraphicsObject(nodeId)->setSelected(true);
    m_propertyEditor->setSelectedNode(nodeId);
}

void WorkflowUI::onWorkflowLoadRequested(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
    {
        InSARLogManager::LogWarning("UI", "Cannot open file: " + filePath);
        QMessageBox::warning(this, "Error", "Cannot open file: " + filePath);
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc(QJsonDocument::fromJson(data));
    if (!doc.isObject())
    {
        InSARLogManager::LogWarning("UI", "Invalid file format");
        QMessageBox::warning(this, "Error", "Invalid file format");
        return;
    }

    onClear();
    if (m_scene->load(filePath))
    {
        m_currentFilePath = filePath;
        setWindowModified(false);
    }
    else
    {
        InSARLogManager::LogWarning("UI", "Cannot load workflow: " + filePath);
        QMessageBox::warning(this, "Error", "Cannot load workflow: " + filePath);
    }
}

// ============================================================================
// Right Panel Signals
// ============================================================================

void WorkflowUI::onPropertyChanged(QtNodes::NodeId nodeId, const QString &property, const QVariant &value)
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
    else if (property == "basic")
    {
        // Basic info (position) already handled
    }
    else
    {
        // Try to set as parameter on ExecutableNodeDelegateModel
        auto execModel = m_graphModel->delegateModel<QtNodes::ExecutableNodeDelegateModel>(nodeId);
        if (execModel)
        {
            execModel->setParameter(property, value.toString());
        }
    }
}

// ============================================================================
// Toolbar Operations
// ============================================================================



void WorkflowUI::onBrowseWorkflows()
{
    // Switch to Workflows tab in left sidebar
    QMessageBox::information(this, "Browse Workflows", "Workflow browser available in left sidebar Workflows tab.");
}

void WorkflowUI::onRefreshNodes()
{
    if (m_nodeLibrary)
    {
        m_nodeLibrary->setRegistry(m_registry);
        m_nodeLibrary->setPaletteOrder(getPaletteFullOrder());
    }
}

void WorkflowUI::onQueueExecute()
{
    // Placeholder for queue execution
    QMessageBox::information(this, "Queue Execution", "Queue execution will be implemented in a future update.");
}

void WorkflowUI::onInterruptExecution()
{
    // Placeholder for interrupt
    QMessageBox::information(this, "Interrupt", "Interrupt functionality will be implemented in a future update.");
}

void WorkflowUI::onClearQueue()
{
    // Placeholder for clear queue
    QMessageBox::information(this, "Clear Queue", "Clear queue functionality will be implemented in a future update.");
}

void WorkflowUI::onShowHistory()
{
    // Placeholder for history
    QMessageBox::information(this, "Execution History", "Execution history will be implemented in a future update.");
}

// ============================================================================
// Panel Toggle Operations
// ============================================================================

void WorkflowUI::onToggleNodesDock()
{
    if (m_nodesDockWidget)
    {
        // Toggle dock widget visibility using ADS
        if (m_nodesDockWidget->isVisible())
        {
            m_nodesDockWidget->closeDockWidget();
        }
        else
        {
            m_nodesDockWidget->toggleView(true);
        }
    }
}

void WorkflowUI::onToggleWorkflowsDock()
{
    if (m_workflowsDockWidget)
    {
        // Toggle dock widget visibility using ADS
        if (m_workflowsDockWidget->isVisible())
        {
            m_workflowsDockWidget->closeDockWidget();
        }
        else
        {
            m_workflowsDockWidget->toggleView(true);
        }
    }
}

void WorkflowUI::onTogglePropertiesDock()
{
    if (m_propertiesDockWidget)
    {
        // Toggle dock widget visibility using ADS
        if (m_propertiesDockWidget->isVisible())
        {
            m_propertiesDockWidget->closeDockWidget();
        }
        else
        {
            m_propertiesDockWidget->toggleView(true);
        }
    }
}

void WorkflowUI::onToggleQueueDock()
{
    if (m_queueDockWidget)
    {
        // Toggle dock widget visibility using ADS
        if (m_queueDockWidget->isVisible())
        {
            m_queueDockWidget->closeDockWidget();
        }
        else
        {
            m_queueDockWidget->toggleView(true);
        }
    }
}

// ============================================================================
// Group Operations
// ============================================================================

void WorkflowUI::onGroupSelection()
{
    if (!m_scene || !m_groupManager)
        return;

    auto selectedNodes = m_scene->selectedNodes();
    if (selectedNodes.size() < 2)
    {
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
}

// ============================================================================
// Project Context
// ============================================================================

void WorkflowUI::setProjectContext(QStandardItemModel* model, const QString& path, const QString& name, XMLFile* projectXml)
{
    m_projectModel = model;
    m_projectPath = path;
    m_projectName = name;
    m_projectXml = projectXml;

    // Set workflow path for workflow browser
    if (m_workflowBrowser && !path.isEmpty())
    {
        QDir dir(path);
        dir.cdUp();  // Go to project directory
        QString workflowDir = dir.filePath("workflows");
        m_workflowBrowser->setWorkflowPath(workflowDir);
    }
}

QStandardItemModel* WorkflowUI::projectModel() const
{
    return m_projectModel;
}

QString WorkflowUI::projectPath() const
{
    return m_projectPath;
}

QString WorkflowUI::projectName() const
{
    return m_projectName;
}

XMLFile* WorkflowUI::projectXml() const
{
    return m_projectXml;
}

// ============================================================================
// Theme Methods
// ============================================================================

void WorkflowUI::setTheme(const QString &theme)
{
    QString jsonContent;

    // Load and apply NodeStyle
    jsonContent = QtNodes::NodeStyle::loadThemeFile(theme);
    if (!jsonContent.isEmpty()) {
        QtNodes::NodeStyle::setNodeStyle(jsonContent);
    }

    // Load and apply ConnectionStyle
    jsonContent = QtNodes::ConnectionStyle::loadThemeFile(theme);
    if (!jsonContent.isEmpty()) {
        QtNodes::ConnectionStyle::setConnectionStyle(jsonContent);
    }

    // Load and apply GraphicsViewStyle
    jsonContent = QtNodes::GraphicsViewStyle::loadThemeFile(theme);
    if (!jsonContent.isEmpty()) {
        QtNodes::GraphicsViewStyle::setStyle(jsonContent);
    }

    // Apply toolbar theme styles
    if (m_toolbar) {
        QString toolbarStyle;
        QColor separatorColor;
        
        if (theme == "dark") {
            // Dark theme colors from ui2.md
            toolbarStyle = R"(
                QToolBar {
                    background-color: #1a1c1c;
                    border-bottom: 1px solid rgba(135, 141, 152, 0.3);
                    spacing: 2px;
                    padding: 4px 8px;
                }
            )";
            separatorColor = QColor(135, 141, 152, 77); // rgba(135,141,152,0.3)
        } else if (theme == "light") {
            // Light theme colors from ui2.md
            toolbarStyle = R"(
                QToolBar {
                    background-color: #f3f3f3;
                    border-bottom: 1px solid rgba(192, 199, 212, 0.3);
                    spacing: 2px;
                    padding: 4px 8px;
                }
            )";
            separatorColor = QColor(192, 199, 212, 77); // rgba(192,199,212,0.3)
        } else {  // fusion
            // Fusion theme colors - flat with blue accent
            toolbarStyle = R"(
                QToolBar {
                    background-color: #f0f0f0;
                    border-bottom: 1px solid rgba(74, 154, 207, 0.3);
                    spacing: 2px;
                    padding: 4px 8px;
                }
            )";
            separatorColor = QColor(74, 154, 207, 77); // rgba(74,154,207,0.3)
        }
        
        m_toolbar->setStyleSheet(toolbarStyle);
        
        // Update separator widgets (find all child widgets that are separators)
        for (QObject *obj : m_toolbar->children()) {
            QWidget *widget = qobject_cast<QWidget*>(obj);
            if (widget && widget->metaObject()->className() == QString("QWidget")) {
                widget->setStyleSheet(QString("background-color: %1;").arg(separatorColor.name(QColor::HexArgb)));
            }
        }
        
        // Update toolbar buttons (simpler approach - set complete stylesheet)
        for (QObject *obj : m_toolbar->children()) {
            QToolButton *btn = qobject_cast<QToolButton*>(obj);
            if (btn) {
                QString textColor, hoverBg, pressedBg;
                if (theme == "dark") {
                    textColor = "#c1c7cf";
                    hoverBg = "#2f3131";
                    pressedBg = "#3f4141";
                } else if (theme == "light") {
                    textColor = "#595F66";
                    hoverBg = "#E0E0E0";
                    pressedBg = "#D0D0D0";
                } else {  // fusion - blue accent
                    textColor = "#333333";
                    hoverBg = "#D0E8F5";
                    pressedBg = "#4a9acf";
                }

                btn->setStyleSheet(
                    QString("QToolButton { "
                    "  border: none; "
                    "  border-radius: 4px; "
                    "  background-color: transparent; "
                    "  color: %1; "
                    "  font-size: 9px; "
                    "  font-weight: bold; "
                    "  text-transform: uppercase; "
                    "  letter-spacing: 0.5px; "
                    "  padding: 2px; "
                    "}"
                    "QToolButton:hover { "
                    "  background-color: %2; "
                    "}"
                    "QToolButton:pressed { "
                    "  background-color: %3; "
                    "}").arg(textColor, hoverBg, pressedBg)
                );
            }
        }

        // Recolor gray toolbar icons for theme
        QColor grayIconColor;
        if (theme == "dark") {
            grayIconColor = QColor("#CCCCCC");
        } else if (theme == "light") {
            grayIconColor = QColor("#414752");
        } else {  // fusion
            grayIconColor = QColor("#005FAC");
        }
        if (m_btnSync)  m_btnSync->setIcon(createColoredIcon(":/SatExplorer/svg/refresh-cw.svg", grayIconColor));
        if (m_btnQueue) m_btnQueue->setIcon(createColoredIcon(":/SatExplorer/svg/reorder.svg", grayIconColor));
        if (m_btnDrop)  m_btnDrop->setIcon(createColoredIcon(":/SatExplorer/svg/playlist_remove.svg", grayIconColor));
        if (m_btnLogs)  m_btnLogs->setIcon(createColoredIcon(":/SatExplorer/svg/history.svg", grayIconColor));
        if (m_btnDel)   m_btnDel->setIcon(createColoredIcon(":/SatExplorer/svg/backspace.svg", grayIconColor));
    }
    if (m_dockManager) {
        QString adsStyle;
        
        if (theme == "dark") {
            // Dark theme colors from ui2.md
            adsStyle = R"(
                ads--CDockContainerWidget {
                    background: #1a1c1c;
                }
                ads--CDockAreaWidget {
                    background: #121212;
                }
                ads--CDockWidgetTab {
                    background: #1a1c1c;
                    border-color: #2f3131;
                    color: #f9f9f9;
                }
                ads--CDockWidgetTab[activeTab="true"] {
                    background: #2f3131;
                    color: #f9f9f9;
                }
                ads--CDockWidgetTab QLabel {
                    color: #f9f9f9;
                }
                ads--CDockWidgetTab[activeTab="true"] QLabel {
                    color: #f9f9f9;
                }
                ads--CDockWidget {
                    background: #1a1c1c;
                    border-color: #2f3131;
                }
                ads--CAutoHideSideBar {
                    background: #1a1c1c;
                }
                ads--CAutoHideDockContainer {
                    background: #1a1c1c;
                }
                ads--CAutoHideDockContainer ads--CDockAreaTitleBar {
                    background: #2f3131;
                }
                ads--CAutoHideDockContainer ads--CDockAreaWidget[focused="true"] ads--CDockAreaTitleBar {
                    background: #2f3131;
                }
                ads--CResizeHandle {
                    background: #1a1c1c;
                }
            )";
        } else if (theme == "light") {
            // Light theme colors from ui2.md
            adsStyle = R"(
                ads--CDockContainerWidget {
                    background: #f9f9f9;
                }
                ads--CDockAreaWidget {
                    background: #f3f3f3;
                }
                ads--CDockWidgetTab {
                    background: #f3f3f3;
                    border-color: #e2e2e2;
                    color: #1a1c1c;
                }
                ads--CDockWidgetTab[activeTab="true"] {
                    background: #ffffff;
                    color: #1a1c1c;
                }
                ads--CDockWidgetTab QLabel {
                    color: #1a1c1c;
                }
                ads--CDockWidgetTab[activeTab="true"] QLabel {
                    color: #1a1c1c;
                }
                ads--CDockWidget {
                    background: #f9f9f9;
                    border-color: #e2e2e2;
                }
                ads--CAutoHideSideBar {
                    background: #f9f9f9;
                }
                ads--CAutoHideDockContainer {
                    background: #f9f9f9;
                }
                ads--CAutoHideDockContainer ads--CDockAreaTitleBar {
                    background: #0078d7;
                }
                ads--CAutoHideDockContainer ads--CDockAreaWidget[focused="true"] ads--CDockAreaTitleBar {
                    background: #0078d7;
                }
                ads--CResizeHandle {
                    background: #f9f9f9;
                }
            )";
        } else {  // fusion - flat blue accent
            adsStyle = R"(
                ads--CDockContainerWidget {
                    background: #f0f0f0;
                }
                ads--CDockAreaWidget {
                    background: #e8e8e8;
                }
                ads--CDockWidgetTab {
                    background: #e0e0e0;
                    border-color: #cccccc;
                    color: #333333;
                }
                ads--CDockWidgetTab[activeTab="true"] {
                    background: #4a9acf;
                    color: white;
                }
                ads--CDockWidgetTab QLabel {
                    color: #333333;
                }
                ads--CDockWidgetTab[activeTab="true"] QLabel {
                    color: white;
                }
                ads--CDockWidget {
                    background: #f0f0f0;
                    border-color: #cccccc;
                }
                ads--CAutoHideSideBar {
                    background: #f0f0f0;
                }
                ads--CAutoHideDockContainer {
                    background: #f0f0f0;
                }
                ads--CAutoHideDockContainer ads--CDockAreaTitleBar {
                    background: #4a9acf;
                }
                ads--CAutoHideDockContainer ads--CDockAreaWidget[focused="true"] ads--CDockAreaTitleBar {
                    background: #4a9acf;
                }
                ads--CResizeHandle {
                    background: #f0f0f0;
                }
            )";
        }

        m_dockManager->setStyleSheet(adsStyle);
    }

    // Update background color and layout for all node embedded widgets
    if (m_scene) {
        std::unordered_set<QtNodes::NodeId> nodeIds = m_graphModel->allNodeIds();
        for (QtNodes::NodeId nodeId : nodeIds) {
            // Get embedded widget from graph model
            QVariant widgetVar = m_graphModel->nodeData(nodeId, QtNodes::NodeRole::Widget);
            QWidget* widget = qobject_cast<QWidget*>(widgetVar.value<QObject*>());
            if (!widget) continue;
            
            // Remove hardcoded inline style that breaks QSS
            widget->setStyleSheet("");
            
            // Force widget to recalculate size hint after QSS application
            ads::internal::repolishStyle(widget);
            if (widget->layout()) {
                widget->layout()->invalidate();
            }
            widget->updateGeometry();
            
            // Notify graph model that node needs layout update
            m_graphModel->nodeUpdated(nodeId);
        }
    }

    // Force scene update to refresh visuals
    if (m_scene) {
        m_scene->update();
    }
    if (m_view) {
        // Update background brush from new theme
        auto const &flowViewStyle = QtNodes::StyleCollection::flowViewStyle();
        m_view->setBackgroundBrush(flowViewStyle.BackgroundColor);
        m_view->update();
    }

    // Set theme-background property for PropertyEditor, View, and WorkflowUI to detect theme
    QColor themeBgColor;
    if (theme == "dark") {
        themeBgColor = QColor(43, 64, 75);
    } else if (theme == "light") {
        themeBgColor = QColor(241, 245, 249);
    } else {  // fusion
        themeBgColor = QColor(240, 240, 240);
    }
    
    this->setProperty("theme-background", themeBgColor);
    if (m_view) {
        m_view->setProperty("theme-background", themeBgColor);
    }

    if (m_propertyEditor) {
        m_propertyEditor->setProperty("theme-background", themeBgColor);
        // Refresh property panel to apply theme changes
        m_propertyEditor->updateThemeStyles();
        m_propertyEditor->refreshCurrentNode();
    }
}

bool WorkflowUI::eventFilter(QObject *obj, QEvent *event)
{
    // Block interaction with scene when detail view is open
    if (_detailWindow && _detailWindow->isVisible()) {
        return QWidget::eventFilter(obj, event);
    }

    if (event->type() == QEvent::MouseButtonPress && obj == m_view->viewport()) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        QPointF scenePos = m_view->mapToScene(mouseEvent->pos());
        QGraphicsItem *item = m_view->itemAt(mouseEvent->pos());

        if (auto *ngo = dynamic_cast<QtNodes::NodeGraphicsObject*>(item)) {
            QtNodes::NodeId nodeId = ngo->nodeId();
            auto *delegateModel = m_graphModel->delegateModel<QtNodes::NodeDelegateModel>(nodeId);
            auto *execModel = dynamic_cast<QtNodes::ExecutableNodeDelegateModel*>(delegateModel);

            if (execModel) {
                // Convert to node local coordinates
                QPointF nodePos = ngo->sceneTransform().inverted().map(scenePos);

                auto &geo = dynamic_cast<QtNodes::ExecutableNodeGeometry&>(m_scene->nodeGeometry());

                bool useExternal = execModel->useExternalLayout();
                
                // Check for mode button (either external layout or card layout)
                if ((useExternal && geo.hitTestModeButton(nodeId, nodePos)) || 
                    (!useExternal && geo.hitTestCardModeButton(nodeId, nodePos))) {
                    // Toggle mode
                    QtNodes::ExecutionMode currentMode = execModel->executionMode();
                    execModel->setExecutionMode(
                        currentMode == QtNodes::ExecutionMode::Automatic
                            ? QtNodes::ExecutionMode::Manual
                            : QtNodes::ExecutionMode::Automatic);
                    ngo->update();
                    return true;
                }
                // Check for start button (either external layout or card layout)
                else if ((useExternal && geo.hitTestStartButton(nodeId, nodePos)) || 
                         (!useExternal && geo.hitTestCardStartButton(nodeId, nodePos))) {
                    // Toggle start/stop
                    if (execModel->executionState() == QtNodes::ExecutionState::Running) {
                        execModel->stop();
                    } else {
                        execModel->start();
                    }
                    ngo->update();
                    return true;
                }
                // Check for detail button (either external layout or card layout)
                else if ((useExternal && geo.hitTestDetailButton(nodeId, nodePos)) || 
                         (!useExternal && geo.hitTestCardDetailButton(nodeId, nodePos))) {
                    // Open detail view
                    openDetailView(ngo, execModel);
                    return true;
                }
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

void WorkflowUI::openDetailView(QtNodes::NodeGraphicsObject* ngo, QtNodes::ExecutableNodeDelegateModel* execModel)
{
    if (!execModel || !ngo) return;
    
    // Ignore duplicate clicks if detail window is already open or opening
    if (_detailWindow || _animationController) return;
    
    execModel->collapseDetailedList();

    // Capture snapshot of node data
    QtNodes::NodeDataSnapshot snapshot = QtNodes::captureNodeData(
        execModel, m_scene, ngo->nodeId());

    // Create detail window and overlay
    _detailWindow = new QtNodes::NodeDetailWindow(this);  // Pass parent for theme detection
    _detailOverlay = new QtNodes::NodeDetailOverlay(m_view->viewport());
    _detailOverlay->setGeometry(m_view->viewport()->rect());

    // Load data into detail window
    _detailWindow->loadData(snapshot);

    // Create and setup animation controller
    _animationController = new QtNodes::NodeDetailAnimationController(this);
    connect(_animationController, &QtNodes::NodeDetailAnimationController::openAnimationCompleted,
            [this]() {
                // Animation complete, detail window now visible and interactive
            });
    connect(_animationController, &QtNodes::NodeDetailAnimationController::closeAnimationCompleted,
            this, &WorkflowUI::cleanupDetailWindow);

    QPointer<QtNodes::NodeGraphicsObject> ngoPtr(ngo);

    auto* window = _detailWindow;
    auto* overlay = _detailOverlay;
    auto* controller = _animationController;

    // Connect close button to trigger reverse animation
    connect(window, &QtNodes::NodeDetailWindow::closeRequested, this, [this, ngoPtr, window, overlay, controller]() {
        if (ngoPtr && controller) {
            controller->startCloseAnimation(ngoPtr.data(), window, overlay);
        } else {
            cleanupDetailWindow();
        }
    });

    // Connect ROI signals
    connect(_detailWindow, &QtNodes::NodeDetailWindow::roiSelectionChanged, execModel, [execModel](QRectF rect, int index) {
        execModel->processRoiSelection(rect, index);
    });
    connect(_detailWindow, &QtNodes::NodeDetailWindow::roiCleared, execModel, [execModel]() {
        execModel->clearRoiSelection();
    });
    
    auto updateSnapshot = [this, execModel, ngoPtr]() {
        if (_detailWindow && ngoPtr) {
            QtNodes::NodeDataSnapshot newSnapshot = QtNodes::captureNodeData(execModel, m_scene, ngoPtr->nodeId());
            _detailWindow->updateTableData(newSnapshot);
        }
    };

    // Connect dual ROI signals
    connect(_detailWindow, &QtNodes::NodeDetailWindow::targetRoiSelectionChanged, execModel, [execModel, updateSnapshot](QRectF rect, int index) {
        execModel->processTargetRoiSelection(rect, index);
        updateSnapshot();
    });
    connect(_detailWindow, &QtNodes::NodeDetailWindow::targetRoiCleared, execModel, [execModel, updateSnapshot]() {
        execModel->clearTargetRoiSelection();
        updateSnapshot();
    });
    connect(_detailWindow, &QtNodes::NodeDetailWindow::clutterRoiSelectionChanged, execModel, [execModel, updateSnapshot](QRectF rect, int index) {
        execModel->processClutterRoiSelection(rect, index);
        updateSnapshot();
    });
    connect(_detailWindow, &QtNodes::NodeDetailWindow::clutterRoiCleared, execModel, [execModel, updateSnapshot]() {
        execModel->clearClutterRoiSelection();
        updateSnapshot();
    });
    
    // Centralized UI refresh triggered by dataUpdated
    connect(execModel, &QtNodes::NodeDelegateModel::dataUpdated, _detailWindow, [this, execModel, ngoPtr]() {
        if (_detailWindow && ngoPtr) {
            QtNodes::NodeDataSnapshot newSnapshot = QtNodes::captureNodeData(execModel, m_scene, ngoPtr->nodeId());
            _detailWindow->updateTableData(newSnapshot);
        }
    });

    // Start the open animation sequence
    _animationController->startOpenAnimation(ngo, _detailWindow, _detailOverlay);
}

void WorkflowUI::cleanupDetailWindow()
{
    if (_detailWindow) {
        _detailWindow->deleteLater();
        _detailWindow = nullptr;
    }
    if (_detailOverlay) {
        _detailOverlay->deleteLater();
        _detailOverlay = nullptr;
    }
    if (_animationController) {
        _animationController->deleteLater();
        _animationController = nullptr;
    }
}

void WorkflowUI::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
}

