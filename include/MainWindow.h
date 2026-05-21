#pragma once
#include <QtWidgets/QMainWindow>
#include "ui_MainWindow.h"
#include<ColorBar.h>
#include<qgraphicsscene.h>
#include"qprogressdialog.h"
#include"MyThread.h"
#include "IApplicationInterface.h"
#include "InterfaceManager.h"

class WorkspaceUI;
class WorkflowUI;
class WelcomeScreenUI;
class XMLFile;

class MainWindow : public QMainWindow
{
    Q_OBJECT
    MyThread* thread;
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

public slots:
    void ShowImage(QModelIndex);
    void updateProcess(int, QString);
    void endProcess();
    void endThread();
    void StopThread();
    void Loading(QString Data_path, QString ImageType);
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
    void operate(QString, QString, QString);
private slots:
    bool CheckTab(QModelIndex);
    //void OpenMould(QModelIndex);
    void on_actionNew_triggered();
    void on_actionOpen_triggered();
    void on_actionTSX_triggered();
    void on_actionGenericSAR_triggered();
    // Import sentinel dialog
    void on_actionSentinel_1_triggered();
    void on_actionCut_triggered();
    void on_actionRegistration_triggered();
    void on_actionS1_TOPS_BackGeocoding_triggered();
    void on_actionS1_Deburst_triggered();
    void on_actionSBAS_deformation_triggered();
    void on_actionDeformation_Preview_triggered();
    void on_actionreference_re_selection_triggered();
    void on_actionExport_KML_triggered();
    void on_actionBaseline_Preview_triggered();
    void on_actionSLC_deramp_triggered();
    void on_actionBaseline_Formation_triggered();
    void on_actionInterferometric_Formation_triggered();
    void on_actionDenoise_triggered();
    void on_actionUnwrap_triggered();
    void on_actionDEM_triggered();
    void on_actiongeocode_triggered();
    void on_actionS1_swath_merge_triggered();
    void on_actionS1_frame_merge_triggered();
    void on_actionCOSMOS_SkyMed_triggered();
    void on_actionALOS_2_triggered();
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
    // 关闭工程
    void on_actionClose_triggered();
private:
    void setupThemeMenu();
    void setTheme(const QString &theme);
    void applyMenuIcons(bool isDark);
    void updateThemeCheckState(QMenu* themeMenu, const QString& theme);
    void setupInterfaceSwitchingMenu();
    void updateInterfaceMenuCheckState();
    void initializeInterfaces(QStandardItemModel* model, XMLFile* project, QString filePath = QString());

    // 关闭当前工程（不含确认对话框），供新建/打开工程前调用
    void closeCurrentProject();

    // 工作流状态保存/加载
    void saveWorkflowToProject(const QString& projectFilePath);
    void loadWorkflowFromProject(const QString& projectFilePath);

    // 最近打开项目管理
    void addToRecentProjects(const QString& path);
    void updateRecentMenu();
    void openRecentProject();

    // 更新窗口标题（显示工程修改状态）
    void updateWindowTitle();
};
