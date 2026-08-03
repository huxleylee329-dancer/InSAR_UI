#if defined(_MSC_VER)
#pragma execution_character_set("utf-8")
#endif

#include<iostream>
#include <exception>
#include <QLabel>
#include <QProgressBar>
#include <QGraphicsScene>
#include <QStatusBar>
#include <QElapsedTimer>
// Include headers
#include"Baseline.h"
#include<Deformation_Average.h>
#include "MainWindow.h"
#include "WorkspaceUI.h"
#include "WorkflowUI.h"
#include "WelcomeScreenUI.h"
#include "InterfaceManager.h"
#include "IApplicationInterface.h"
#include "icon_utils.h"
#include "include/GCPDatabase.h"
#include "include/GCPAnnotationWidget.h"
#include <QtSql/QSqlError>
#include <QToolButton>
#include <QFrame>
#include <QtConcurrent/QtConcurrent>
#include <QFuture>
#include <QFutureWatcher>
#include <QEventLoop>
#include <memory>
#include <atomic>
#include "NodeUtils.h"

// Windows DWM 标题栏主题支持
#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

// DWMWA_USE_IMMERSIVE_DARK_MODE 常量定义（Windows 10 1809+）
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#endif

// External function declarations from main.cpp
extern void applyTheme(const QString &theme);
#include"OpenProject.h"
#include"NewProject.h"
#include"Import_TSX.h"
#include"Import_GenericSAR.h"
#include"import_sentinel.h"
#include"Cut.h"
#include"Registration_ui.h"
#include"S1_TOPS_BackGeocoding.h"
#include"S1_Deburst.h"
#include"Coordinate.h"
#include"Interferometric_Formation.h"
#include"Filter_ui.h"
#include"treeview.h"
#include"ImageView.h"
#include"Unwrap_ui.h"
#include"DEMSourceDialog.h"
#include"Dem_ui.h"
#include"SLC_deramp.h"
#include"Baseline_Formation.h"
#include <QJsonDocument>
#include <QJsonObject>
#include "include/GCPAnnotationWidget.h"
#include "include/GCPDatabase.h"
#include <QtSql/QSqlError>
#include "tinyxml.h"
#include"SBAS_time_series_analysis.h"
#include"DeformationRateField_ui.h"
#include"SBAS_reference_reselection.h"
#include<Export_KML.h>
#include"Geocoding.h"
#include"S1_swath_merge.h"
#include"S1_frame_merge.h"
#include"import_CSK.h"
#include"import_ALOS2.h"
#include "import_LUTAN.h"
#include "import_HTHT.h"
#include "import_Spacety.h"
#include "import_AIRSAT.h"
#include "import_Biomass.h"
#include "import_LiDAR.h"
#include"icon_source.h"
#include"PS_Dialogs.h"
#include"SpeckleDenoise.h"
#include"ClutterSuppression.h"
#include"BatchTargetRecognition.h"
#include"TargetDetection.h"
#include "InSARLogManager.h"

// Qt related headers
#include<QtGui>
#include<qfiledialog.h>
#include<qdialog.h>
#include<qsplitter.h>
#include<qstring.h>
#include<qgraphicsitem.h>
#include<qmessagebox.h>
#include<qdialogbuttonbox.h>
#include<qsettings.h>
#include<qdir.h>
// Include headers
// opencv related headers
#include<opencv2/highgui.hpp>

using namespace cv;

// ============================================================================
// Menu icon mapping: action name → SVG resource path
// ============================================================================
struct MenuIconMapping {
    const char* actionName;
    const char* svgPath;
};
static const MenuIconMapping menuIconMap[] = {
    {"actionNew",                      ":/SatExplorer/svg/new_project.svg"},
    {"actionOpen",                     ":/SatExplorer/svg/open_project.svg"},
    {"actionSave",                     ":/SatExplorer/svg/save.svg"},
    {"actionSave_as",                  ":/SatExplorer/svg/saveas.svg"},
    {"actionClose",                    ":/SatExplorer/svg/close.svg"},
    {"actionQuit",                     ":/SatExplorer/svg/quit.svg"},
    {"actionRegistration",             ":/SatExplorer/svg/coregistration.svg"},
    {"actionOrbitRefinement",          ":/SatExplorer/svg/orbit_refine.svg"},
    {"actionCut",                      ":/SatExplorer/svg/cut.svg"},
    {"actionS1_TOPS_BackGeocoding",    ":/SatExplorer/svg/coregistration.svg"},
    {"actionS1_Deburst",               ":/SatExplorer/svg/splice.svg"},
    {"actionSLC_deramp",               ":/SatExplorer/svg/splice.svg"},
    {"actionBaseline_Formation",       ":/SatExplorer/svg/baseline_formation.svg"},
    {"actionSBAS_deformation",         ":/SatExplorer/svg/time_series.svg"},
    {"actionDeformationRateField",     ":/SatExplorer/svg/rate_field.svg"},
    {"actionDeformation_Preview",      ":/SatExplorer/svg/view.svg"},
    {"actionreference_re_selection",   ":/SatExplorer/svg/reference.svg"},
    {"actionExport_KML",               ":/SatExplorer/svg/GoogleEarth.svg"},
    {"actionPSI_Candidate",            ":/SatExplorer/svg/psi_candidate.svg"},
    {"actionPSI_Network",              ":/SatExplorer/svg/psi_network.svg"},
    {"actionPSI_TimeSeries",           ":/SatExplorer/svg/psi_time_series.svg"},
    {"actiongeocode",                  ":/SatExplorer/svg/geocoding.svg"},
    {"actionS1_frame_merge",           ":/SatExplorer/svg/frame_merge.svg"},
    {"actionS1_swath_merge",           ":/SatExplorer/svg/swath_merge.svg"},
    {"actionNodeEditor",               ":/SatExplorer/svg/flow_editor.svg"},
    {"actionCleanOrphanedFiles",       ":/SatExplorer/svg/delete_icon.svg"},
    {"actionDEM",                      ":/SatExplorer/svg/dem.svg"},
    {"actionExternal_DEM",             ":/SatExplorer/svg/external_dem.svg"},
    {"actionGenericSAR",               ":/SatExplorer/svg/imagedata.svg"},
    {"actionTSX",                      ":/SatExplorer/svg/x_band.svg"},
    {"actionSentinel_1",               ":/SatExplorer/svg/sentinel1_logo.svg"},
    {"actionCOSMOS_SkyMed",            ":/SatExplorer/svg/x_band.svg"},
    {"actionALOS_2",                   ":/SatExplorer/svg/l_band.svg"},
    {"actionLuTan_1",                  ":/SatExplorer/svg/l_band.svg"},
    {"actionHongtu_1",                 ":/SatExplorer/svg/c_band.svg"},
    {"actionSpacety",                  ":/SatExplorer/svg/x_band.svg"},
    {"actionAIRSAT",                   ":/SatExplorer/svg/airborne_sar.svg"},
    {"actionBiomass",                  ":/SatExplorer/svg/l_band.svg"},
    {"actionLiDAR",                    ":/SatExplorer/svg/lidar_sensor.svg"},
    {"actionSpeckleDenoise",           ":/SatExplorer/svg/filter.svg"},
    {"actionDenoise",                  ":/SatExplorer/svg/filter.svg"},
    {"actionClutterSuppression",       ":/SatExplorer/svg/clutter_suppress.svg"},
    {"actionTargetDetection",          ":/SatExplorer/svg/target_detect.svg"},
    {"actionBatchTargetRecognition",   ":/SatExplorer/svg/batch_detect.svg"},
    {"actionEvaluation",               ":/SatExplorer/svg/chart.svg"},
    {"actionInterferometric_Formation",":/SatExplorer/svg/interferogram.svg"},
    {"actionUnwrap",                   ":/SatExplorer/svg/unwrap.svg"},
    {"actionBaseline_Preview",         ":/SatExplorer/svg/view.svg"},
    {"menuInSAR_Import",               ":/SatExplorer/svg/insar_group.svg"},
    {"menuLiDAR_Import",               ":/SatExplorer/svg/lidar_group.svg"},
    {"menuSBAS",                       ":/SatExplorer/svg/time_series.svg"},
    {"menuPSI",                        ":/SatExplorer/svg/psi_network.svg"},
    {"menuSentinel1_Tool",             ":/SatExplorer/svg/toolbox.svg"},
    {"menuImageEnhancement",           ":/SatExplorer/svg/enhancement.svg"},
    {"menuDetection",                  ":/SatExplorer/svg/radar.svg"},
    {"menuTheme",                      ":/SatExplorer/svg/palette.svg"},
    {"actionGcpManager",               ":/SatExplorer/svg/GCPs.svg"},
    {"actionPhaseElevationRegression",  ":/SatExplorer/svg/phase_elevation.svg"},
    {"actionGacosOnlineService",         ":/SatExplorer/svg/gacos.svg"},
    {"actionTroposphericCorrection",     ":/SatExplorer/svg/troposphere.svg"},
    {"actionIonosphericCorrection",      ":/SatExplorer/svg/ionosphere.svg"},
};

MainWindow::MainWindow(QWidget* parent)
    : MainWindow(QString(), parent)
{
}

MainWindow::MainWindow(QString str, QWidget* parent)
    : QMainWindow(parent)
    , Process(nullptr)
    , project(nullptr)
    , m_interfaceManager(nullptr)
    , m_workspaceUI(nullptr)
    , m_workflowUI(nullptr)
    , m_welcomeUI(nullptr)
    , m_statusProjectLabel(nullptr)
    , m_statusInterfaceLabel(nullptr)
    , m_statusProgressBar(nullptr)
    , m_activeStatusTaskId(QtNodes::InvalidNodeId)
    , m_actionGcpManager(nullptr)
    , m_actionOrbitManager(nullptr)
    , m_actionPhaseElevationRegression(nullptr)
    , m_actionGacosOnlineService(nullptr)
    , m_actionTroposphericCorrection(nullptr)
    , m_actionIonosphericCorrection(nullptr)
    , m_menuTools(nullptr)
    , m_menuAtmosphericCorrection(nullptr)
{
    ui.setupUi(this);
    initStatusBar();
    this->project = new XMLFile;
    this->double_click_open_project_file = "";
    this->b_open_throug_dbclk = false;

    // Set APP icon
    this->setWindowTitle("SatExplorer");
    this->setWindowIcon(QIcon(APP_ICON));

    // Initialize interfaces for switching
    initializeInterfaces(nullptr, project, str);

    // Use WorkspaceUI components - get the authoritative model from WorkspaceUI
    m_workspaceUI->treeView()->init_tree();
    QStandardItemModel* initialModel = m_workspaceUI->treeView()->model;

    // Determine initial project name
    QString projectName;
    if (!str.isEmpty()) {
        projectName = QFileInfo(str).fileName();
    }

    // Explicitly sync the valid model and context to all interfaces
    if (m_interfaceManager) {
        m_interfaceManager->setProjectContext(initialModel, str, projectName, this->project);
    }

    m_workspaceUI->tabWidget()->setTabsClosable(true);

    // Load initial theme from Config.ini
    QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
    m_currentTheme = settings.value("Appearance/Theme", "light").toString();

    // 创建最近打开子菜单并插入文件菜单（插入在第一个分隔线之前，使其与“新建/打开”归为一组）
    m_recentMenu = new QMenu("最近打开", this);
    QAction* firstSep = nullptr;
    for (QAction* action : ui.File->actions()) {
        if (action->isSeparator()) {
            firstSep = action;
            break;
        }
    }
    if (firstSep) {
        ui.File->insertMenu(firstSep, m_recentMenu);
    } else {
        ui.File->insertMenu(ui.actionSave, m_recentMenu);
    }

    // Setup theme menu (after setting m_currentTheme)
    setupThemeMenu();

    // Apply menu icons after m_recentMenu is created and setupThemeMenu is done
    applyMenuIcons(m_currentTheme == "dark");
    updateRecentMenu();
    
    // Connect signals/slots
    connect(m_workspaceUI->treeView(), SIGNAL(sendindex(QModelIndex)), this, SLOT(ShowImage(QModelIndex)));
    connect(m_workspaceUI->treeView(), &TreeView::update, m_workspaceUI, &WorkspaceUI::refreshProjectTree);
    connect(m_workspaceUI->tabWidget(), &QTabWidget::currentChanged, this, &MainWindow::ShowColorBar);
    connect(m_workspaceUI->tabWidget(), &QTabWidget::tabCloseRequested, this, &MainWindow::handleTabCloseRequested);
    connect(m_workspaceUI, &WorkspaceUI::projectTreeRefreshed, this, [this]() {
        if (!m_projectPath.isEmpty() && this->project) {
            this->project->XMLFile_load(m_projectPath.toStdString().c_str());
        }

        // Enable menus if project has data (Replicates legacy RenewTree logic)
        QStandardItemModel* currentModel = m_interfaceManager->projectModel();
        if (currentModel && currentModel->rowCount() > 0)
        {
            if (!ui.menuImport_Top->isEnabled()) ui.menuImport_Top->setDisabled(0);
            if (!ui.menuPreprocessing->isEnabled()) ui.menuPreprocessing->setDisabled(0);
            if (!ui.menuInSAR->isEnabled()) ui.menuInSAR->setDisabled(0);
            if (!ui.menuDInSAR->isEnabled()) ui.menuDInSAR->setDisabled(0);
            if (!ui.menuSAR->isEnabled()) ui.menuSAR->setDisabled(0);
            if (!ui.menuExport->isEnabled()) ui.menuExport->setDisabled(0);
            if (m_actionGcpManager && !m_actionGcpManager->isEnabled()) m_actionGcpManager->setDisabled(0);
            if (m_actionOrbitManager && !m_actionOrbitManager->isEnabled()) m_actionOrbitManager->setDisabled(0);
            if (m_menuTools) m_menuTools->menuAction()->setEnabled(true);
            if (m_menuAtmosphericCorrection) m_menuAtmosphericCorrection->menuAction()->setEnabled(true);
            if (m_actionPhaseElevationRegression) m_actionPhaseElevationRegression->setEnabled(true);
            if (m_actionGacosOnlineService) m_actionGacosOnlineService->setEnabled(true);
            if (m_actionTroposphericCorrection) m_actionTroposphericCorrection->setEnabled(true);
            if (m_actionIonosphericCorrection) m_actionIonosphericCorrection->setEnabled(true);
        }

        m_projectModified = true;
        updateWindowTitle();
    });
    connect(ui.actionQuit, &QAction::triggered, this, &MainWindow::close);

    // Add interface switching menu to View
    setupInterfaceSwitchingMenu();

    // 动态创建并插入“工具”菜单 (SOP 建议一)
    m_menuTools = new QMenu(QStringLiteral("工具"), this);
    m_actionGcpManager = new QAction(QStringLiteral("地面控制点管理与评估"), this);
    m_actionGcpManager->setObjectName("actionGcpManager");
    m_actionGcpManager->setIcon(QIcon(GCP_ICON));
    m_menuTools->addAction(m_actionGcpManager);

    m_actionOrbitManager = new QAction(QStringLiteral("Sentinel-1 精密轨道管理器"), this);
    m_actionOrbitManager->setObjectName("actionOrbitManager");
    m_actionOrbitManager->setIcon(QIcon(":/SatExplorer/svg/toolbox.svg"));
    m_menuTools->addAction(m_actionOrbitManager);

    // 挂接槽信号
    connect(m_actionGcpManager, &QAction::triggered, this, &MainWindow::slot_actionGCP_Manager_triggered);
    connect(m_actionOrbitManager, &QAction::triggered, this, &MainWindow::slot_actionOrbit_Manager_triggered);

    // 插入到“数据导出”的后面
    QList<QAction*> actions = menuBar()->actions();
    QAction* exportAction = ui.menuExport->menuAction();
    int idx = actions.indexOf(exportAction);
    if (idx != -1 && idx + 1 < actions.size()) {
        menuBar()->insertMenu(actions[idx + 1], m_menuTools);
    } else {
        menuBar()->addMenu(m_menuTools);
    }

    // 动态创建并插入“大气校正”菜单 (V2.2 UI 集成)
    m_menuAtmosphericCorrection = new QMenu(QStringLiteral("大气校正"), this);
    m_menuAtmosphericCorrection->setObjectName("menuAtmosphericCorrection");

    m_actionPhaseElevationRegression = new QAction(QStringLiteral("经验性相位-高程回归"), this);
    m_actionPhaseElevationRegression->setObjectName("actionPhaseElevationRegression");
    m_menuAtmosphericCorrection->addAction(m_actionPhaseElevationRegression);
    connect(m_actionPhaseElevationRegression, &QAction::triggered, this, &MainWindow::slot_actionPhaseElevationRegression_triggered);

    m_actionGacosOnlineService = new QAction(QStringLiteral("GACOS 在线服务"), this);
    m_actionGacosOnlineService->setObjectName("actionGacosOnlineService");
    m_menuAtmosphericCorrection->addAction(m_actionGacosOnlineService);
    connect(m_actionGacosOnlineService, &QAction::triggered, this, &MainWindow::slot_actionGacosOnlineService_triggered);

    m_actionTroposphericCorrection = new QAction(QStringLiteral("ERA5 对流层校正"), this);
    m_actionTroposphericCorrection->setObjectName("actionTroposphericCorrection");
    m_menuAtmosphericCorrection->addAction(m_actionTroposphericCorrection);
    connect(m_actionTroposphericCorrection, &QAction::triggered, this, &MainWindow::slot_actionTroposphericCorrection_triggered);

    m_actionIonosphericCorrection = new QAction(QStringLiteral("电离层 Split-Spectrum 校正"), this);
    m_actionIonosphericCorrection->setObjectName("actionIonosphericCorrection");
    m_menuAtmosphericCorrection->addAction(m_actionIonosphericCorrection);
    connect(m_actionIonosphericCorrection, &QAction::triggered, this, &MainWindow::slot_actionIonosphericCorrection_triggered);

    // 在无工程打开时，默认禁用
    m_actionPhaseElevationRegression->setEnabled(false);
    m_actionGacosOnlineService->setEnabled(false);
    m_actionTroposphericCorrection->setEnabled(false);
    m_actionIonosphericCorrection->setEnabled(false);
    m_actionOrbitManager->setEnabled(false);

    // 插入到 DInSAR 后面（数据导出前面），符合地理数据处理先后逻辑流程
    QList<QAction*> newActions = menuBar()->actions();
    int exportIdx = newActions.indexOf(exportAction);
    if (exportIdx != -1) {
        menuBar()->insertMenu(newActions[exportIdx], m_menuAtmosphericCorrection);
    } else {
        menuBar()->addMenu(m_menuAtmosphericCorrection);
    }

    if (str.isEmpty()) {
        // No project opened - show welcome screen
        ui.View->setDisabled(1);
        ui.menuImport_Top->setDisabled(1);
        ui.menuPreprocessing->setDisabled(1);
        ui.menuInSAR->setDisabled(1);
        ui.menuDInSAR->setDisabled(1);
        ui.menuSAR->setDisabled(1);
        ui.menuExport->setDisabled(1);
        if (m_actionGcpManager) m_actionGcpManager->setDisabled(1);
        if (m_actionPhaseElevationRegression) m_actionPhaseElevationRegression->setEnabled(false);
        if (m_actionGacosOnlineService) m_actionGacosOnlineService->setEnabled(false);
        if (m_actionTroposphericCorrection) m_actionTroposphericCorrection->setEnabled(false);
        if (m_actionIonosphericCorrection) m_actionIonosphericCorrection->setEnabled(false);
        
        m_menuTools->menuAction()->setEnabled(false);
        m_menuAtmosphericCorrection->menuAction()->setEnabled(false);
        
        m_interfaceManager->switchToInterface("welcome");
        updateInterfaceMenuCheckState();
    }
    else {
        // Open project file (loadWorkflowFromProject inside handles interface switching)
        this->open_from_project_file(str);
    }
    
    updateFileMenuState();
}
MainWindow::~MainWindow()

