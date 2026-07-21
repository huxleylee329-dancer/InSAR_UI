#pragma once
#include <QtWidgets/QMainWindow>
#include "ui_MainWindow.h"
#include "Unwrap_ui.h"
#include "DEMSourceDialog.h"
#include "OrbitSourceDialog.h"
#include "Dem_ui.h"
#include "PhaseElevationRegression_ui.h"
#include "GacosOnlineService_ui.h"
#include "TroposphericCorrection_ui.h"
#include "IonosphericCorrection_ui.h"
#include "IApplicationInterface.h"
#include "InterfaceManager.h"
#include "tinyxml.h"
#include <ColorBar.h>
#include <QtCore/QHash>
#include "qprogressdialog.h"
#include "QtNodes/internal/Definitions.hpp"

class WorkspaceUI;
class WorkflowUI;
class WelcomeScreenUI;
class XMLFile;
class QLabel;
class QProgressBar;
class GCPAnnotationDockWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(QWidget* parent = Q_NULLPTR);
    MainWindow(QString str, QWidget* parent = Q_NULLPTR);
    ~MainWindow();
    void Addproject(QString,QString);
    void resizeEvent(QResizeEvent* event);
    void closeEvent(QCloseEvent* event) override;

    // Getters for application-wide components
    InterfaceManager* interfaceManager() const { return m_interfaceManager; }
    WorkspaceUI* workspaceUI() const { return m_workspaceUI; }
    GCPAnnotationDockWidget* gcpDockWidget() const { return m_gcpDockWidget; }
    void showGCPDockWidget(const QString& h5Path);

    // Color bar list accessors (single source of truth)
    QList<ColorBar*> colors() const { return mColors; }
    QList<bool> existColors() const { return mExist_Color; }

public slots:
    void ShowImage(QModelIndex);
    void Loading(QString Data_path, QString ImageType, QString bmp_path = "", QString bmp_name = "");
    // Open project when double click project file
    void open_from_project_file(QString str);
    // Welcome screen signal handlers
    void onNewProjectFromWelcome();
    void onOpenProjectFromWelcome();
    void onRecentProjectFromWelcome(const QString &filePath);
protected:
    bool eventFilter(QObject*, QEvent*);
    void showEvent(QShowEvent* event);  // 添加 showEvent 声明
private:
    Ui::MainWindow ui;
    bool m_initialThemeApplied = false;  // 标记是否已应用初始主题
    QLabel* mColorbar_Layer;
    QProgressDialog *Process;
    QString bmp_path;
    QString bmp_name;

    // Data path to display image
    QString mData_path;
    QString mType;
    QList<ColorBar*> mColors;
    QList<bool> mExist_Color;
    int ColorBar_Before = -1;
    int TabCount_Before = -1;

    QTimer *t;
    QMenu* m_recentMenu;      // 最近打开子菜单
    QString m_currentTheme;  // Current theme: light, dark, fusion
    XMLFile* project;
    // Double click project file to open
    QString double_click_open_project_file;
    bool b_open_throug_dbclk = false;

    // Interface manager for workspace/workflow switching
    InterfaceManager* m_interfaceManager;
    WorkspaceUI* m_workspaceUI;
    WorkflowUI* m_workflowUI;
    WelcomeScreenUI* m_welcomeUI;

    // 当前打开的工程文件路径
    QString m_projectPath;

    // 工程是否被修改标记
    bool m_projectModified = false;

    // 保存工作流 JSON 数据（TiXmlText 不复制字符串，需要保持生命周期）
    QByteArray m_workflowBytes;

    void updateProjectContext(const QString& filePath);

signals:
    void sendModel(QStandardItemModel*);
