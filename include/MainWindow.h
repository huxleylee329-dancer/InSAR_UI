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
public slots:
    void ShowImage(QModelIndex);
    void updateProcess(int, QString);
    void endProcess();
    void endThread();
    void StopThread();
    void Loading(QString Data_path, QString ImageType);
    // Open project when double click project file
    void open_from_project_file(QString str);
    // Update treeview
    void update_treeview();
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
    QString m_currentTheme;  // Current theme: light, dark, fusion
    XMLFile* project;
    QStandardItemModel* model;
    // Double click project file to open
    QString double_click_open_project_file;
    bool b_open_throug_dbclk = false;

    // Interface manager for workspace/workflow switching
    InterfaceManager* m_interfaceManager;
    WorkspaceUI* m_workspaceUI;
    WorkflowUI* m_workflowUI;
    WelcomeScreenUI* m_welcomeUI;

signals:
    void sendModel(QStandardItemModel*);
    void operate(QString, QString, QString);
private slots:
    bool CheckTab(QModelIndex);
    //void OpenMould(QModelIndex);
    void on_actionNew_triggered();
    void on_actionOpen_triggered();
    void on_actionTSX_triggered();
    void on_actionMacao_triggered();
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
    void on_tabWidget_tabCloseRequested(int);
    void RenewTree(QStandardItemModel*);
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
private:
    void setupThemeMenu();
    void setTheme(const QString &theme);
    void updateThemeCheckState(QMenu* themeMenu, const QString& theme);
    void setupInterfaceSwitchingMenu();
    void updateInterfaceMenuCheckState();
    void initializeInterfaces(QStandardItemModel* model, XMLFile* project, QString filePath = QString());
};