{
    // Cleanup interface manager first - it owns interface widgets
    if (m_interfaceManager)
    {
        delete m_interfaceManager;
        m_interfaceManager = nullptr;
    }
    m_workspaceUI = nullptr;  // Already deleted by InterfaceManager
    m_workflowUI = nullptr;  // Already deleted by InterfaceManager
    m_welcomeUI = nullptr;   // Already deleted by InterfaceManager

    if (this->Process)
    {
        delete(Process);
        Process = NULL;
    }
    if (this->project)
    {
        delete this->project;
        this->project = NULL;
    }
}
void MainWindow::Addproject(QString name, QString save_path)
{
    m_workspaceUI->treeView()->NewProject(name, save_path);
    m_workspaceUI->updateProjectModel(m_workspaceUI->treeView()->model);

    // 设置工程路径并加载 XML，使后续保存能正常工作
    QString projectFile = save_path + "/" + name + ".insar";
    updateProjectContext(projectFile);
    this->project->XMLFile_load(projectFile.toStdString().c_str());

    // 添加到最近打开列表
    addToRecentProjects(projectFile);

    // 新建工程后，重置修改标记（因为刚保存过）
    m_projectModified = false;
    updateWindowTitle();

    statusBar()->showMessage(QStringLiteral("已成功创建新工程: %1").arg(name + ".insar"), 3000);
}
void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);

    // After InterfaceManager refactoring:
    // - If current interface is Workspace, delegate resize to WorkspaceUI
    // - ui.tabWidget was deleted with old central widget - do not access it
    if (m_interfaceManager && m_interfaceManager->currentInterfaceId() == "workflow")
    {
        // Node-based workflow does not need ColorBar handling - nothing to do
        return;
    }

    // When in workspace interface - let WorkspaceUI handle it
    if (m_workspaceUI)
    {
        // WorkspaceUI handles its own resizing - native event handling will happen
        // The old ui.tabWidget no longer exists
        return;
    }
}
void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!m_projectPath.isEmpty() && m_projectModified) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, tr("退出确认"),
            tr("当前工程已修改，是否保存？"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

        if (reply == QMessageBox::Save) {
            if (this->project) {
                this->project->XMLFile_load(m_projectPath.toStdString().c_str());
            }
            saveWorkflowToProject(m_projectPath);
            if (m_interfaceManager && this->project) {
                m_interfaceManager->saveLastInterfaceToProject(this->project);
            }
            this->project->XMLFile_save(m_projectPath.toStdString().c_str());
            closeCurrentProject();
            event->accept();
        } else if (reply == QMessageBox::Discard) {
            closeCurrentProject();
            event->accept();
        } else {
            event->ignore();
        }
    } else {
        closeCurrentProject();
        event->accept();
    }
}
void MainWindow::Loading(QString Data_path, QString ImageType, QString bmp_path, QString bmp_name)
{
    // 界面切换到 Workflow 后直接返回
    if (m_interfaceManager && m_interfaceManager->currentInterfaceId() != "workspace")
        return;

    if (bmp_path.isEmpty()) bmp_path = this->bmp_path;
    if (bmp_name.isEmpty()) bmp_name = this->bmp_name;

    QWidget* TabChild = new QWidget;
    TabChild->setProperty("filePath", Data_path); // 记录数据物理路径以供系统工具栏控制 GCP 显隐

    QGridLayout* TabLayout = new QGridLayout(TabChild);
    TabLayout->setContentsMargins(0, 0, 0, 0);
    TabLayout->setSpacing(0);

    ImageView* graph = new ImageView(TabChild);
    TabLayout->addWidget(graph, 0, 0);

    QGraphicsScene* scene = new QGraphicsScene;
    graph->setScene(scene);
    graph->setInteractive(true);
    graph->setDragMode(QGraphicsView::RubberBandDrag);
    graph->setRubberBandSelectionMode(Qt::ContainsItemShape);
    QImage Qimg = QImage(bmp_path);
    QGraphicsPixmapItem* item = new QGraphicsPixmapItem(QPixmap::fromImage(Qimg));
    item->setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
    item->setAcceptedMouseButtons(Qt::LeftButton);
    scene->addItem(item);

    TabChild->setLayout(TabLayout);
    TabChild->setAttribute(Qt::WA_DeleteOnClose);

    /// progressdialog.setValue(100);
    // progressdialog.autoClose();
    graph->show();
    ColorBar* Color_Label = new ColorBar(TabChild);

    int ret = Color_Label->SetData(Data_path, ImageType);
    if(ret == 0)
    {
        Color_Label->move(0, 0);
        Color_Label->show();
        mExist_Color.append(true);
    }
    else
    {
        Color_Label->hide();
        mExist_Color.append(false);
    }
    mColors.append(Color_Label);

    // 在所有子控件及布局构建并挂载完毕后，最后执行 addTab 与 setCurrentWidget。
    // 这能确保 currentChanged 信号被触发时，活动页面中已经可以立即检索到 ImageView 子控件，消除了打点事件过滤的初始化时序竞态条件。
    QTabWidget* activeTabWidget = m_workspaceUI->tabWidget();
    int index = activeTabWidget->addTab(TabChild, bmp_name);
    activeTabWidget->setCurrentWidget(TabChild);
}
void MainWindow::open_from_project_file(QString str)
{
    if (str.isEmpty())
        return;

    QFileInfo newFileInfo(str);
    QFileInfo currentFileInfo(m_projectPath);
    bool isSameProject = (!m_projectPath.isEmpty() && newFileInfo.absoluteFilePath() == currentFileInfo.absoluteFilePath());

    if (isSameProject) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this,
            QStringLiteral("重新加载提示"),
            QStringLiteral("该项目已在当前窗口中打开，是否重新加载？\n(注意：未保存的修改将会丢失)"),
            QMessageBox::Yes | QMessageBox::No
        );
        if (reply == QMessageBox::No) {
            return;
        }
    } else {
        if (!maybeSave())
            return;
    }

    // 关闭当前工程（不保存），避免两个工程状态共存
    closeCurrentProject();

    QString filename = str;
    QFileInfo fileinfo = QFileInfo(filename);
    QString abs_path = fileinfo.absolutePath();
    QStandardItemModel* currentModel = m_workspaceUI->treeView()->model;

    int ret = this->project->XMLFile_load(filename.toStdString().c_str());
    if (ret < 0)
    {
        return;
    }
    TiXmlElement* root;
    ret = this->project->get_root(root);
    TiXmlElement* p, * q, * j;
    QList<QString> origin_name;
    if (!root->NoChildren())
    {
        p = root->FirstChildElement();
        if (!strcmp(p->Value(), "project_info"))
        {
            q = p->FirstChildElement();
            QString project_name = q->Value();
            QStandardItem* Project = new QStandardItem;
            QStandardItem* Project_Path = new QStandardItem;
            Project->setIcon(QIcon(PROJECT_ICON));
            Project->setData(PROJECT_ICON, Qt::UserRole + 10);
            Project->setStatusTip(NOT_IN_PROCESS);
            if (!strcmp(q->Value(), "project_name"))
            {
                // 如果实际文件名与XML中记录的名称不一致（手动改名或另存为Bug导致），自动修正XML内存结构并更新显示名称
                QString actualFileName = fileinfo.fileName();
                QString oldFileName = QString::fromUtf8(q->GetText());
                if (actualFileName != oldFileName)
                {
                    q->Clear();
                    q->LinkEndChild(new TiXmlText(actualFileName.toStdString().c_str()));

                    // 自动修正并重命名 GCP 数据库文件（自愈机制）
                    QString projDir = fileinfo.absolutePath();
                    QString oldBase = QFileInfo(oldFileName).baseName();
                    QString newBase = fileinfo.baseName();
                    QString oldDbPath = projDir + "/" + oldBase + "_gcp.db";
                    QString newDbPath = projDir + "/" + newBase + "_gcp.db";
                    if (QFile::exists(oldDbPath) && !QFile::exists(newDbPath))
                    {
                        QFile::rename(oldDbPath, newDbPath);
                    }
                }
                Project->setText(actualFileName);
            }

            q = q->NextSiblingElement();
            if (!strcmp(q->Value(), "project_path"))
                if (!strcmp(abs_path.toStdString().c_str(), q->GetText()))
                    Project_Path->setText(q->GetText());
                else
                {
                    Project_Path->setText(abs_path.toStdString().c_str());
                    q->Clear(); q->LinkEndChild(new TiXmlText(abs_path.toStdString().c_str()));//Update save path
                }
            currentModel->appendRow(Project);
            currentModel->setItem(currentModel->rowCount() - 1, 1, Project_Path);
            for (p = p->NextSiblingElement(); p != NULL; p = p->NextSiblingElement())
            {
                // 跳过非 DataNode 元素（如 lastInterface、workflow）
                if (!p->Attribute("name"))
                    continue;

                QStandardItem* Data_Node = new QStandardItem;
                Data_Node->setText(p->Attribute("name"));
                Data_Node->setToolTip(Project->text());
                Data_Node->setIcon(QIcon(FOLDER_ICON));
                Data_Node->setData(FOLDER_ICON, Qt::UserRole + 10);
                Project->appendRow(Data_Node);
                QStandardItem* Rank = new QStandardItem(p->Attribute("rank"));
                Project->setChild(Project->rowCount() - 1, 1, Rank);
                int i = 0;
                int count = 0;
                for (q = p->FirstChildElement(); q != NULL && strcmp(q->Value(), "Data") == 0; q = q->NextSiblingElement(), i++)
                {
                    QStandardItem* Data = new QStandardItem;
                    QStandardItem* Data_Path = new QStandardItem;

                    for (j = q->FirstChildElement(); j != NULL; j = j->NextSiblingElement())
                    {

                        if (!strcmp(j->Value(), "Data_Name"))
                        {
                            Data->setText(j->GetText());
                        }
                        else if (!strcmp(j->Value(), "Data_Rank"))
                        {
                            if (strcmp(j->GetText(), "complex-0.0") == 0 ||
                                strcmp(j->GetText(), "complex-1.0") == 0 ||
                                strcmp(j->GetText(), "complex-2.0") == 0 ||
                                strcmp(j->GetText(), "complex-3.0") == 0
                                )
                                Data->setToolTip("complex");
                            else if (strcmp(j->GetText(), "phase-1.0") == 0 ||
                                strcmp(j->GetText(), "phase-2.0") == 0 ||
                                strcmp(j->GetText(), "phase-3.0") == 0 ||
                                strcmp(j->GetText(), "phase-1.1") == 0 ||
                                strcmp(j->GetText(), "phase-2.1") == 0 ||
                                strcmp(j->GetText(), "phase-3.1") == 0
                                )
                                Data->setToolTip("phase");
                            else if (strcmp(j->GetText(), "coherence-1.0") == 0 || strcmp(j->GetText(), "coherence-1.1") == 0)
                                Data->setToolTip("coherence");
                            else if (strcmp(j->GetText(), "dem-1.0") == 0 || strcmp(j->GetText(), "dem-1.1") == 0)
                                Data->setToolTip("dem");
                            else if (strcmp(j->GetText(), "SBAS-1.0") == 0 || strcmp(j->GetText(), "SBAS-1.1") == 0)
                                Data->setToolTip("SBAS");
                            else if(strcmp(j->GetText(), "amplitude-1.0") == 0 || strcmp(j->GetText(), "amplitude-1.1") == 0)
                                Data->setToolTip("amplitude");
                        }
                        else if (!strcmp(j->Value(), "Data_Path"))
                        {
                            Data_Path->setText(abs_path + QString(j->GetText()));
                        }

                    }
                    Data_Node->appendRow(Data);
                    Data->setIcon(QIcon(IMAGEDATA_ICON));
                    Data->setData(IMAGEDATA_ICON, Qt::UserRole + 10);
                    Data_Node->setChild(i, 1, Data_Path);
                }

            }
            //model->setHeaderData(0, Qt::Horizontal, tr("workspace"));
            m_workspaceUI->updateProjectModel(currentModel);

            // 加载工作流状态
            loadWorkflowFromProject(str, false);
        }
        this->project->XMLFile_save(str.toStdString().c_str());
        statusBar()->showMessage(QStringLiteral("已成功加载工程: %1").arg(fileinfo.fileName()), 3000);
    }
    else
        QMessageBox::warning(NULL, "Warning!", "*.Insar is empty!");
}