private slots:
    void onLoadImageFinished();
    bool CheckTab(QModelIndex);
    //void OpenMould(QModelIndex);
    void on_actionNew_triggered();
    void on_actionOpen_triggered();
    void on_actionTSX_triggered();
    void on_actionGenericSAR_triggered();
    // Import sentinel dialog
    void on_actionSentinel_1_triggered();
    void on_actionCut_triggered();
    void on_actionOrbitRefinement_triggered();
    void on_actionRegistration_triggered();
    void on_actionS1_TOPS_BackGeocoding_triggered();
    void on_actionS1_Deburst_triggered();
    void on_actionSBAS_deformation_triggered();
    void on_actionDeformationRateField_triggered();
    void on_actionDeformation_Preview_triggered();
    void on_actionreference_re_selection_triggered();
    void on_actionExport_KML_triggered();
    void on_actionPSI_Candidate_triggered();
    void on_actionPSI_Network_triggered();
    void on_actionPSI_TimeSeries_triggered();
    void on_actionBaseline_Preview_triggered();
    void on_actionSLC_deramp_triggered();
    void on_actionBaseline_Formation_triggered();
    void on_actionInterferometric_Formation_triggered();
    void on_actionDenoise_triggered();
    void on_actionUnwrap_triggered();
    void on_actionDEM_triggered();
    void slot_actionPhaseElevationRegression_triggered();
    void slot_actionGacosOnlineService_triggered();
    void slot_actionTroposphericCorrection_triggered();
    void slot_actionIonosphericCorrection_triggered();
    void slot_actionOrbit_Manager_triggered();
    void on_actionExternal_DEM_triggered();
    void on_actiongeocode_triggered();
    void on_actionS1_swath_merge_triggered();
    void on_actionS1_frame_merge_triggered();
    void on_actionCOSMOS_SkyMed_triggered();
    void on_actionALOS_2_triggered();
    void on_actionLuTan_1_triggered();
    void on_actionHongtu_1_triggered();
    void on_actionSpacety_triggered();
    void on_actionAIRSAT_triggered();
    void on_actionBiomass_triggered();
    void on_actionLiDAR_triggered();
    void handleTabCloseRequested(int);
    // Switch ColorBar
    void ShowColorBar(int index);
    // Theme switching
    void onThemeLight();
    void onThemeDark();
    void onThemeFusion();

    void on_actionSpeckleDenoise_triggered();
    void on_actionClutterSuppression_triggered();
    void on_actionBatchTargetRecognition_triggered();
    void on_actionTargetDetection_triggered();

    // Interface switching
    void switchToWorkspace();
    void switchToWorkflow();

    // 保存工程
    void on_actionSave_triggered();
    void on_actionSave_as_triggered();
    // 关闭工程
    void on_actionClose_triggered();
    void on_actionCleanOrphanedFiles_triggered();

public slots:
    void slot_actionGCP_Manager_triggered();
private:
    void setupThemeMenu();
    void setTheme(const QString &theme);
    void applyMenuIcons(bool isDark);
    void updateThemeCheckState(QMenu* themeMenu, const QString& theme);
    void setupInterfaceSwitchingMenu();
    void updateInterfaceMenuCheckState();
    void updateColorBarVisibility();
    void initializeInterfaces(QStandardItemModel* model, XMLFile* project, QString filePath = QString());

    // 关闭当前工程（不含确认对话框），供新建/打开工程前调用
    void closeCurrentProject();
    bool maybeSave();

    // 工作流状态保存/加载
    void saveWorkflowToProject(const QString& projectFilePath);
    void loadWorkflowFromProject(const QString& projectFilePath);

    // 最近打开项目管理
    void addToRecentProjects(const QString& path);
    void updateRecentMenu();
    void openRecentProject();
    void handleInvalidRecentProject(const QString& filePath);

    // 更新窗口标题（显示工程修改状态）
    void updateWindowTitle();

    // 状态栏控件
    QLabel* m_statusProjectLabel;
    QLabel* m_statusInterfaceLabel;
    QProgressBar* m_statusProgressBar;
    struct StatusBarTask
    {
        QString caption;
        int progress = 0;
    };
    QHash<QtNodes::NodeId, StatusBarTask> m_runningStatusTasks;
    QtNodes::NodeId m_activeStatusTaskId;

    void initStatusBar();
    void updateStatusBarProject(const QString& filePath);
    void updateStatusBarInterface(const QString& interfaceId);
    void showStatusBarTask(QtNodes::NodeId nodeId);
    void removeStatusBarTask(QtNodes::NodeId nodeId);

    // 更新“文件”菜单项状态
    void updateFileMenuState();

    QAction* m_actionGcpManager;
    QAction* m_actionOrbitManager;
    QAction* m_actionPhaseElevationRegression;
    QAction* m_actionGacosOnlineService;
    QAction* m_actionTroposphericCorrection;
    QAction* m_actionIonosphericCorrection;
    QMenu* m_menuTools;
    QMenu* m_menuAtmosphericCorrection;
    GCPAnnotationDockWidget* m_gcpDockWidget = nullptr;
};
