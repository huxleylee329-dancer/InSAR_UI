#if defined(_MSC_VER)
#pragma execution_character_set("utf-8")
#endif

#include<iostream>
#include <exception>
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
#include"MyThread.h"
#include"Unwrap_ui.h"
#include"Dem_ui.h"
#include"SLC_deramp.h"
#include"Baseline_Formation.h"
#include <QJsonDocument>
#include <QJsonObject>
#include "tinyxml.h"
#include"SBAS_time_series_analysis.h"
#include"SBAS_reference_reselection.h"
#include<Export_KML.h>
#include"Geocoding.h"
#include"S1_swath_merge.h"
#include"S1_frame_merge.h"
#include"import_CSK.h"
#include"import_ALOS2.h"
#include"icon_source.h"
#include"SpeckleDenoise.h"
#include"ClutterSuppression.h"
#include"BatchTargetRecognition.h"
#include"TargetDetection.h"
//#include<Mould.h>

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
//#include<FormatConversion.h>
//#include<Utils.h>
// opencv related headers
#include<opencv2/highgui.hpp>

//#ifdef _DEBUG
//#pragma comment(lib, "Utils_d.lib")
//#pragma comment(lib, "FormatConversion_d.lib")
//#pragma comment(lib, "ComplexMat_d.lib")
//#endif
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
    {"actionSave_all",                 ":/SatExplorer/svg/saveall.svg"},
    {"actionClose",                    ":/SatExplorer/svg/close.svg"},
    {"actionQuit",                     ":/SatExplorer/svg/quit.svg"},
    {"actionRegistration",             ":/SatExplorer/svg/coregistration.svg"},
    {"actionCut",                      ":/SatExplorer/svg/cut.svg"},
    {"actionS1_TOPS_BackGeocoding",    ":/SatExplorer/svg/coregistration.svg"},
    {"actionS1_Deburst",               ":/SatExplorer/svg/splice.svg"},
    {"actionSLC_deramp",               ":/SatExplorer/svg/dem.svg"},
    {"actionBaseline_Formation",       ":/SatExplorer/svg/baseline_formation.svg"},
    {"actionSBAS_deformation",         ":/SatExplorer/svg/time_series.svg"},
    {"actionDeformation_Preview",      ":/SatExplorer/svg/view.svg"},
    {"actionreference_re_selection",   ":/SatExplorer/svg/reference.svg"},
    {"actionExport_KML",               ":/SatExplorer/svg/GoogleEarth.svg"},
    {"actiongeocode",                  ":/SatExplorer/svg/geocoding.svg"},
    {"actionS1_frame_merge",           ":/SatExplorer/svg/frame_merge.svg"},
    {"actionS1_swath_merge",           ":/SatExplorer/svg/swath_merge.svg"},
    {"actionNodeEditor",               ":/SatExplorer/svg/flow_editor.svg"},
    // QMenu icons
    {"menuImport",                     ":/SatExplorer/svg/import.svg"},
    {"menuSBAS",                       ":/SatExplorer/svg/baseline_formation.svg"},
};

MainWindow::MainWindow(QWidget* parent)
    : MainWindow(QString(), parent)
{
}