bool MainWindow::eventFilter(QObject* target, QEvent* event)
{
    bool isWorkspace = m_interfaceManager && m_interfaceManager->currentInterfaceId() == "workspace";
    QTabWidget* activeTabWidget = m_workspaceUI->tabWidget();

    if (isWorkspace && target == activeTabWidget)
    {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Move)
        {
            if (mColors.size() && activeTabWidget->currentIndex() >= 0)
                mColors.at(activeTabWidget->currentIndex())->move(0, 0);
        }
    }
    if (isWorkspace && target == this)
    {
        if (event->type() == QEvent::Move)
        {
            if (mColors.size() && activeTabWidget->currentIndex() >= 0)
                mColors.at(activeTabWidget->currentIndex())->move(0, 0);
        }
    }
    return false;
}

void MainWindow::ShowImage(QModelIndex image)
{

    if (!image.isValid() ||
        !image.parent().isValid() ||
        !image.parent().parent().isValid())
    {
        return;
    }

    QStandardItemModel* currentModel = m_interfaceManager->projectModel();
    if (!currentModel) return;

    QString fileName = currentModel->index(image.row(), 0, image.parent()).data().toString();
    QString nodeName = currentModel->index(image.parent().row(), 0, image.parent().parent()).data().toString();
    QString path = currentModel->index(image.row(), 1, image.parent()).data().toString();

    // 已打开则直接切换到对应tab
    if (CheckTab(image))
        return;

    if (path.isEmpty() || !QFileInfo(path).exists())
    {
        return;
    }

    QString type = currentModel->itemFromIndex(currentModel->index(image.row(),0,image.parent()))->toolTip();
    if (!path.isEmpty())
    {
        QFileInfo fileinfo = QFileInfo(path);
        QString bmp = QString("%1/%2.jpg").arg(fileinfo.absolutePath()).arg(fileinfo.baseName());
        QString path_abs = QString("%1%2%3%4").arg(fileinfo.absolutePath()).arg("/").arg(fileName).arg(".jpg");
        this->bmp_name = QString("%1-%2").arg(nodeName).arg(fileName);
        this->bmp_path = path_abs;

        QFileInfo fileinfo1 = QFileInfo(path_abs);
        QDir dir(fileinfo1.absolutePath());
        bool exists = dir.exists(fileinfo1.baseName() + ".jpg");
        if (exists)
        {
            mData_path = path;
            mType = type;
            Loading(mData_path, mType);
        }
        else if(!image.child(0,0).isValid() && image.parent().isValid())
        {
            QString suffix = fileinfo.suffix().toLower();
            QStringList imageFormats = {"jpg","jpeg","png","bmp","tif","tiff"};
            if (imageFormats.contains(suffix))
            {
                this->bmp_path = path;
                mData_path = path;
                mType = type;
                Loading(mData_path, mType);
            }
            else
            {
                if (this->Process) {
                    this->Process->close();
                    this->Process->deleteLater();
                }
                this->Process = new QProgressDialog("Loading Image...", nullptr, 0, 0, this);
                Process->setFixedSize(450, 100);
                Process->setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
                Process->setWindowTitle(QStringLiteral("Loading Result"));
                Process->setValue(0);
                Process->show();
                if (m_statusProgressBar) {
                    m_statusProgressBar->setValue(0);
                    m_statusProgressBar->show();
                }
                
                QFutureWatcher<bool>* watcher = new QFutureWatcher<bool>(this);
                watcher->setProperty("path_abs", path_abs);
                watcher->setProperty("mData_path", path);
                watcher->setProperty("mType", type);
                watcher->setProperty("bmp_path", path_abs);
                watcher->setProperty("bmp_name", this->bmp_name);
                watcher->setProperty("progress", QVariant::fromValue(static_cast<void*>(Process)));
                connect(watcher, &QFutureWatcher<bool>::finished, this, &MainWindow::onLoadImageFinished);
                
                QFuture<bool> future = QtConcurrent::run(NodeUtils::generateJpgPreviewFromH5, path, path_abs, type);
                watcher->setFuture(future);
            }
        }
    }
}

void MainWindow::onLoadImageFinished()
{
    QFutureWatcher<bool>* watcher = static_cast<QFutureWatcher<bool>*>(sender());
    if (!watcher) return;
    
    bool success = watcher->result();
    QString path_abs = watcher->property("path_abs").toString();
    QString mData_path_val = watcher->property("mData_path").toString();
    QString mType_val = watcher->property("mType").toString();
    QString bmp_path_val = watcher->property("bmp_path").toString();
    QString bmp_name_val = watcher->property("bmp_name").toString();
    QProgressDialog* progress = static_cast<QProgressDialog*>(watcher->property("progress").value<void*>());
    
    if (progress) {
        progress->setValue(100);
        progress->deleteLater();
        if (this->Process == progress) {
            this->Process = nullptr;
        }
    }
    if (m_statusProgressBar) {
        m_statusProgressBar->setValue(100);
        m_statusProgressBar->hide();
    }
    watcher->deleteLater();
    
    if (success) {
        Loading(mData_path_val, mType_val, bmp_path_val, bmp_name_val);
    } else {
        QFile::remove(path_abs);
        QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("图像预览生成失败！"));
    }
}
void MainWindow::on_actionNew_triggered()
{
    if (!maybeSave())
        return;

    NewProject newpro(this);
    connect(this, &MainWindow::sendModel, &newpro, &NewProject::ReceiveModel);
    emit sendModel(m_interfaceManager->projectModel());
    
    if (newpro.exec() == QDialog::Accepted)
    {
        // 只有在用户点击“确认”并验证通过后，才关闭当前工程并添加新工程
        closeCurrentProject();
        Addproject(newpro.project, newpro.save);
    }
}
void MainWindow::on_actionOpen_triggered()
{
    if (!maybeSave())
        return;

    OpenProject open_Window(this);
    connect(this, &MainWindow::sendModel, &open_Window, &OpenProject::LoadModel);
    emit sendModel(m_interfaceManager->projectModel());
    
    connect(&open_Window, &OpenProject::aboutToLoadProject, this, &MainWindow::closeCurrentProject);
    connect(&open_Window, &OpenProject::sendModel, m_workspaceUI, &WorkspaceUI::updateProjectModel);
    connect(&open_Window, &OpenProject::projectOpened, this, [this](const QString& projectFilePath) {
        loadWorkflowFromProject(projectFilePath, true);
    });
    
    open_Window.exec();
}
void MainWindow::updateWindowTitle()
{
    if (m_projectPath.isEmpty()) {
        setWindowTitle("SatExplorer");
    } else {
        QFileInfo fileInfo(m_projectPath);
        QString title = QString("SatExplorer - %1").arg(fileInfo.fileName());
        if (m_projectModified) {
            title += " *";
        }
        setWindowTitle(title);
    }
}
void MainWindow::on_actionSave_triggered()
{
    if (m_projectPath.isEmpty()) {
        QMessageBox::warning(this, "提示", "没有打开的工程，无法保存。");
        return;
    }
    if (this->project) {
        this->project->XMLFile_load(m_projectPath.toStdString().c_str());
    }
    saveWorkflowToProject(m_projectPath);
    if (m_interfaceManager && this->project) {
        m_interfaceManager->saveLastInterfaceToProject(this->project);
    }
    this->project->XMLFile_save(m_projectPath.toStdString().c_str());

    // 添加或更新到最近打开列表
    addToRecentProjects(m_projectPath);

    m_projectModified = false;
    updateWindowTitle();
    statusBar()->showMessage(QStringLiteral("工程保存成功"), 3000);
}
void MainWindow::on_actionSave_as_triggered()
{
    if (m_projectPath.isEmpty() || !this->project) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("没有打开的工程，无法另存为。"));
        return;
    }

    // 1. 弹出保存文件对话框
    QString newFilePath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("工程另存为"),
        m_projectPath,
        QStringLiteral("InSAR Project (*.insar);;All Files (*)")
    );

    if (newFilePath.isEmpty()) {
        return;
    }

    if (!newFilePath.endsWith(".insar", Qt::CaseInsensitive)) {
        newFilePath += ".insar";
    }

    // 如果选择的新路径和当前路径一致，相当于直接保存
    if (QFileInfo(newFilePath).absoluteFilePath() == QFileInfo(m_projectPath).absoluteFilePath()) {
        on_actionSave_triggered();
        return;
    }

    // 2. 先从当前磁盘文件同步最新数据，确保所有外部写入的节点都加载到内存中
    this->project->XMLFile_load(m_projectPath.toStdString().c_str());

    // 3. 更新 XML 内存结构中的 project_name 和 project_path
    QFileInfo newFileInfo(newFilePath);
    QString newProjectName = newFileInfo.fileName(); // 另存为时使用含后缀的完整文件名，确保与系统全局的工程名形式一致
    QString newProjectDir = newFileInfo.absolutePath();

    TiXmlElement* root = nullptr;
    if (this->project->get_root(root) >= 0 && root) {
        TiXmlElement* p = root->FirstChildElement("project_info");
        if (p) {
            TiXmlElement* nameElem = p->FirstChildElement("project_name");
            if (nameElem) {
                nameElem->Clear();
                nameElem->LinkEndChild(new TiXmlText(newProjectName.toStdString().c_str()));
            }
            TiXmlElement* pathElem = p->FirstChildElement("project_path");
            if (pathElem) {
                pathElem->Clear();
                pathElem->LinkEndChild(new TiXmlText(newProjectDir.toStdString().c_str()));
            }
        }
    }

    // 4. 保存工作流与界面状态到内存中
    saveWorkflowToProject(newFilePath);
    if (m_interfaceManager) {
        m_interfaceManager->saveLastInterfaceToProject(this->project);
    }

    // 4.5. 检查新旧工程是否在同一个目录下
    QString oldProjectDir = QFileInfo(m_projectPath).absolutePath();
    bool isSameDir = (QString::compare(QFileInfo(oldProjectDir).absoluteFilePath(), QFileInfo(newProjectDir).absoluteFilePath(), Qt::CaseInsensitive) == 0);

    bool copyData = false;
    if (isSameDir) {
        // 同一目录下，不复制关联数据文件（避免同名覆盖自删），但需要静默复制并重命名 GCP 数据库
        QString oldProjBase = QFileInfo(m_projectPath).baseName();
        QString newProjBase = newFileInfo.baseName();
        QString srcDb = oldProjectDir + "/" + oldProjBase + "_gcp.db";
        QString dstDb = newProjectDir + "/" + newProjBase + "_gcp.db";
        if (QFile::exists(srcDb) && QString::compare(QFileInfo(srcDb).absoluteFilePath(), QFileInfo(dstDb).absoluteFilePath(), Qt::CaseInsensitive) != 0) {
            if (QFile::exists(dstDb)) {
                QFile::remove(dstDb);
            }
            QFile::copy(srcDb, dstDb);
        }
    } else {
        // 不同目录下，询问用户是否复制关联的数据文件
        QMessageBox::StandardButton reply = QMessageBox::question(
            this,
            QStringLiteral("复制数据"),
            QStringLiteral("是否将当前工程中已处理的数据文件（如H5影像）一同复制到新的目录？"),
            QMessageBox::Yes | QMessageBox::No
        );
        if (reply == QMessageBox::Yes) {
            copyData = true;
        }
    }

    if (copyData) {
        // 解析 XML 并获取所有相对路径
        QStringList relativePaths;
        TiXmlElement* root = nullptr;
        if (this->project->get_root(root) >= 0 && root) {
            for (TiXmlElement* p = root->FirstChildElement(); p != nullptr; p = p->NextSiblingElement()) {
                if (p->Value() && strcmp(p->Value(), "DataNode") == 0) {
                    for (TiXmlElement* q = p->FirstChildElement(); q != nullptr; q = q->NextSiblingElement()) {
                        if (q->Value() && strcmp(q->Value(), "Data") == 0) {
                            TiXmlElement* pathElem = q->FirstChildElement("Data_Path");
                            if (pathElem && pathElem->GetText()) {
                                QString relPath = QString::fromUtf8(pathElem->GetText());
                                if (!relPath.isEmpty()) {
                                    relativePaths.append(relPath);
                                }
                            }
                        }
                    }
                }
            }
        }

        // 构建需要拷贝的文件列表（源路径 -> 目标路径）
        QList<QPair<QString, QString>> filesToCopy;

        for (const QString& relPath : relativePaths) {
            // H5 数据文件
            QString srcH5 = oldProjectDir + relPath;
            QString dstH5 = newProjectDir + relPath;
            if (QFile::exists(srcH5)) {
                filesToCopy.append(qMakePair(srcH5, dstH5));
            }

            // 对应的 JPG 预览图文件
            int dotIdx = relPath.lastIndexOf('.');
            if (dotIdx != -1) {
                QString relJpg = relPath.left(dotIdx) + ".jpg";
                QString srcJpg = oldProjectDir + relJpg;
                QString dstJpg = newProjectDir + relJpg;
                if (QFile::exists(srcJpg)) {
                    filesToCopy.append(qMakePair(srcJpg, dstJpg));
                }
            }
        }

        // 拷贝 GCP 控制点 SQLite 数据库（如果存在且不同名/不同路径）
        QString oldProjBase = QFileInfo(m_projectPath).baseName();
        QString newProjBase = newFileInfo.baseName();
        QString srcDb = oldProjectDir + "/" + oldProjBase + "_gcp.db";
        QString dstDb = newProjectDir + "/" + newProjBase + "_gcp.db";
        if (QFile::exists(srcDb) && QString::compare(QFileInfo(srcDb).absoluteFilePath(), QFileInfo(dstDb).absoluteFilePath(), Qt::CaseInsensitive) != 0) {
            filesToCopy.append(qMakePair(srcDb, dstDb));
        }

        // 执行后台多线程文件复制
        if (!filesToCopy.isEmpty()) {
            QStringList failedFiles;
            std::shared_ptr<std::atomic<bool>> cancelFlag = std::make_shared<std::atomic<bool>>(false);

            QProgressDialog progress(
                QStringLiteral("正在复制项目数据..."),
                QStringLiteral("取消"),
                0,
                filesToCopy.size(),
                this
            );
            progress.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
            progress.setWindowTitle(QStringLiteral("项目另存为"));
            progress.setWindowModality(Qt::WindowModal);
            progress.setMinimumDuration(0);
            progress.setValue(0);
            progress.show();

            connect(&progress, &QProgressDialog::canceled, [cancelFlag]() {
                cancelFlag->store(true);
            });

            QFuture<void> future = QtConcurrent::run([filesToCopy, cancelFlag, &progress, &failedFiles]() {
                for (int i = 0; i < filesToCopy.size(); ++i) {
                    if (cancelFlag->load()) {
                        break;
                    }
                    const auto& pair = filesToCopy.at(i);
                    QString src = pair.first;
                    QString dst = pair.second;

                    // 防御性检查：如果源路径与目标路径完全相同，则跳过复制，避免自删
                    if (QString::compare(QFileInfo(src).absoluteFilePath(), QFileInfo(dst).absoluteFilePath(), Qt::CaseInsensitive) == 0) {
                        continue;
                    }

                    // 自动建立目标文件夹
                    QDir().mkpath(QFileInfo(dst).absolutePath());

                    // 如果目标文件已存在，先删除再覆盖
                    if (QFile::exists(dst)) {
                        QFile::remove(dst);
                    }

                    if (!QFile::copy(src, dst)) {
                        failedFiles.append(src);
                    }

                    int val = i + 1;
                    QString labelText = QStringLiteral("正在复制文件 (%1/%2):\n%3")
                                        .arg(val)
                                        .arg(filesToCopy.size())
                                        .arg(QFileInfo(src).fileName());
                    QMetaObject::invokeMethod(&progress, "setValue", Qt::QueuedConnection, Q_ARG(int, val));
                    QMetaObject::invokeMethod(&progress, "setLabelText", Qt::QueuedConnection, Q_ARG(QString, labelText));
                }
            });

            QEventLoop loop;
            QFutureWatcher<void> watcher;
            connect(&watcher, &QFutureWatcher<void>::finished, &loop, &QEventLoop::quit);
            watcher.setFuture(future);

            // 阻塞主线程直到复制完成，确保局部变量生命周期安全，且主事件循环仍能响应UI
            loop.exec();

            if (cancelFlag->load()) {
                QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("另存为已取消，部分文件可能未被成功复制。"));
                return;
            }

            if (!failedFiles.isEmpty()) {
                QMessageBox::warning(
                    this,
                    QStringLiteral("警告"),
                    QStringLiteral("部分数据文件复制失败，请检查目标磁盘空间或权限。\n失败文件数：%1").arg(failedFiles.size())
                );
            }
        }
    }

    // 5. 保存到新路径
    if (this->project->XMLFile_save(newFilePath.toStdString().c_str()) < 0) {
        QMessageBox::critical(this, QStringLiteral("错误"), QStringLiteral("另存工程失败！"));
        return;
    }

    // 6. 重置修改标记，用新路径重新加载工程，刷新所有 UI 和树节点绝对路径
    m_projectModified = false;
    open_from_project_file(newFilePath);
}
bool MainWindow::maybeSave()
{
    if (m_projectPath.isEmpty() || !m_projectModified)
        return true;

    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        QStringLiteral("保存提示"),
        QStringLiteral("当前工程已修改，是否保存？"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel
    );

    if (reply == QMessageBox::Save) {
        on_actionSave_triggered();
        return true;
    } else if (reply == QMessageBox::Discard) {
        return true;
    } else {
        return false;
    }
}
void MainWindow::closeCurrentProject()
{
    // 清空工程路径
    m_projectPath.clear();

    // 重置 XMLFile
    if (this->project) {
        delete this->project;
        this->project = new XMLFile;
    }

    // 释放工作流 JSON 数据
    m_workflowBytes.clear();

    // 清空主树形视图模型
    QStandardItemModel* currentModel = m_interfaceManager ? m_interfaceManager->projectModel() : nullptr;
    if (currentModel) {
        currentModel->clear();
        currentModel->setColumnCount(2);
        currentModel->setHeaderData(0, Qt::Horizontal, tr("workspace"));
        currentModel->setHeaderData(1, Qt::Horizontal, tr("Path"));
    }

    // 清空工作区（通过接口）
    if (m_workspaceUI) {
        m_workspaceUI->clear();
    }

    // 清空 MainWindow 中的颜色列表，避免悬空指针
    mColors.clear();
    mExist_Color.clear();
    ColorBar_Before = -1;
    TabCount_Before = -1;

    // 清空流程编辑器（通过接口）
    if (m_workflowUI) {
        m_workflowUI->clear();
    }

    // 重置两个界面的工程上下文（通过接口）
    if (m_workspaceUI)
        m_workspaceUI->setProjectContext(currentModel, QString(), QString(), nullptr);
    if (m_workflowUI)
        m_workflowUI->setProjectContext(currentModel, QString(), QString(), nullptr);

    // 同步重置 InterfaceManager 的项目上下文
    if (m_interfaceManager) {
        m_interfaceManager->setProjectContext(currentModel, QString(), QString(), nullptr);
    }

    // 禁用处理菜单（恢复到初始状态）
    ui.menuImport_Top->setDisabled(1);
    ui.menuPreprocessing->setDisabled(1);
    ui.menuInSAR->setDisabled(1);
    ui.menuDInSAR->setDisabled(1);
    ui.menuSAR->setDisabled(1);
    ui.menuExport->setDisabled(1);

    // 清空标签页
    if (m_workspaceUI && m_workspaceUI->tabWidget()) {
        QTabWidget* closeTabWidget = m_workspaceUI->tabWidget();
        while (closeTabWidget->count() > 0)
            closeTabWidget->removeTab(0);
    }

    // 重置工程修改标记
    m_projectModified = false;
    updateWindowTitle();
    updateFileMenuState();

    updateStatusBarProject("");
    statusBar()->showMessage(QStringLiteral("工程已关闭"), 3000);
}

void MainWindow::on_actionClose_triggered()
{
    if (!m_projectPath.isEmpty() && m_projectModified) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, "关闭工程",
            "是否保存当前工程？",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

        if (reply == QMessageBox::Save) {
            if (this->project) {
                this->project->XMLFile_load(m_projectPath.toStdString().c_str());
            }
            saveWorkflowToProject(m_projectPath);
            if (m_interfaceManager && this->project) {
                m_interfaceManager->saveLastInterfaceToProject(this->project);
            }
            this->project->XMLFile_save(m_projectPath.toStdString().c_str());
        } else if (reply == QMessageBox::Cancel) {
            return;
        }
    }

    closeCurrentProject();

    // 切换到欢迎界面
    if (m_interfaceManager) {
        m_interfaceManager->switchToInterface("welcome");
        updateInterfaceMenuCheckState();
    }
}

void MainWindow::on_actionCleanOrphanedFiles_triggered()
{
    if (m_workspaceUI && m_workspaceUI->treeView()) {
        m_workspaceUI->treeView()->CleanOrphanedFiles();
    }
}

void MainWindow::saveWorkflowToProject(const QString& projectFilePath)
{
    if (!m_workflowUI || !this->project)
        return;

    QJsonObject workflowJson = m_workflowUI->saveWorkflowToJson();
    if (workflowJson.isEmpty())
        return;

    // 格式化输出 + CDATA 包裹，使人可以直接阅读
    m_workflowBytes = QJsonDocument(workflowJson).toJson(QJsonDocument::Indented);

    TiXmlElement* root = nullptr;
    this->project->get_root(root);
    if (!root)
        return;

    TiXmlElement* pnode = nullptr;
    this->project->_find_node(root, "workflow", pnode);
    if (!pnode) {
        pnode = new TiXmlElement("workflow");
        root->LinkEndChild(pnode);
    } else {
        pnode->Clear();
    }
    TiXmlText* textNode = new TiXmlText(m_workflowBytes.constData());
    textNode->SetCDATA(true);
    pnode->LinkEndChild(textNode);
}
void MainWindow::loadWorkflowFromProject(const QString& projectFilePath, bool reloadProjectXml)
{
    if (!m_workflowUI || projectFilePath.isEmpty())
        return;

    try {
        // 记录当前工程路径
        updateProjectContext(projectFilePath);

        // 直接打开路径已加载并修正 project_info，不能在此重载，否则会丢失内存中的修正。
        if (reloadProjectXml && this->project)
        {
            int ret = this->project->XMLFile_load(projectFilePath.toStdString().c_str());
            if (ret < 0)
                return;
        }

        // 读取 <workflow> 元素
        TiXmlElement* root = nullptr;
        this->project->get_root(root);
        if (!root)
            return;

        TiXmlElement* workflowNode = nullptr;
        this->project->_find_node(root, "workflow", workflowNode);
        if (!workflowNode || !workflowNode->GetText())
            return;

        QByteArray workflowData = QByteArray(workflowNode->GetText());
        QJsonDocument doc = QJsonDocument::fromJson(workflowData);
        if (doc.isObject()) {
            m_workflowUI->loadWorkflowFromJson(doc.object());
        }

        // 切换到上次使用的界面
        if (m_interfaceManager && this->project)
        {
            QString lastInterface = m_interfaceManager->loadLastInterfaceFromProject(this->project);
            if (!lastInterface.isEmpty()) {
                m_interfaceManager->switchToInterface(lastInterface);
            }
            else {
                m_interfaceManager->switchToInterface("workspace");
            }
            updateInterfaceMenuCheckState();
        }

        // 添加到最近打开列表
        addToRecentProjects(projectFilePath);
    }
    catch (const std::exception& e) {
        QMessageBox::critical(this, "错误", QString("加载工作流失败：") + QString::fromUtf8(e.what()));
    }
    catch (...) {
        QMessageBox::critical(this, "错误", "加载工作流时发生未知异常。");
    }

    // 加载完成，重置修改标记
    m_projectModified = false;
    updateWindowTitle();
}
void MainWindow::addToRecentProjects(const QString& path)
{
    if (path.isEmpty())
        return;

    QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
    QStringList recent = settings.value("Recent/Projects", QStringList()).toStringList();

    // 移除已存在的相同路径，避免重复
    recent.removeAll(path);

    // 插入到列表头部
    recent.prepend(path);

    // 最多保留 10 个
    while (recent.size() > 10)
        recent.removeLast();

    settings.setValue("Recent/Projects", recent);

    // 刷新 WelcomeScreen 和文件菜单的最近项目列表
    if (m_welcomeUI)
        m_welcomeUI->refreshRecentProjects();
    updateRecentMenu();
}
void MainWindow::updateRecentMenu()
{
    m_recentMenu->clear();

    QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
    QStringList recent = settings.value("Recent/Projects", QStringList()).toStringList();

    if (recent.isEmpty()) {
        QAction* emptyAction = m_recentMenu->addAction("无最近项目");
        emptyAction->setEnabled(false);
        return;
    }

    for (int i = 0; i < recent.size(); ++i) {
        const QString& path = recent[i];
        if (path.isEmpty())
            continue;

        QFileInfo info(path);
        QString displayName = info.completeBaseName();
        QString displayPath = info.path();

        // 显示格式：项目名  —  路径
        QAction* action = m_recentMenu->addAction(displayName + "  —  " + displayPath);
        action->setData(path);
        action->setToolTip(path);

        // 用序号标记，便于识别
        action->setShortcut(QKeySequence());

        connect(action, &QAction::triggered, this, &MainWindow::openRecentProject);
    }

    m_recentMenu->addSeparator();
    QAction* clearAction = m_recentMenu->addAction("清除最近列表");
    connect(clearAction, &QAction::triggered, this, [this]() {
        QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
        settings.remove("Recent/Projects");
        updateRecentMenu();
        if (m_welcomeUI)
            m_welcomeUI->refreshRecentProjects();
    });
}
void MainWindow::openRecentProject()
{
    QAction* action = qobject_cast<QAction*>(sender());
    if (!action)
        return;

    QString filePath = action->data().toString();
    if (filePath.isEmpty() || !QFileInfo::exists(filePath)) {
        handleInvalidRecentProject(filePath);
        return;
    }

    try {
        open_from_project_file(filePath);
    }
    catch (const std::exception& e) {
        QMessageBox::critical(this, QStringLiteral("错误"), QStringLiteral("加载项目失败：") + QString::fromUtf8(e.what()));
    }
    catch (...) {
        QMessageBox::critical(this, QStringLiteral("错误"), QStringLiteral("加载项目时发生未知异常。"));
    }
}
void MainWindow::handleInvalidRecentProject(const QString& filePath)
{
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        QStringLiteral("提示"),
        QStringLiteral("项目文件不存在：%1\n\n是否从历史记录中删除该项？").arg(filePath),
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply == QMessageBox::Yes) {
        QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
        QStringList recent = settings.value("Recent/Projects", QStringList()).toStringList();
        recent.removeAll(filePath);
        settings.setValue("Recent/Projects", recent);

        // 刷新欢迎界面和文件菜单最近项目列表
        if (m_welcomeUI)
            m_welcomeUI->refreshRecentProjects();
        updateRecentMenu();
    }
}
void MainWindow::on_actionTSX_triggered()
{

    Import_TSX* TSX_win = new Import_TSX;
    connect(this, &MainWindow::sendModel, TSX_win, &Import_TSX::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    TSX_win->show();

    connect(TSX_win, &Import_TSX::sendCopy, m_workspaceUI, &WorkspaceUI::updateProjectModel);
    TSX_win->setAttribute(Qt::WA_DeleteOnClose, true);
}
void MainWindow::on_actionGenericSAR_triggered()
{

    Import_GenericSAR* GenericSAR_win = new Import_GenericSAR;
    connect(this, &MainWindow::sendModel, GenericSAR_win, &Import_GenericSAR::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    GenericSAR_win->show();

    connect(GenericSAR_win, &Import_GenericSAR::sendCopy, m_workspaceUI, &WorkspaceUI::updateProjectModel);
    GenericSAR_win->setAttribute(Qt::WA_DeleteOnClose, true);
}
void MainWindow::on_actionSentinel_1_triggered()
{
    import_sentinel* sentinel_wnd = new import_sentinel;
    connect(this, &MainWindow::sendModel, sentinel_wnd, &import_sentinel::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    sentinel_wnd->show();

    connect(sentinel_wnd, &import_sentinel::sendCopy, m_workspaceUI, &WorkspaceUI::updateProjectModel);
    sentinel_wnd->setAttribute(Qt::WA_DeleteOnClose, true);
}
void MainWindow::on_actionCut_triggered()
{
    Cut* cut = new Cut();
    connect(this, &MainWindow::sendModel, cut, &Cut::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    cut->show();
    connect(cut, &Cut::sendCopy, m_workspaceUI, &WorkspaceUI::updateProjectModel);
    cut->setAttribute(Qt::WA_DeleteOnClose, true);
}
void MainWindow::on_actionOrbitRefinement_triggered()
{
    if (m_interfaceManager && m_interfaceManager->currentInterfaceId() == "workflow") {
        if (m_workflowUI) {
            m_workflowUI->onNodeDoubleClicked("OrbitRefinement");
        }
    } else {
        QMessageBox::information(this, QStringLiteral("提示"), 
            QStringLiteral("轨道精炼功能目前仅在工作流编辑器（Node Editor）中支持，请切换界面后在画布上双击或通过该菜单创建节点使用！"));
    }
}
void MainWindow::on_actionRegistration_triggered()
{
    Registration_ui* regis = new Registration_ui();
    connect(this, &MainWindow::sendModel, regis, &Registration_ui::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    regis->show();
    connect(regis, &Registration_ui::sendCopy, m_workspaceUI, &WorkspaceUI::updateProjectModel);
    regis->setAttribute(Qt::WA_DeleteOnClose, true);
}
void MainWindow::on_actionS1_TOPS_BackGeocoding_triggered()
{
    S1_TOPS_BackGeocoding* backGeocoding = new S1_TOPS_BackGeocoding();
    connect(this, &MainWindow::sendModel, backGeocoding, &S1_TOPS_BackGeocoding::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    backGeocoding->show();
    connect(backGeocoding, &S1_TOPS_BackGeocoding::sendCopy, m_workspaceUI, &WorkspaceUI::updateProjectModel);
    backGeocoding->setAttribute(Qt::WA_DeleteOnClose, true);
}

void MainWindow::on_actionS1_Deburst_triggered()
{
    S1_Deburst* deburst = new S1_Deburst();
    connect(this, &MainWindow::sendModel, deburst, &S1_Deburst::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    deburst->show();
    connect(deburst, &S1_Deburst::sendCopy, m_workspaceUI, &WorkspaceUI::updateProjectModel);
    deburst->setAttribute(Qt::WA_DeleteOnClose, true);
}

void MainWindow::on_actionSBAS_deformation_triggered()
{
    SBAS_time_series_analysis* SBAS_time_series = new SBAS_time_series_analysis();
    connect(this, &MainWindow::sendModel, SBAS_time_series, &SBAS_time_series_analysis::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    SBAS_time_series->show();
    connect(SBAS_time_series, &SBAS_time_series_analysis::sendCopy, m_workspaceUI, &WorkspaceUI::updateProjectModel);
    SBAS_time_series->setAttribute(Qt::WA_DeleteOnClose, true);
}

void MainWindow::on_actionDeformationRateField_triggered()
{
    DeformationRateField_ui* dialog = new DeformationRateField_ui();
    connect(this, &MainWindow::sendModel, dialog, &DeformationRateField_ui::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    dialog->show();
    connect(dialog, &DeformationRateField_ui::sendCopy, m_workspaceUI, &WorkspaceUI::updateProjectModel);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
}

void MainWindow::on_actionPSI_Candidate_triggered()
{
    QtNodes::PS_Candidate_Dialog* dlg = new QtNodes::PS_Candidate_Dialog(this);
    connect(this, &MainWindow::sendModel, dlg, &QtNodes::PS_Candidate_Dialog::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    dlg->show();
    connect(dlg, &QtNodes::PS_Candidate_Dialog::sendCopy, m_workspaceUI, &WorkspaceUI::updateProjectModel);
    dlg->setAttribute(Qt::WA_DeleteOnClose, true);
}

void MainWindow::on_actionPSI_Network_triggered()
{
    QtNodes::PS_Network_Dialog* dlg = new QtNodes::PS_Network_Dialog(this);
    connect(this, &MainWindow::sendModel, dlg, &QtNodes::PS_Network_Dialog::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    dlg->show();
    connect(dlg, &QtNodes::PS_Network_Dialog::sendCopy, m_workspaceUI, &WorkspaceUI::updateProjectModel);
    dlg->setAttribute(Qt::WA_DeleteOnClose, true);
}

void MainWindow::on_actionPSI_TimeSeries_triggered()
{
    QtNodes::PS_TimeSeries_Dialog* dlg = new QtNodes::PS_TimeSeries_Dialog(this);
    connect(this, &MainWindow::sendModel, dlg, &QtNodes::PS_TimeSeries_Dialog::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    dlg->show();
    connect(dlg, &QtNodes::PS_TimeSeries_Dialog::sendCopy, m_workspaceUI, &WorkspaceUI::updateProjectModel);
    dlg->setAttribute(Qt::WA_DeleteOnClose, true);
}

void MainWindow::on_actionDeformation_Preview_triggered()
{
    Deformation_Average* bl = new Deformation_Average();
    connect(this, &MainWindow::sendModel, bl, &Deformation_Average::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    bl->show();
    bl->setAttribute(Qt::WA_DeleteOnClose, true);
}

void MainWindow::on_actionreference_re_selection_triggered()
{
    SBAS_reference_reselection* bl = new SBAS_reference_reselection();
    connect(this, &MainWindow::sendModel, bl, &SBAS_reference_reselection::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    bl->show();
    bl->setAttribute(Qt::WA_DeleteOnClose, true);
}

void MainWindow::on_actionExport_KML_triggered()
{
    Export_KML* bl = new Export_KML();
    connect(this, &MainWindow::sendModel, bl, &Export_KML::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    bl->show();
    bl->setAttribute(Qt::WA_DeleteOnClose, true);
}
void MainWindow::on_actionSpeckleDenoise_triggered()
{
     SpeckleDenoise* dlg = new SpeckleDenoise();
    connect(this, &MainWindow::sendModel, dlg, &SpeckleDenoise::ShowProjectList);
    connect(dlg, &SpeckleDenoise::sendCopy, m_workspaceUI, &WorkspaceUI::updateProjectModel);
    emit sendModel(m_interfaceManager->projectModel());
    dlg->show();
    dlg->setAttribute(Qt::WA_DeleteOnClose, true);
}

void MainWindow::on_actionClutterSuppression_triggered()
{
    ClutterSuppression* dlg = new ClutterSuppression();
    connect(this, &MainWindow::sendModel, dlg, &ClutterSuppression::ShowProjectList);
    connect(dlg, &ClutterSuppression::sendCopy, m_workspaceUI, &WorkspaceUI::updateProjectModel);
    emit sendModel(m_interfaceManager->projectModel());
    dlg->show();
    dlg->setAttribute(Qt::WA_DeleteOnClose, true);
}


void MainWindow::on_actionBatchTargetRecognition_triggered()
{
    BatchTargetRecognition* dlg = new BatchTargetRecognition();
    connect(this, &MainWindow::sendModel, dlg, &BatchTargetRecognition::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    dlg->show();
    dlg->setAttribute(Qt::WA_DeleteOnClose, true);
}

void MainWindow::on_actionTargetDetection_triggered()
{
    TargetDetection* dlg = new TargetDetection();
    connect(this, &MainWindow::sendModel, dlg, &TargetDetection::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    dlg->show();
    dlg->setAttribute(Qt::WA_DeleteOnClose, true);
}

void MainWindow::on_actionBaseline_Preview_triggered()
{
    Baseline* bl = new Baseline();
    connect(this, &MainWindow::sendModel, bl, &Baseline::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    bl->show();
    bl->setAttribute(Qt::WA_DeleteOnClose, true);
}
void MainWindow::on_actionSLC_deramp_triggered()
{
    SLC_deramp* deramp = new SLC_deramp();
    connect(this, &MainWindow::sendModel, deramp, &SLC_deramp::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    deramp->show();
    deramp->setAttribute(Qt::WA_DeleteOnClose, true);
}
void MainWindow::on_actionBaseline_Formation_triggered()
{
    Baseline_Formation* BF = new Baseline_Formation();
    connect(this, &MainWindow::sendModel, BF, &Baseline_Formation::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    BF->show();
    BF->setAttribute(Qt::WA_DeleteOnClose, true);
}
void MainWindow::on_actionInterferometric_Formation_triggered()
{
    Interferometric_Formation* IF = new Interferometric_Formation();
    connect(this, &MainWindow::sendModel, IF, &Interferometric_Formation::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    IF->show();
    connect(IF, &Interferometric_Formation::sendCopy, m_workspaceUI, &WorkspaceUI::updateProjectModel);
    IF->setAttribute(Qt::WA_DeleteOnClose, true);
}
void MainWindow::on_actionDenoise_triggered()
{
    Filter_ui *Denoise = new Filter_ui();
    connect(this, &MainWindow::sendModel, Denoise, &Filter_ui::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    Denoise->show();
    connect(Denoise, &Filter_ui::sendCopy, m_workspaceUI, &WorkspaceUI::updateProjectModel);
    Denoise->setAttribute(Qt::WA_DeleteOnClose, true);
}
void MainWindow::on_actionUnwrap_triggered()
{
    Unwrap_ui* unwrap = new Unwrap_ui();
    connect(this, &MainWindow::sendModel, unwrap, &Unwrap_ui::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    unwrap->show();
    connect(unwrap, &Unwrap_ui::sendCopy, m_workspaceUI, &WorkspaceUI::updateProjectModel);
    unwrap->setAttribute(Qt::WA_DeleteOnClose, true);
}
void MainWindow::on_actionDEM_triggered()
{
    Dem_ui* Dem = new Dem_ui();
    connect(this, &MainWindow::sendModel, Dem, &Dem_ui::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    Dem->show();
    connect(Dem, &Dem_ui::sendCopy, m_workspaceUI, &WorkspaceUI::updateProjectModel);
    Dem->setAttribute(Qt::WA_DeleteOnClose, true);
}
void MainWindow::on_actionExternal_DEM_triggered()
{
    DEMSourceDialog* demSourceDlg = new DEMSourceDialog(this);
    connect(this, &MainWindow::sendModel, demSourceDlg, &DEMSourceDialog::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    demSourceDlg->show();
    connect(demSourceDlg, &DEMSourceDialog::sendCopy, m_workspaceUI, &WorkspaceUI::updateProjectModel);
    demSourceDlg->setAttribute(Qt::WA_DeleteOnClose, true);
}
void MainWindow::on_actiongeocode_triggered()
{
    Geocoding* geocode = new Geocoding();
    connect(this, &MainWindow::sendModel, geocode, &Geocoding::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    geocode->show();
    connect(geocode, &Geocoding::sendCopy, m_workspaceUI, &WorkspaceUI::updateProjectModel);
    geocode->setAttribute(Qt::WA_DeleteOnClose, true);
}
void MainWindow::on_actionS1_swath_merge_triggered()
{
    S1_swath_merge* swath_merge = new S1_swath_merge();
    connect(this, &MainWindow::sendModel, swath_merge, &S1_swath_merge::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    swath_merge->show();
    connect(swath_merge, &S1_swath_merge::sendCopy, m_workspaceUI, &WorkspaceUI::updateProjectModel);
    swath_merge->setAttribute(Qt::WA_DeleteOnClose, true);
}
void MainWindow::on_actionS1_frame_merge_triggered()
{
    S1_frame_merge* frame_merge = new S1_frame_merge();
    connect(this, &MainWindow::sendModel, frame_merge, &S1_frame_merge::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    frame_merge->show();
    connect(frame_merge, &S1_frame_merge::sendCopy, m_workspaceUI, &WorkspaceUI::updateProjectModel);
    frame_merge->setAttribute(Qt::WA_DeleteOnClose, true);
}
void MainWindow::on_actionCOSMOS_SkyMed_triggered()
{
    import_CSK* csk = new import_CSK;
    connect(this, &MainWindow::sendModel, csk, &import_CSK::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    csk->show();

    connect(csk, &import_CSK::sendCopy, m_workspaceUI, &WorkspaceUI::updateProjectModel);
    csk->setAttribute(Qt::WA_DeleteOnClose, true);
}
void MainWindow::on_actionALOS_2_triggered()
{
    import_ALOS2* alos2 = new import_ALOS2;
    connect(this, &MainWindow::sendModel, alos2, &import_ALOS2::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    alos2->show();

    connect(alos2, &import_ALOS2::sendCopy, m_workspaceUI, &WorkspaceUI::updateProjectModel);
    alos2->setAttribute(Qt::WA_DeleteOnClose, true);
}

void MainWindow::on_actionLuTan_1_triggered()
{
    import_LUTAN* lutan = new import_LUTAN;
    connect(this, &MainWindow::sendModel, lutan, &import_LUTAN::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    lutan->show();

    connect(lutan, &import_LUTAN::sendCopy, m_workspaceUI, &WorkspaceUI::updateProjectModel);
    lutan->setAttribute(Qt::WA_DeleteOnClose, true);
}

void MainWindow::on_actionHongtu_1_triggered()
{
    import_HTHT* htht = new import_HTHT;
    connect(this, &MainWindow::sendModel, htht, &import_HTHT::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    htht->show();

    connect(htht, &import_HTHT::sendCopy, m_workspaceUI, &WorkspaceUI::updateProjectModel);
    htht->setAttribute(Qt::WA_DeleteOnClose, true);
}

void MainWindow::on_actionSpacety_triggered()
{
    import_Spacety* spacety = new import_Spacety;
    connect(this, &MainWindow::sendModel, spacety, &import_Spacety::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    spacety->show();

    connect(spacety, &import_Spacety::sendCopy, m_workspaceUI, &WorkspaceUI::updateProjectModel);
    spacety->setAttribute(Qt::WA_DeleteOnClose, true);
}

void MainWindow::on_actionAIRSAT_triggered()
{
    import_AIRSAT* airsat = new import_AIRSAT;
    connect(this, &MainWindow::sendModel, airsat, &import_AIRSAT::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    airsat->show();

    connect(airsat, &import_AIRSAT::sendCopy, m_workspaceUI, &WorkspaceUI::updateProjectModel);
    airsat->setAttribute(Qt::WA_DeleteOnClose, true);
}

void MainWindow::on_actionBiomass_triggered()
{
    import_Biomass* biomass = new import_Biomass;
    connect(this, &MainWindow::sendModel, biomass, &import_Biomass::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    biomass->show();

    connect(biomass, &import_Biomass::sendCopy, m_workspaceUI, &WorkspaceUI::updateProjectModel);
    biomass->setAttribute(Qt::WA_DeleteOnClose, true);
}

void MainWindow::on_actionLiDAR_triggered()
{
    import_LiDAR* lidar = new import_LiDAR;
    connect(this, &MainWindow::sendModel, lidar, &import_LiDAR::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    lidar->show();

    connect(lidar, &import_LiDAR::sendCopy, m_workspaceUI, &WorkspaceUI::updateProjectModel);
    lidar->setAttribute(Qt::WA_DeleteOnClose, true);
}

void MainWindow::ShowColorBar(int index)
{
    // 界面切换到 Workflow 后直接返回
    if (m_interfaceManager && m_interfaceManager->currentInterfaceId() != "workspace")
        return;

    QTabWidget* activeTabWidget = m_workspaceUI->tabWidget();

    if ( activeTabWidget->count()== mExist_Color.size())
    {
        if (index >= 0 && index < mExist_Color.size() && index < mColors.size())
        {
            if (mExist_Color.at(index))
            {
                if (activeTabWidget->currentWidget())
                {
                    mColors.at(index)->resize(activeTabWidget->currentWidget()->width() / 8, activeTabWidget->currentWidget()->height() / 3);
                    mColors.at(index)->move(0, 0);
                    mColors.at(index)->raise();
                    mColors.at(index)->show();
                }
            }
        }

        if (ColorBar_Before >= 0 && ColorBar_Before < mColors.size())
        {
            mColors.at(ColorBar_Before)->hide();
        }

    }
    if (TabCount_Before >= 0 && TabCount_Before< activeTabWidget->count())
    {
        if (ColorBar_Before >= 0 && ColorBar_Before < mColors.size())
        {
            mColors.at(ColorBar_Before)->hide();
        }
    }
    TabCount_Before = activeTabWidget->count();
    ColorBar_Before = index;
}

bool MainWindow::CheckTab(QModelIndex image)
{
    // 界面切换到 Workflow 后直接返回
    if (m_interfaceManager && m_interfaceManager->currentInterfaceId() != "workspace")
        return false;

    QTabWidget* activeTabWidget = m_workspaceUI->tabWidget();

    int n = activeTabWidget->count();
    int i = 0;
    QStandardItemModel* currentModel = m_interfaceManager->projectModel();
    if (!currentModel) return false;

    QString fileName = currentModel->index(image.row(), 0, image.parent()).data().toString();
    QString nodeName = currentModel->index(image.parent().row(), 0, image.parent().parent()).data().toString();
    QString tabId = QString("%1-%2").arg(nodeName).arg(fileName);

    for (i = 0; i < n; i++)
    {
        if (!QString::compare(activeTabWidget->tabText(i), tabId))
        {
            activeTabWidget->setCurrentIndex(i);
            return true;
        }
    }

    return false;
}

void MainWindow::handleTabCloseRequested(int index)
{
    // 界面切换到 Workflow 后直接返回
    if (m_interfaceManager && m_interfaceManager->currentInterfaceId() != "workspace")
        return;

    QTabWidget* activeTabWidget = m_workspaceUI->tabWidget();

    if (activeTabWidget->widget(index))
    {
        QWidget* tabContent = activeTabWidget->widget(index);
        if (tabContent) {
            ImageView* view = tabContent->findChild<ImageView*>();
            if (view && m_gcpDockWidget) {
                if (m_gcpDockWidget->boundImageView() == view) {
                    m_gcpDockWidget->bindImageView(nullptr);
                }
            }
        }

        cout << activeTabWidget->count();
        cout << "\n" << "close";
       // activeTabWidget->removeTab(index);
        delete(activeTabWidget->widget(index));
        mColors.removeAt(index);
        mExist_Color.removeAt(index);
        if (activeTabWidget->currentIndex() >= 0)
        {
            if (mExist_Color.at(activeTabWidget->currentIndex()))
            {
                mColors.at(activeTabWidget->currentIndex())->resize(activeTabWidget->currentWidget()->width() / 8, activeTabWidget->currentWidget()->height() / 3);
                mColors.at(activeTabWidget->currentIndex())->move(0, 0);
                mColors.at(activeTabWidget->currentIndex())->raise();
                mColors.at(activeTabWidget->currentIndex())->show();
            }

        }

    }
}

void MainWindow::onThemeLight()
{
    setTheme("light");
}

void MainWindow::onThemeDark()
{
    setTheme("dark");
}

void MainWindow::onThemeFusion()
{
    setTheme("fusion");
}

void MainWindow::applyMenuIcons(bool isDark)
{
    auto getActionSemanticColor = [](const QString& name, bool isDark) -> QColor {
        // 5. Red (Safety & Termination)
        if (name == "actionQuit" || name == "actionCleanOrphanedFiles") {
            return isDark ? QColor("#FFB4AB") : QColor("#BA1A1A");
        }
        // 2. Green/Teal (Import/Export/Geocoding/Evaluation/KML/GCP)
        if (name.contains("Import") || name.contains("Export") || name == "actionEvaluation" ||
            name == "actionTSX" || name == "actionSentinel_1" || name == "actionCOSMOS_SkyMed" ||
            name == "actionALOS_2" || name == "actionGenericSAR" || name == "actionExport_KML" ||
            name == "actiongeocode" || name == "menuImport" || name == "menuImport_2" ||
            name == "actionLuTan_1" || name == "actionHongtu_1" || name == "actionSpacety" ||
            name == "actionAIRSAT" || name == "actionBiomass" || name == "actionLiDAR" ||
            name == "actionGcpManager" || name == "actionExternal_DEM") {
            return isDark ? QColor("#47D8A4") : QColor("#0F7D5C");
        }
        // 4. Amber/Orange (Filtering/Denoising/Unwrap/DEM)
        if (name.contains("Denoise") || name == "actionClutterSuppression" ||
            name == "actionUnwrap" || name == "actionDEM" || name == "actionSLC_deramp") {
            return isDark ? QColor("#FFB95B") : QColor("#A85C00");
        }
        // 3. Purple (Heavy InSAR/SAR calculations & AI detection)
        if (name == "actionCut" || name == "actionRegistration" || name == "actionOrbitRefinement" || name.contains("Geocoding") ||
            name.contains("merge") || name.contains("Deburst") || name == "actionInterferometric_Formation" ||
            name == "menuSBAS" || name == "actionBaseline_Formation" || name == "actionSBAS_deformation" ||
            name == "actionDeformationRateField" ||
            name == "actionreference_re_selection" || name == "actionDeformation_Preview" ||
            name == "actionBaseline_Preview" || name == "menuSentinel1_Tool" || name == "menuPreprocessing" ||
            name == "menuImageEnhancement" || name == "menuDetection" || name == "actionTargetDetection" ||
            name == "actionBatchTargetRecognition" || name == "menuPSI" || name.contains("PSI_")) {
            return isDark ? QColor("#D0BCFF") : QColor("#6750A4");
        }
        // 6. Cyan/Sky Blue (Atmospheric Correction)
        if (name == "menuAtmosphericCorrection" || name == "actionPhaseElevationRegression" ||
            name == "actionGacosOnlineService" || name == "actionTroposphericCorrection" ||
            name == "actionIonosphericCorrection") {
            return isDark ? QColor("#80E8FF") : QColor("#00687A");
        }
        // 1. Blue (Standard files, project, theme)
        return isDark ? QColor("#82CFFF") : QColor("#005FAC");
    };

    QColor selectedColor(255, 255, 255); // 选中/Hover状态下使用白色，与文字对齐，避免在蓝色背景下隐形
    for (const auto& m : menuIconMap) {
        QColor color = getActionSemanticColor(m.actionName, isDark);
        QAction* action = findChild<QAction*>(m.actionName);
        if (action) {
            action->setIcon(createColoredIcon(m.svgPath, color, selectedColor));
        } else {
            QMenu* menu = findChild<QMenu*>(m.actionName);
            if (menu) {
                menu->setIcon(createColoredIcon(m.svgPath, color, selectedColor));
            }
        }
    }
    
    QColor blueColor = isDark ? QColor("#82CFFF") : QColor("#005FAC");
    QColor purpleColor = isDark ? QColor("#D0BCFF") : QColor("#6750A4");
    
    if (m_recentMenu) m_recentMenu->setIcon(createColoredIcon(":/SatExplorer/svg/recen_open.svg", blueColor, selectedColor));

    // Dynamic View menu actions (工作区界面 / 工作流界面)
    QMenu* viewMenu = ui.menubar->findChild<QMenu*>("View");
    if (viewMenu) {
        for (QAction* action : viewMenu->actions()) {
            if (action->text().contains(QStringLiteral("工作区")))
                action->setIcon(createColoredIcon(":/SatExplorer/svg/project.svg", blueColor, selectedColor));
            else if (action->text().contains(QStringLiteral("工作流")))
                action->setIcon(createColoredIcon(":/SatExplorer/svg/flow_editor.svg", purpleColor, selectedColor));
        }
    }
}

void MainWindow::setTheme(const QString &theme)
{
    // ========================================================================
    // Windows 标题栏主题设置
    // ========================================================================
#ifdef Q_OS_WIN
    HWND hwnd = NULL;
    QWindow* window = windowHandle();
    if (window) {
        hwnd = reinterpret_cast<HWND>(window->winId());
        if (hwnd) {
            BOOL useDarkMode = (theme == "dark") ? TRUE : FALSE;
            DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, 
                                 &useDarkMode, sizeof(useDarkMode));
        }
    }
#endif

    // Apply theme using global function
    applyTheme(theme);

    // Update current theme variable
    m_currentTheme = theme;

    // Save to Config.ini
    QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
    settings.setValue("Appearance/Theme", theme);
    settings.sync();

    // Update theme for all interfaces
    if (m_workspaceUI) {
        m_workspaceUI->setTheme(theme);
    }
    if (m_workflowUI) {
        m_workflowUI->setTheme(theme);
    }
    if (m_welcomeUI) {
        m_welcomeUI->setTheme(theme);
    }

    // Recolor tree view icons for theme
    if (m_workspaceUI && m_workspaceUI->treeView()) {
        m_workspaceUI->treeView()->updateTreeIcons(theme);
    }

    // Recolor menu icons for theme
    applyMenuIcons(theme == "dark");

    // 刷新状态栏中的界面模式徽章样式以适配新主题
    if (m_interfaceManager) {
        updateStatusBarInterface(m_interfaceManager->currentInterfaceId());
    }

    // Update ColorBar theme
    for (ColorBar* colorBar : mColors) {
        if (colorBar) {
            colorBar->setTheme(theme);
        }
    }

    // Update theme menu check state
    QMenu* settingsMenu = ui.Setting;
    QList<QMenu*> submenus = settingsMenu->findChildren<QMenu*>();
    for (QMenu* submenu : submenus) {
        if (submenu->title().contains(QStringLiteral("主题"))) {
            updateThemeCheckState(submenu, theme);
            break;
        }
    }

    // ========================================================================
    // 强制触发窗口重画以更新标题栏（统一用最大化切换）
    // ========================================================================
#ifdef Q_OS_WIN
    if (hwnd) {
        bool wasMaximized = isMaximized();
        if (wasMaximized) {
            showNormal();
            showMaximized();
        } else {
            showMaximized();
            showNormal();
        }
    }
#endif
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    
    // 只在第一次 show 时应用主题
    if (!m_initialThemeApplied) {
        m_initialThemeApplied = true;
        setTheme(m_currentTheme);
    }
}

void MainWindow::setupThemeMenu()
{
    // Find or create Settings menu
    QMenu* settingsMenu = ui.Setting;

    // Create Theme submenu under Settings
    QMenu* themeMenu = settingsMenu->addMenu(QStringLiteral("主题"));
    themeMenu->setObjectName("menuTheme");

    // Create theme actions with checkable property
    QAction* lightAction = themeMenu->addAction(QStringLiteral("浅色主题"));
    lightAction->setCheckable(true);
    lightAction->setData("light");

    QAction* darkAction = themeMenu->addAction(QStringLiteral("深色主题"));
    darkAction->setCheckable(true);
    darkAction->setData("dark");

    QAction* fusionAction = themeMenu->addAction(QStringLiteral("Fusion主题"));
    fusionAction->setCheckable(true);
    fusionAction->setData("fusion");

    // Set initial check state based on current theme
    updateThemeCheckState(themeMenu, m_currentTheme);

    // Connect actions
    connect(lightAction, &QAction::triggered, this, &MainWindow::onThemeLight);
    connect(darkAction, &QAction::triggered, this, &MainWindow::onThemeDark);
    connect(fusionAction, &QAction::triggered, this, &MainWindow::onThemeFusion);
}

void MainWindow::updateThemeCheckState(QMenu* themeMenu, const QString& theme)
{
    QList<QAction*> actions = themeMenu->actions();
    for (QAction* action : actions) {
        if (action->data().toString() == theme) {
            action->setChecked(true);
        } else {
            action->setChecked(false);
        }
    }
}

void MainWindow::initializeInterfaces(QStandardItemModel* model, XMLFile* project, QString filePath)
{
    // Create interface manager
    m_interfaceManager = new InterfaceManager(this);

    // Create welcome screen UI (parent is nullptr - will be managed by InterfaceManager)
    m_welcomeUI = new WelcomeScreenUI(nullptr);
    // Connect welcome screen signals
    connect(m_welcomeUI, &WelcomeScreenUI::newProjectRequested, this, &MainWindow::onNewProjectFromWelcome);
    connect(m_welcomeUI, &WelcomeScreenUI::openProjectRequested, this, &MainWindow::onOpenProjectFromWelcome);
    connect(m_welcomeUI, &WelcomeScreenUI::recentProjectRequested, this, &MainWindow::onRecentProjectFromWelcome);

    // Create workspace UI (traditional interface)
    // Parent is nullptr - will be managed by InterfaceManager
    m_workspaceUI = new WorkspaceUI(nullptr);

    // Note: All signal connections and component initialization happen in the constructor
    // WorkspaceUI manages its own treeView, toolTree, and tabWidget exclusively
    // Create workflow UI (node editor interface)
    // Parent is nullptr - will be managed by InterfaceManager
    m_workflowUI = new WorkflowUI(nullptr);

    // Connect WorkspaceUI toolbar buttons to MainWindow actions
    auto connectWorkspaceBtn = [&](const QString& objName, QAction* action) {
        QToolButton* btn = m_workspaceUI->findChild<QToolButton*>(objName);
        if (btn && action) {
            connect(btn, &QToolButton::clicked, action, &QAction::trigger);
        }
    };
    connectWorkspaceBtn("btnNew", ui.actionNew);
    connectWorkspaceBtn("btnOpen", ui.actionOpen);
    connectWorkspaceBtn("btnSave", ui.actionSave);

    QToolButton* btnWorkflow = m_workspaceUI->findChild<QToolButton*>("btnWorkflow");
    if (btnWorkflow) {
        connect(btnWorkflow, &QToolButton::clicked, this, &MainWindow::switchToWorkflow);
    }

    // Connect WorkflowUI toolbar buttons to MainWindow actions
    auto connectWorkflowBtn = [&](const QString& objName, QAction* action) {
        QToolButton* btn = m_workflowUI->findChild<QToolButton*>(objName);
        if (btn && action) {
            connect(btn, &QToolButton::clicked, action, &QAction::trigger);
        }
    };
    connectWorkflowBtn("btnNew", ui.actionNew);
    connectWorkflowBtn("btnOpen", ui.actionOpen);
    connectWorkflowBtn("btnSave", ui.actionSave);

    QToolButton* btnWorkspace = m_workflowUI->findChild<QToolButton*>("btnWorkspace");
    if (btnWorkspace) {
        connect(btnWorkspace, &QToolButton::clicked, this, &MainWindow::switchToWorkspace);
    }

    // 连接工作流修改信号
    connect(m_workflowUI, &WorkflowUI::workflowModified, this, [this]() {
        m_projectModified = true;
        updateWindowTitle();
    });

    // 连接工作流节点的进度和状态信号到 MainWindow 状态栏。
    // NodeId 是任务身份，caption 只用于展示，避免同名节点或延迟事件改写当前任务状态。
    connect(m_workflowUI, &WorkflowUI::nodeExecutionStarted, this, [this](QtNodes::NodeId nodeId, const QString& caption) {
        m_runningStatusTasks.insert(nodeId, {caption, 0});
        showStatusBarTask(nodeId);
        statusBar()->showMessage(QStringLiteral("正在运行节点: %1...").arg(caption));
    });

    connect(m_workflowUI, &WorkflowUI::nodeProgressUpdated, this, [this](QtNodes::NodeId nodeId, const QString& caption, int percent) {
        auto it = m_runningStatusTasks.find(nodeId);
        if (it == m_runningStatusTasks.end()) {
            return;
        }
        it->caption = caption;
        it->progress = percent;
        showStatusBarTask(nodeId);
        statusBar()->showMessage(QStringLiteral("节点 %1 正在处理: %2%").arg(caption).arg(percent));
    });

    connect(m_workflowUI, &WorkflowUI::nodeExecutionFinished, this, [this](QtNodes::NodeId nodeId, const QString& caption) {
        if (!m_runningStatusTasks.contains(nodeId)) {
            return;
        }
        removeStatusBarTask(nodeId);
        statusBar()->showMessage(QStringLiteral("节点 %1 执行完成").arg(caption), 4000);
    });

    connect(m_workflowUI, &WorkflowUI::nodeExecutionStopped, this, [this](QtNodes::NodeId nodeId, const QString& caption) {
        if (!m_runningStatusTasks.contains(nodeId)) {
            return;
        }
        removeStatusBarTask(nodeId);
        statusBar()->showMessage(QStringLiteral("节点 %1 已停止").arg(caption), 4000);
    });

    connect(m_workflowUI, &WorkflowUI::nodeExecutionError, this, [this](QtNodes::NodeId nodeId, const QString& caption, const QString& error) {
        if (!m_runningStatusTasks.contains(nodeId)) {
            return;
        }
        removeStatusBarTask(nodeId);
        statusBar()->showMessage(QStringLiteral("节点 %1 执行出错: %2").arg(caption).arg(error), 6000);
    });

    connect(m_workflowUI, &WorkflowUI::nodeExecutionTerminalState, this, [this](QtNodes::NodeId nodeId) {
        if (m_runningStatusTasks.contains(nodeId)) {
            removeStatusBarTask(nodeId);
        }
    });

    connect(m_workflowUI, &WorkflowUI::nodeExecutionStartRejected, this, [this](QtNodes::NodeId nodeId, const QString& caption, const QString& reason) {
        Q_UNUSED(nodeId);
        if (m_runningStatusTasks.isEmpty()) {
            statusBar()->showMessage(QStringLiteral("节点 %1 未启动: %2").arg(caption).arg(reason), 6000);
        }
    });

    // Get project name from file path
    QString projectName = filePath;
    if (!filePath.isEmpty()) {
        QFileInfo info(filePath);
        projectName = info.baseName();
    }

    // Set project context and theme for both interfaces
    updateProjectContext(filePath);
    m_workspaceUI->setTheme(m_currentTheme);
    m_workflowUI->setTheme(m_currentTheme);

    // Register interfaces
    m_interfaceManager->registerInterface(m_welcomeUI);     // Register welcome first
    m_interfaceManager->registerInterface(m_workspaceUI);
    m_interfaceManager->registerInterface(m_workflowUI);

    // Switch to default interface from settings (only if project is loaded)
    if (!filePath.isEmpty()) {
        QString defaultInterface = m_interfaceManager->loadDefaultInterface();
        if (defaultInterface.isEmpty()) {
            defaultInterface = "workspace";  // Default to workspace for loaded projects
        }
        m_interfaceManager->switchToInterface(defaultInterface);
        updateInterfaceMenuCheckState();
    }
    // else: caller will switch to welcome screen
}

void MainWindow::setupInterfaceSwitchingMenu()
{
    QMenu* viewMenu = ui.menubar->findChild<QMenu*>("View");
    if (!viewMenu) {
        return;
    }

    // Add separator if menu is not empty
    if (!viewMenu->isEmpty()) {
        viewMenu->addSeparator();
    }

    // Create action group to ensure mutual exclusivity
    QActionGroup* interfaceGroup = new QActionGroup(this);
    interfaceGroup->setExclusive(true);

    // Add workspace action
    QAction* workspaceAction = viewMenu->addAction(QStringLiteral("工作区界面"));
    workspaceAction->setIcon(createColoredIcon(":/SatExplorer/svg/project.svg", themeIconColor(m_currentTheme == "dark"), QColor(255, 255, 255)));
    workspaceAction->setCheckable(true);
    interfaceGroup->addAction(workspaceAction);
    connect(workspaceAction, &QAction::triggered, this, &MainWindow::switchToWorkspace);

    // Add workflow action
    QAction* workflowAction = viewMenu->addAction(QStringLiteral("工作流界面"));
    workflowAction->setIcon(createColoredIcon(":/SatExplorer/svg/flow_editor.svg", themeIconColor(m_currentTheme == "dark"), QColor(255, 255, 255)));
    workflowAction->setCheckable(true);
    interfaceGroup->addAction(workflowAction);
    connect(workflowAction, &QAction::triggered, this, &MainWindow::switchToWorkflow);

    viewMenu->addSeparator();
    if (m_workflowUI) {
        for (QAction* action : m_workflowUI->getViewActions()) {
            viewMenu->addAction(action);
        }
    }
    
    // 添加“显示工具栏”的控制 Action
    viewMenu->addSeparator();
    QAction* showToolBarAction = viewMenu->addAction(QStringLiteral("显示工具栏"));
    showToolBarAction->setObjectName("actionShowToolBar");
    showToolBarAction->setCheckable(true);

    // 从配置文件中读取工具栏显隐设置并应用
    QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
    bool showToolBar = settings.value("Appearance/ShowToolBar", true).toBool();
    showToolBarAction->setChecked(showToolBar);

    connect(showToolBarAction, &QAction::triggered, this, [this](bool checked) {
        for (QToolBar* toolbar : findChildren<QToolBar*>()) {
            toolbar->setVisible(checked);
        }
        QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
        settings.setValue("Appearance/ShowToolBar", checked);
        settings.sync();
    });

    // 添加“显示状态栏”的控制 Action
    QAction* showStatusBarAction = viewMenu->addAction(QStringLiteral("显示状态栏"));
    showStatusBarAction->setObjectName("actionShowStatusBar");
    showStatusBarAction->setCheckable(true);

    // 从配置文件中读取状态栏显隐设置并应用
    bool showStatusBar = settings.value("Appearance/ShowStatusBar", true).toBool();
    showStatusBarAction->setChecked(showStatusBar);
    statusBar()->setVisible(showStatusBar);

    connect(showStatusBarAction, &QAction::triggered, this, [this](bool checked) {
        statusBar()->setVisible(checked);
        QSettings settings(NodeUtils::getConfigPath(), QSettings::IniFormat);
        settings.setValue("Appearance/ShowStatusBar", checked);
        settings.sync();
    });

    updateInterfaceMenuCheckState();
}

void MainWindow::updateInterfaceMenuCheckState()
{
    QMenu* viewMenu = ui.menubar->findChild<QMenu*>("View");
    if (!viewMenu) return;

    QString currentId = m_interfaceManager->currentInterfaceId();
    QList<QAction*> actions = viewMenu->actions();

    for (QAction* action : actions) {
        if (action->text().contains(QStringLiteral("工作区"))) {
            action->setChecked(currentId == "workspace");
        } else if (action->text().contains(QStringLiteral("工作流"))) {
            action->setChecked(currentId == "workflow");
        }
    }

    if (m_workflowUI) {
        bool isWorkflow = (currentId == "workflow");
        for (QAction* action : m_workflowUI->getViewActions()) {
            action->setVisible(isWorkflow);
        }
    }

    updateColorBarVisibility();
    updateStatusBarInterface(currentId);
}

void MainWindow::updateColorBarVisibility()
{
    bool isWorkspace = m_interfaceManager && m_interfaceManager->currentInterfaceId() == "workspace";
    QTabWidget* activeTabWidget = m_workspaceUI ? m_workspaceUI->tabWidget() : nullptr;
    int currentIndex = activeTabWidget ? activeTabWidget->currentIndex() : -1;

    for (int i = 0; i < mColors.size(); ++i)
    {
        if (mColors.at(i))
        {
            if (isWorkspace && i == currentIndex && i < mExist_Color.size() && mExist_Color.at(i))
            {
                if (activeTabWidget && activeTabWidget->currentWidget())
                {
                    mColors.at(i)->resize(activeTabWidget->currentWidget()->width() / 8, activeTabWidget->currentWidget()->height() / 3);
                    mColors.at(i)->move(0, 0);
                    mColors.at(i)->raise();
                    mColors.at(i)->show();
                }
            }
            else
            {
                mColors.at(i)->hide();
            }
        }
    }
}

void MainWindow::switchToWorkspace()
{
    if (m_interfaceManager->switchToInterface("workspace")) {
        updateInterfaceMenuCheckState();
        // Save to project
        if (project) {
            m_interfaceManager->saveLastInterfaceToProject(project);
            m_projectModified = true;
            updateWindowTitle();
        }
    }
}

void MainWindow::switchToWorkflow()
{
    if (m_gcpDockWidget) {
        m_gcpDockWidget->hide();
    }

    if (m_interfaceManager->switchToInterface("workflow")) {
        updateInterfaceMenuCheckState();
        // Save to project
        if (project) {
            m_interfaceManager->saveLastInterfaceToProject(project);
            m_projectModified = true;
            updateWindowTitle();
        }
    }
}

void MainWindow::onNewProjectFromWelcome()
{
    // Create new project (same logic as on_actionNew_triggered)
    on_actionNew_triggered();

    // After new project is created, switch to workspace
    m_interfaceManager->switchToInterface("workspace");
    updateInterfaceMenuCheckState();
}

void MainWindow::onOpenProjectFromWelcome()
{
    // Show file dialog to open project
    QString filePath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("打开项目"),
        QDir::currentPath(),
        QStringLiteral("InSAR Project (*.Insar);;All Files (*)")
    );

    if (!filePath.isEmpty()) {
        open_from_project_file(filePath);
    }
}

void MainWindow::onRecentProjectFromWelcome(const QString &filePath)
{
    if (filePath.isEmpty() || !QFileInfo::exists(filePath)) {
        handleInvalidRecentProject(filePath);
        return;
    }
    open_from_project_file(filePath);
}

void MainWindow::updateProjectContext(const QString& filePath)
{
    m_projectPath = filePath;
    updateStatusBarProject(filePath);
    QString projectName;
    if (!filePath.isEmpty()) {
        QFileInfo info(filePath);
        projectName = info.fileName();
    }

    QStandardItemModel* model = m_interfaceManager ? m_interfaceManager->projectModel() : nullptr;

    if (m_workspaceUI)
        m_workspaceUI->setProjectContext(model, m_projectPath, projectName, this->project);
    if (m_workflowUI)
        m_workflowUI->setProjectContext(model, m_projectPath, projectName, this->project);
    if (m_interfaceManager)
        m_interfaceManager->setProjectContext(model, m_projectPath, projectName, this->project);

    if (!filePath.isEmpty()) {
        QFileInfo info(filePath);
        InSARLogManager::instance().setProjectDirectory(info.absolutePath());
    } else {
        InSARLogManager::instance().setProjectDirectory("");
    }

    updateFileMenuState();
}

void MainWindow::updateFileMenuState()
{
    bool isProjectOpen = !m_projectPath.isEmpty();
    ui.actionNew->setEnabled(true);
    ui.actionOpen->setEnabled(true);
    ui.actionSave->setEnabled(isProjectOpen);
    ui.actionSave_as->setEnabled(isProjectOpen);
    ui.actionClose->setEnabled(isProjectOpen);
    ui.actionQuit->setEnabled(true);

    // 清除孤立文件在未打开工程时置灰，开启时启用
    ui.actionCleanOrphanedFiles->setEnabled(isProjectOpen);

    // 视图菜单在未打开工程时置灰，开启时启用
    ui.View->setEnabled(isProjectOpen);

    // 处理菜单安全加固：若无工程打开，强制置灰（若有工程，则保持由数据刷新逻辑控制）
    if (!isProjectOpen) {
        ui.menuImport_Top->setEnabled(false);
        ui.menuPreprocessing->setEnabled(false);
        ui.menuInSAR->setEnabled(false);
        ui.menuDInSAR->setEnabled(false);
        ui.menuSAR->setEnabled(false);
        ui.menuExport->setEnabled(false);
        if (m_actionGcpManager) m_actionGcpManager->setEnabled(false);
        if (m_menuTools) m_menuTools->menuAction()->setEnabled(false);
        if (m_menuAtmosphericCorrection) m_menuAtmosphericCorrection->menuAction()->setEnabled(false);
        if (m_actionPhaseElevationRegression) m_actionPhaseElevationRegression->setEnabled(false);
        if (m_actionGacosOnlineService) m_actionGacosOnlineService->setEnabled(false);
        if (m_actionTroposphericCorrection) m_actionTroposphericCorrection->setEnabled(false);
        if (m_actionIonosphericCorrection) m_actionIonosphericCorrection->setEnabled(false);
    }
}

void MainWindow::initStatusBar()
{
    QStatusBar* bar = statusBar();
    if (!bar) return;

    // 1. 初始化进度条，限制最大宽度，默认隐藏 (放置在 permanent 区域最左侧，防止显隐时挤压其他控件)
    m_statusProgressBar = new QProgressBar(this);
    m_statusProgressBar->setRange(0, 100);
    m_statusProgressBar->setValue(0);
    m_statusProgressBar->setTextVisible(true);
    m_statusProgressBar->setFixedWidth(130);
    m_statusProgressBar->setFixedHeight(20);
    m_statusProgressBar->setStyleSheet(
        "QProgressBar {"
        "   border: 1px solid #888888;"
        "   border-radius: 3px;"
        "   text-align: center;"
        "   font-size: 9px;"
        "   background-color: rgba(128, 128, 128, 40);"
        "   color: palette(text);"
        "   height: 20px;"
        "   min-height: 20px;"
        "   max-height: 20px;"
        "}"
        "QProgressBar::chunk {"
        "   background-color: #3182ce;"
        "   border-radius: 2px;"
        "}"
    );
    m_statusProgressBar->hide();
    bar->addPermanentWidget(m_statusProgressBar);

    // 2. 初始化模式 Label，作为 Badge 圆角药丸 (排在中间)
    m_statusInterfaceLabel = new QLabel(this);
    m_statusInterfaceLabel->setAlignment(Qt::AlignCenter);
    m_statusInterfaceLabel->setFixedHeight(20);
    m_statusInterfaceLabel->setStyleSheet("font-size: 11px; padding: 0px 8px; border-radius: 4px; font-weight: bold; margin-right: 10px;");
    updateStatusBarInterface("welcome");
    bar->addPermanentWidget(m_statusInterfaceLabel);

    // 3. 初始化当前工程 Label，作为 permanent widget (排在最右侧，紧贴右边缘)
    m_statusProjectLabel = new QLabel(this);
    m_statusProjectLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    m_statusProjectLabel->setStyleSheet("padding-right: 15px; font-size: 12px;");
    updateStatusBarProject(""); // 默认显示未加载
    bar->addPermanentWidget(m_statusProjectLabel);

    // 4. 左侧默认消息
    bar->showMessage(QStringLiteral("就绪"));
}

void MainWindow::showStatusBarTask(QtNodes::NodeId nodeId)
{
    auto it = m_runningStatusTasks.constFind(nodeId);
    if (it == m_runningStatusTasks.constEnd()) {
        return;
    }

    m_activeStatusTaskId = nodeId;
    if (m_statusProgressBar) {
        m_statusProgressBar->setValue(it->progress);
        m_statusProgressBar->show();
    }
}

void MainWindow::removeStatusBarTask(QtNodes::NodeId nodeId)
{
    const bool removedActiveTask = (m_activeStatusTaskId == nodeId);
    m_runningStatusTasks.remove(nodeId);

    if (m_runningStatusTasks.isEmpty()) {
        m_activeStatusTaskId = QtNodes::InvalidNodeId;
        if (m_statusProgressBar) {
            m_statusProgressBar->setValue(0);
            m_statusProgressBar->hide();
        }
        return;
    }

    if (removedActiveTask) {
        showStatusBarTask(m_runningStatusTasks.constBegin().key());
    }
}

void MainWindow::updateStatusBarProject(const QString& filePath)
{
    if (!m_statusProjectLabel) return;

    if (filePath.isEmpty()) {
        m_statusProjectLabel->setText(QStringLiteral("📁 未加载工程"));
        m_statusProjectLabel->setToolTip("");
    } else {
        QFileInfo info(filePath);
        m_statusProjectLabel->setText(QStringLiteral("📁 工程: %1").arg(info.fileName()));
        m_statusProjectLabel->setToolTip(filePath); // 悬停显示完整路径
    }
}

void MainWindow::updateStatusBarInterface(const QString& interfaceId)
{
    if (!m_statusInterfaceLabel) return;

    bool isDark = (m_currentTheme == "dark");
    QString style;

    if (interfaceId == "workspace") {
        m_statusInterfaceLabel->setText(QStringLiteral("工作区模式"));
        if (isDark) {
            style = "color: #5cd699; background-color: #1b4d3e; border: 1px solid #2d7a62; font-size: 11px; padding: 0px 8px; border-radius: 4px; font-weight: bold; margin-right: 10px; height: 20px; min-height: 20px; max-height: 20px;";
        } else {
            style = "color: #1f8b4c; background-color: #e6f7ed; border: 1px solid #c2e0cf; font-size: 11px; padding: 0px 8px; border-radius: 4px; font-weight: bold; margin-right: 10px; height: 20px; min-height: 20px; max-height: 20px;";
        }
    } else if (interfaceId == "workflow") {
        m_statusInterfaceLabel->setText(QStringLiteral("工作流模式"));
        if (isDark) {
            style = "color: #63b3ed; background-color: #1a365d; border: 1px solid #2b4c7e; font-size: 11px; padding: 0px 8px; border-radius: 4px; font-weight: bold; margin-right: 10px; height: 20px; min-height: 20px; max-height: 20px;";
        } else {
            style = "color: #2b6cb0; background-color: #ebf8ff; border: 1px solid #bee3f8; font-size: 11px; padding: 0px 8px; border-radius: 4px; font-weight: bold; margin-right: 10px; height: 20px; min-height: 20px; max-height: 20px;";
        }
    } else {
        m_statusInterfaceLabel->setText(QStringLiteral("欢迎界面"));
        if (isDark) {
            style = "color: #cccccc; background-color: #333333; border: 1px solid #444444; font-size: 11px; padding: 0px 8px; border-radius: 4px; font-weight: bold; margin-right: 10px; height: 20px; min-height: 20px; max-height: 20px;";
        } else {
            style = "color: #4a5568; background-color: #f7fafc; border: 1px solid #e2e8f0; font-size: 11px; padding: 0px 8px; border-radius: 4px; font-weight: bold; margin-right: 10px; height: 20px; min-height: 20px; max-height: 20px;";
        }
    }

    m_statusInterfaceLabel->setStyleSheet(style);
}

void MainWindow::slot_actionGCP_Manager_triggered()
{
    // 如果控制点标注界面当前已经显示，再次触发则将其隐藏以实现 Toggle 效果
    if (m_gcpDockWidget && m_gcpDockWidget->isVisible()) {
        m_gcpDockWidget->hide();
        return;
    }

    // 1. 检查工程是否打开且 Workspace 树是否存在
    if (!m_workspaceUI || !m_workspaceUI->treeView() || !m_workspaceUI->treeView()->model) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请先打开或新建一个 InSAR 工程！"));
        return;
    }
    
    QStandardItemModel* model = m_workspaceUI->treeView()->model;
    if (model->rowCount() == 0) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请先打开或新建一个 InSAR 工程！"));
        return;
    }

    // 新增：如果当前活动 Tab 已经是 H5 影像，则直接在其上开启控制台，无需弹窗选择
    if (m_workspaceUI && m_workspaceUI->isVisible()) {
        QWidget* currentTab = m_workspaceUI->tabWidget()->currentWidget();
        if (currentTab) {
            QString filePath = currentTab->property("filePath").toString();
            if (!filePath.isEmpty() && filePath.endsWith(".h5", Qt::CaseInsensitive)) {
                showGCPDockWidget(filePath);
                return;
            }
        }
    }

    // 2. 递归扫描项目树里的所有影像文件 H5 节点 (属于三级叶子节点)
    // 树层级：Projects (0) -> Data Nodes (1) -> Images (2)
    QMap<QString, QString> imagePathMap; // 影像显示名称 -> H5绝对物理路径
    QMap<QString, QModelIndex> imageIndexMap; // 影像绝对物理路径 -> QModelIndex
    for (int p = 0; p < model->rowCount(); ++p) {
        QStandardItem* projItem = model->item(p, 0);
        if (!projItem) continue;
        for (int d = 0; d < projItem->rowCount(); ++d) {
            QStandardItem* dirItem = projItem->child(d, 0);
            if (!dirItem) continue;
            for (int i = 0; i < dirItem->rowCount(); ++i) {
                QStandardItem* imgItem = dirItem->child(i, 0);
                if (!imgItem) continue;
                // 数据路径存储在 Item 的第二列 (sibling 1) 节点文本中
                QModelIndex pathIdx = imgItem->index().sibling(i, 1);
                QStandardItem* pathItem = model->itemFromIndex(pathIdx);
                if (pathItem) {
                    QString path = pathItem->text();
                    if (path.endsWith(".h5", Qt::CaseInsensitive)) {
                        imagePathMap[imgItem->text()] = path;
                        imageIndexMap[path] = imgItem->index();
                    }
                }
            }
        }
    }

    if (imagePathMap.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("当前工程中没有已导入的影像数据，请先导入或加载影像数据！"));
        return;
    }

    // 获取当前工程文件路径以推导控制点 SQLite 数据库路径
    QStandardItem* firstProjItem = model->item(0, 0);
    QModelIndex firstProjPathIdx = firstProjItem->index().sibling(0, 1);
    QStandardItem* firstProjPathItem = model->itemFromIndex(firstProjPathIdx);
    if (!firstProjPathItem) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("无法获取当前工程物理路径。"));
        return;
    }
    
    QString projXmlPath = firstProjPathItem->text();
    QString projBase = QFileInfo(projXmlPath).baseName();
    QString projDir = QFileInfo(projXmlPath).absolutePath();
    QString dbPath = projDir + "/" + projBase + "_gcp.db";

    // 3. 弹出一个影像选择对话框
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("选择标注底图影像"));
    dlg.setMinimumWidth(360);
    QVBoxLayout* layout = new QVBoxLayout(&dlg);
    
    QLabel* label = new QLabel(QStringLiteral("请选择要进行控制点标注与管理的底图影像:"), &dlg);
    layout->addWidget(label);
    
    QComboBox* combo = new QComboBox(&dlg);
    combo->setMinimumHeight(28);
    for (auto it = imagePathMap.begin(); it != imagePathMap.end(); ++it) {
        combo->addItem(it.key(), it.value());
    }
    layout->addWidget(combo);
    
    // 增加间距
    layout->addSpacing(10);
    
    QHBoxLayout* btnLayout = new QHBoxLayout();
    QPushButton* okBtn = new QPushButton(QStringLiteral("确认"), &dlg);
    QPushButton* cancelBtn = new QPushButton(QStringLiteral("取消"), &dlg);
    okBtn->setDefault(true);
    btnLayout->addWidget(okBtn);
    btnLayout->addWidget(cancelBtn);
    layout->addLayout(btnLayout);
    
    connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    
    if (dlg.exec() == QDialog::Accepted) {
        QString selectedH5 = combo->currentData().toString();
        if (imageIndexMap.contains(selectedH5)) {
            ShowImage(imageIndexMap[selectedH5]);
        }
        showGCPDockWidget(selectedH5);
    }
}

void MainWindow::showGCPDockWidget(const QString& h5Path)
{
    if (!m_gcpDockWidget) {
        m_gcpDockWidget = new GCPAnnotationDockWidget(this);
        addDockWidget(Qt::BottomDockWidgetArea, m_gcpDockWidget);
        
        // 当保存数据时，通知工作区刷新工程树
        connect(m_gcpDockWidget, &GCPAnnotationDockWidget::gcpDataSaved, this, [this]() {
            if (m_workspaceUI) {
                m_workspaceUI->refreshProjectTree();
            }
        });

        // 监听 Tab 切换，自适应重载控制点文件并重新绑定活动视图
        if (m_workspaceUI && m_workspaceUI->tabWidget()) {
            connect(m_workspaceUI->tabWidget(), &QTabWidget::currentChanged, this, [this]() {
                if (m_gcpDockWidget && m_gcpDockWidget->isVisible()) {
                    QWidget* currentTab = m_workspaceUI->tabWidget()->currentWidget();
                    if (currentTab) {
                        QString filePath = currentTab->property("filePath").toString();
                        if (!filePath.isEmpty() && filePath.endsWith(".h5", Qt::CaseInsensitive)) {
                            m_gcpDockWidget->loadDataset(filePath);
                        }
                    }
                }
            });
        }
    }
    m_gcpDockWidget->show();
    m_gcpDockWidget->raise();
    m_gcpDockWidget->loadDataset(h5Path);
}

void MainWindow::slot_actionPhaseElevationRegression_triggered()
{
    PhaseElevationRegression_ui* regis = new PhaseElevationRegression_ui();
    connect(this, &MainWindow::sendModel, regis, &PhaseElevationRegression_ui::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    regis->show();
    connect(regis, &PhaseElevationRegression_ui::sendCopy, m_workspaceUI, &WorkspaceUI::updateProjectModel);
    regis->setAttribute(Qt::WA_DeleteOnClose, true);
}

void MainWindow::slot_actionGacosOnlineService_triggered()
{
    GacosOnlineService_ui* gacos = new GacosOnlineService_ui();
    connect(this, &MainWindow::sendModel, gacos, &GacosOnlineService_ui::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    gacos->show();
    connect(gacos, &GacosOnlineService_ui::sendCopy, m_workspaceUI, &WorkspaceUI::updateProjectModel);
    gacos->setAttribute(Qt::WA_DeleteOnClose, true);
}

void MainWindow::slot_actionTroposphericCorrection_triggered()
{
    TroposphericCorrection_ui* tropo = new TroposphericCorrection_ui();
    connect(this, &MainWindow::sendModel, tropo, &TroposphericCorrection_ui::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    tropo->show();
    connect(tropo, &TroposphericCorrection_ui::sendCopy, m_workspaceUI, &WorkspaceUI::updateProjectModel);
    tropo->setAttribute(Qt::WA_DeleteOnClose, true);
}

void MainWindow::slot_actionIonosphericCorrection_triggered()
{
    IonosphericCorrection_ui* iono = new IonosphericCorrection_ui();
    connect(this, &MainWindow::sendModel, iono, &IonosphericCorrection_ui::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    iono->show();
    connect(iono, &IonosphericCorrection_ui::sendCopy, m_workspaceUI, &WorkspaceUI::updateProjectModel);
    iono->setAttribute(Qt::WA_DeleteOnClose, true);
}

void MainWindow::slot_actionOrbit_Manager_triggered()
{
    OrbitSourceDialog* orbitSourceDlg = new OrbitSourceDialog(this);
    connect(this, &MainWindow::sendModel, orbitSourceDlg, &OrbitSourceDialog::ShowProjectList);
    emit sendModel(m_interfaceManager->projectModel());
    orbitSourceDlg->show();
    orbitSourceDlg->setAttribute(Qt::WA_DeleteOnClose, true);
}