MainWindow::MainWindow(QString str, QWidget* parent)
    : QMainWindow(parent)
    , thread(nullptr)
    , Process(nullptr)
    , project(nullptr)
    , m_interfaceManager(nullptr)
    , m_workspaceUI(nullptr)
    , m_workflowUI(nullptr)
    , m_welcomeUI(nullptr)
{
    ui.setupUi(this);
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
    QSettings settings("Config.ini", QSettings::IniFormat);
    m_currentTheme = settings.value("Appearance/Theme", "light").toString();

    // 创建最近打开子菜单并插入文件菜单
    m_recentMenu = new QMenu("最近打开", this);
    ui.File->insertMenu(ui.actionSave, m_recentMenu);

    // Apply menu icons after m_recentMenu is created
    applyMenuIcons(m_currentTheme == "dark");
    updateRecentMenu();

    // Setup theme menu (after setting m_currentTheme)
    setupThemeMenu();
    
    // Connect signals/slots
    connect(m_workspaceUI->treeView(), SIGNAL(sendindex(QModelIndex)), this, SLOT(ShowImage(QModelIndex)));
    connect(m_workspaceUI->treeView(), &TreeView::update, m_workspaceUI, &WorkspaceUI::refreshProjectTree);
    connect(m_workspaceUI->tabWidget(), &QTabWidget::currentChanged, this, &MainWindow::ShowColorBar);
    connect(m_workspaceUI->tabWidget(), &QTabWidget::tabCloseRequested, this, &MainWindow::on_tabWidget_tabCloseRequested);
    connect(m_workspaceUI, &WorkspaceUI::projectTreeRefreshed, this, [this]() {
        if (!m_projectPath.isEmpty() && this->project) {
            this->project->XMLFile_load(m_projectPath.toStdString().c_str());
        }

        // Enable menus if project has data (Replicates legacy RenewTree logic)
        QStandardItemModel* currentModel = m_interfaceManager->projectModel();
        if (currentModel && currentModel->rowCount() > 0)
        {
            if (!ui.Process->isEnabled()) ui.Process->setDisabled(0);
            if (!ui.menuSAR->isEnabled()) ui.menuSAR->setDisabled(0);
            if (!ui.menuInSAR->isEnabled()) ui.menuInSAR->setDisabled(0);
            if (!ui.menuDInSAR->isEnabled()) ui.menuDInSAR->setDisabled(0);
        }

        m_projectModified = true;
        updateWindowTitle();
    });
    connect(ui.actionQuit, &QAction::triggered, this, &MainWindow::close);

    // Add interface switching menu to View
    setupInterfaceSwitchingMenu();

    if (str.isEmpty()) {
        // No project opened - show workflow interface for debugging
        ui.View->setDisabled(0);
        ui.Process->setDisabled(1);
        ui.menuSAR->setDisabled(1);
        ui.menuInSAR->setDisabled(1);
        ui.menuDInSAR->setDisabled(1);
        
        m_interfaceManager->switchToInterface("workflow");
        updateInterfaceMenuCheckState();
    }
    else {
        // Open project file (loadWorkflowFromProject inside handles interface switching)
        this->open_from_project_file(str);
    }
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
    this->thread = NULL;
    if (this->project)
    {
        delete this->project;
        this->project = NULL;
    }
}
//void MainWindow::OpenMould(QModelIndex index)
//{
//    if (ui.tool->model->itemFromIndex(index)->toolTip() == "DEM")
//    {
//        Mould* mould = new Mould;
//        connect(this, &MainWindow::sendModel, mould, &Mould::ReceiveModel);
//        emit sendModel(ui.treeView->model);
//        mould->show();
//        connect(mould, &Mould::sendCopy, this, &MainWindow::RenewTree);
//        mould->setAttribute(Qt::WA_DeleteOnClose, true);
//    }
//}
void MainWindow::Addproject(QString name, QString save_path)
{
    m_workspaceUI->treeView()->NewProject(name, save_path);
    m_workspaceUI->updateProjectModel(m_workspaceUI->treeView()->model);

    // 设置工程路径并加载 XML，使后续保存能正常工作
    QString projectFile = save_path + "/" + name + ".insar";
    updateProjectContext(projectFile);
    this->project->XMLFile_load(projectFile.toStdString().c_str());

    // 新建工程后，重置修改标记（因为刚保存过）
    m_projectModified = false;
    updateWindowTitle();
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
void MainWindow::updateProcess(int value, QString information)
{
    this->Process->setValue(value);
    this->Process->setLabelText(information);
    QThread::currentThread()->msleep(1);

}
void MainWindow::endProcess()
{
        Process->setValue(100);
        waitKey(100);
        if (!this->Process)
        {
            delete(Process);
            Process = NULL;
        }
        Loading(mData_path, mType);
        thread->thread()->quit();
        thread->thread()->wait();
}
void MainWindow::endThread()
{
    thread->thread()->quit();
    thread->thread()->wait();
    //thread->thread()->deleteLater();
}
void MainWindow::StopThread()
{
    //qDebug() << "Close thread id: " << thread->thread()->isFinished();
   // qDebug() << "Status: " << thread->thread()->isInterruptionRequested();
    if(this->thread != NULL)
        if (this->thread->thread()->isRunning())
    {
        thread->thread()->requestInterruption();
        thread->thread()->quit();
        thread->thread()->wait();
    }

}
void MainWindow::Loading(QString Data_path, QString ImageType)
{
    // 界面切换到 Workflow 后直接返回
    if (m_interfaceManager && m_interfaceManager->currentInterfaceId() != "workspace")
        return;

    QGridLayout* TabLayout = new QGridLayout;
    QWidget* TabChild = new QWidget;
    QTabWidget* activeTabWidget = m_workspaceUI->tabWidget();
    int index = activeTabWidget->addTab(TabChild, bmp_name);
    activeTabWidget->setCurrentWidget(TabChild);


    ImageView* graph = new ImageView(TabChild);
    TabLayout->addWidget(graph);
    TabLayout->setContentsMargins(0, 0, 0, 0);
    QGraphicsScene* scene = new QGraphicsScene;
    graph->setScene(scene);
    graph->setInteractive(true);
    graph->setDragMode(QGraphicsView::RubberBandDrag);
    graph->setRubberBandSelectionMode(Qt::ContainsItemShape);
    QImage Qimg = QImage(this->bmp_path);
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
        QPoint globalpos = TabChild->mapToGlobal(QPoint(0, 0));
        Color_Label->move(globalpos.x(), globalpos.y());
        Color_Label->show();
        mExist_Color.append(true);
    }
    else
    {
        mExist_Color.append(false);
    }
    mColors.append(Color_Label);
}
void MainWindow::open_from_project_file(QString str)
{
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
                String x(q->GetText());
                Project->setText(x.c_str());
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
            loadWorkflowFromProject(str);
        }
        this->project->XMLFile_save(str.toStdString().c_str());
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
                mColors.at(activeTabWidget->currentIndex())->move(activeTabWidget->currentWidget()->mapToGlobal(QPoint(0, 0)));
        }
    }
    if (isWorkspace && target == this)
    {
        if (event->type() == QEvent::Move)
        {
            if (mColors.size() && activeTabWidget->currentIndex() >= 0)
                mColors.at(activeTabWidget->currentIndex())->move(activeTabWidget->currentWidget()->mapToGlobal(QPoint(0, 0)));
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
        if (dir.exists(fileinfo1.baseName() + ".jpg"))
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
                // Generic SAR等直接导入的普通图片，跳过HDF5读取，直接加载
                this->bmp_path = path;
                mData_path = path;
                mType = type;
                Loading(mData_path, mType);
            }
            else
            {
                mData_path = path;
                mType = type;
                thread = new MyThread;
                thread->moveToThread(new QThread(this));
                this->Process = new QProgressDialog("Loading Image...", "Cancel", 0, 100);
                Process->setFixedSize(450, 100);
                Process->setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
                Process->setWindowTitle(QString::fromLocal8Bit("Loading Result"));
                Process->setCancelButton(false);
                //this->Process->setAutoClose(true);
                this->Process->setValue(0);
                this->Process->show();
                waitKey(100);
                connect(this, &MainWindow::operate, thread, &MyThread::ShowImage);
                connect(thread, &MyThread::updateProcess, this, &MainWindow::updateProcess);
                connect(thread->thread(), &QThread::finished, thread, &MyThread::deleteLater);
                connect(thread, &MyThread::endProcess, this, &MainWindow::endProcess);
                connect(this->Process, &QProgressDialog::destroyed, this, &MainWindow::StopThread);
                connect(this->Process, &QProgressDialog::canceled, this, &MainWindow::StopThread);// , Qt::QueuedConnection);
                thread->thread()->start();
                emit operate(path, path_abs, type);
            }
        }
    }
}
void MainWindow::on_actionNew_triggered()
{
    // 关闭当前工程（不保存），避免两个工程状态共存
    closeCurrentProject();

    NewProject* newpro = new NewProject;
    connect(this, &MainWindow::sendModel, newpro, &NewProject::ReceiveModel);
    emit sendModel(m_interfaceManager->projectModel());
    newpro->show();
    connect(newpro, &NewProject::sendPath, this, &MainWindow::Addproject);
    newpro->setAttribute(Qt::WA_DeleteOnClose, true);

}
void MainWindow::on_actionOpen_triggered()
{
    // 关闭当前工程（不保存），避免两个工程状态共存
    closeCurrentProject();

    OpenProject* open_Window = new OpenProject;
    open_Window->show();
    connect(this, &MainWindow::sendModel, open_Window, &OpenProject::LoadModel);
    emit sendModel(m_interfaceManager->projectModel());
    connect(open_Window, &OpenProject::sendModel, m_workspaceUI, &WorkspaceUI::updateProjectModel);
    connect(open_Window, &OpenProject::projectOpened, this, &MainWindow::loadWorkflowFromProject);
    open_Window->setAttribute(Qt::WA_DeleteOnClose, true);
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
    this->project->XMLFile_save(m_projectPath.toStdString().c_str());
    m_projectModified = false;
    updateWindowTitle();
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
    ui.Process->setDisabled(1);
    ui.menuSAR->setDisabled(1);
    ui.menuInSAR->setDisabled(1);
    ui.menuDInSAR->setDisabled(1);

    // 清空标签页
    if (m_workspaceUI && m_workspaceUI->tabWidget()) {
        QTabWidget* closeTabWidget = m_workspaceUI->tabWidget();
        while (closeTabWidget->count() > 0)
            closeTabWidget->removeTab(0);
    }

    // 重置工程修改标记
    m_projectModified = false;
    updateWindowTitle();
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
            this->project->XMLFile_save(m_projectPath.toStdString().c_str());
        } else if (reply == QMessageBox::Cancel) {
            return;
        }
    }

    closeCurrentProject();

    // 切换到流程编辑器界面
    if (m_interfaceManager) {
        m_interfaceManager->switchToInterface("workflow");
        updateInterfaceMenuCheckState();
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
void MainWindow::loadWorkflowFromProject(const QString& projectFilePath)
{
    if (!m_workflowUI || projectFilePath.isEmpty())
        return;

    try {
        // 记录当前工程路径
        updateProjectContext(projectFilePath);

        // 加载项目 XML 到 this->project，确保后续保存不会写空文件
        if (this->project)
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
        QMessageBox::critical(this, "错误", QString("加载工作流失败：") + QString::fromLocal8Bit(e.what()));
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

    QSettings settings("Config.ini", QSettings::IniFormat);
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

    QSettings settings("Config.ini", QSettings::IniFormat);
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
        QSettings settings("Config.ini", QSettings::IniFormat);
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
        QMessageBox::warning(this, "提示", "项目文件不存在：" + filePath);
        return;
    }

    try {
        open_from_project_file(filePath);
    }
    catch (const std::exception& e) {
        QMessageBox::critical(this, "错误", QString("加载项目失败：") + QString::fromLocal8Bit(e.what()));
    }
    catch (...) {
        QMessageBox::critical(this, "错误", "加载项目时发生未知异常。");
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

void MainWindow::ShowColorBar(int index)
{
    // 界面切换到 Workflow 后直接返回
    if (m_interfaceManager && m_interfaceManager->currentInterfaceId() != "workspace")
        return;

    QTabWidget* activeTabWidget = m_workspaceUI->tabWidget();

    if ( activeTabWidget->count()== mExist_Color.size())
    {
        if (activeTabWidget->count() - 1 >= index)
        {
            cout << activeTabWidget->count();
            cout << "\n" << "new";
            if (mExist_Color.at(index))
            {

                mColors.at(index)->resize(activeTabWidget->currentWidget()->width() / 5, activeTabWidget->currentWidget()->height() / 3);
                mColors.at(index)->move(activeTabWidget->currentWidget()->mapToGlobal(QPoint(0, 0)));
                mColors.at(index)->raise();
                mColors.at(index)->show();
            }
        }

        if (ColorBar_Before >= 0)
        {
            if (mColors.size())
            {
                cout << activeTabWidget->count();
                cout << "\n" << "hide";
                mColors.at(ColorBar_Before)->hide();
            }

        }
        cout << activeTabWidget->count();
        cout << "\n" << "change";

    }
    if (TabCount_Before >= 0 && TabCount_Before< activeTabWidget->count())
    {
        if (mColors.size())
        {
            cout << activeTabWidget->count();
            cout << "\n" << "hide";
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

void MainWindow::on_tabWidget_tabCloseRequested(int index)
{
    // 界面切换到 Workflow 后直接返回
    if (m_interfaceManager && m_interfaceManager->currentInterfaceId() != "workspace")
        return;

    QTabWidget* activeTabWidget = m_workspaceUI->tabWidget();

    if (activeTabWidget->widget(index))
    {
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
                mColors.at(activeTabWidget->currentIndex())->resize(activeTabWidget->currentWidget()->width() / 5, activeTabWidget->currentWidget()->height() / 3);
                mColors.at(activeTabWidget->currentIndex())->move(activeTabWidget->currentWidget()->mapToGlobal(QPoint(0, 0)));
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
    QColor color = themeIconColor(isDark);
    for (const auto& m : menuIconMap) {
        QAction* action = findChild<QAction*>(m.actionName);
        if (action) action->setIcon(createColoredIcon(m.svgPath, color));
    }
    if (m_recentMenu) m_recentMenu->setIcon(createColoredIcon(":/SatExplorer/svg/recen_open.svg", color));

    // Dynamic View menu actions (工作区界面 / 工作流界面)
    QMenu* viewMenu = ui.menubar->findChild<QMenu*>("View");
    if (viewMenu) {
        for (QAction* action : viewMenu->actions()) {
            if (action->text().contains(QString::fromUtf8("工作区")))
                action->setIcon(createColoredIcon(":/SatExplorer/svg/project.svg", color));
            else if (action->text().contains(QString::fromUtf8("工作流")))
                action->setIcon(createColoredIcon(":/SatExplorer/svg/flow_editor.svg", color));
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
    QSettings settings("Config.ini", QSettings::IniFormat);
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
        if (submenu->title().contains(QString::fromUtf8("主题"))) {
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
    QMenu* themeMenu = settingsMenu->addMenu(QString::fromUtf8("主题"));

    // Create theme actions with checkable property
    QAction* lightAction = themeMenu->addAction(QString::fromUtf8("浅色主题"));
    lightAction->setCheckable(true);
    lightAction->setData("light");

    QAction* darkAction = themeMenu->addAction(QString::fromUtf8("深色主题"));
    darkAction->setCheckable(true);
    darkAction->setData("dark");

    QAction* fusionAction = themeMenu->addAction(QString::fromUtf8("Fusion主题"));
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

    // 连接工作流修改信号
    connect(m_workflowUI, &WorkflowUI::workflowModified, this, [this]() {
        m_projectModified = true;
        updateWindowTitle();
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
    QAction* workspaceAction = viewMenu->addAction(QString::fromUtf8("工作区界面"));
    workspaceAction->setIcon(createColoredIcon(":/SatExplorer/svg/project.svg", themeIconColor(m_currentTheme == "dark")));
    workspaceAction->setCheckable(true);
    interfaceGroup->addAction(workspaceAction);
    connect(workspaceAction, &QAction::triggered, this, &MainWindow::switchToWorkspace);

    // Add workflow action
    QAction* workflowAction = viewMenu->addAction(QString::fromUtf8("工作流界面"));
    workflowAction->setIcon(createColoredIcon(":/SatExplorer/svg/flow_editor.svg", themeIconColor(m_currentTheme == "dark")));
    workflowAction->setCheckable(true);
    interfaceGroup->addAction(workflowAction);
    connect(workflowAction, &QAction::triggered, this, &MainWindow::switchToWorkflow);

    updateInterfaceMenuCheckState();
}

void MainWindow::updateInterfaceMenuCheckState()
{
    QMenu* viewMenu = ui.menubar->findChild<QMenu*>("View");
    if (!viewMenu) return;

    QString currentId = m_interfaceManager->currentInterfaceId();
    QList<QAction*> actions = viewMenu->actions();

    for (QAction* action : actions) {
        if (action->text().contains(QString::fromUtf8("工作区"))) {
            action->setChecked(currentId == "workspace");
        } else if (action->text().contains(QString::fromUtf8("工作流"))) {
            action->setChecked(currentId == "workflow");
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
        }
    }
}

void MainWindow::switchToWorkflow()
{
    if (m_interfaceManager->switchToInterface("workflow")) {
        updateInterfaceMenuCheckState();
        // Save to project
        if (project) {
            m_interfaceManager->saveLastInterfaceToProject(project);
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
        QString::fromUtf8("打开项目"),
        QDir::currentPath(),
        QString::fromUtf8("InSAR Project (*.Insar);;All Files (*)")
    );

    if (!filePath.isEmpty()) {
        open_from_project_file(filePath);
    }
}

void MainWindow::onRecentProjectFromWelcome(const QString &filePath)
{
    open_from_project_file(filePath);
}

void MainWindow::updateProjectContext(const QString& filePath)
{
    m_projectPath = filePath;
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
}
