#if defined(_MSC_VER)
#pragma execution_character_set("utf-8")
#endif

#include<iostream>
// Include headers
#include"Baseline.h"
#include<Deformation_Average.h>
#include "MainWindow.h"
#include "WorkspaceUI.h"
#include "WorkflowUI.h"
#include "WelcomeScreenUI.h"
#include "InterfaceManager.h"
#include "IApplicationInterface.h"

// External function declarations from main.cpp
extern void applyTheme(const QString &theme);
#include"OpenProject.h"
#include"NewProject.h"
#include"Import_TSX.h"
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
#include"SBAS_time_series_analysis.h"
#include"SBAS_reference_reselection.h"
#include<Export_KML.h>
#include"Geocoding.h"
#include"S1_swath_merge.h"
#include"S1_frame_merge.h"
#include"import_CSK.h"
#include"import_ALOS2.h"
#include"icon_source.h"
#include"NodeEditorWindow.h"
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

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    if (!this->Process)
    {
        Process = NULL;
    }
    ui.setupUi(this);
    this->project = new XMLFile;
    this->double_click_open_project_file = "";
    this->b_open_throug_dbclk = false;

    // Set APP icon
    this->setWindowTitle("SatExplorer");
    this->setWindowIcon(QIcon(APP_ICON));
    ui.View->setDisabled(0);
    ui.Process->setDisabled(1);
    ui.menuSAR->setDisabled(1);
    ui.menuInSAR->setDisabled(1);
    ui.menuDInSAR->setDisabled(1);

    ui.treeView->init_tree();
    ui.tool->init_mould();

    ui.tabWidget->setTabsClosable(true);
    connect(ui.treeView, SIGNAL(sendindex(QModelIndex)), this, SLOT(ShowImage(QModelIndex)));

    // Load initial theme from Config.ini
    QSettings settings("Config.ini", QSettings::IniFormat);
    m_currentTheme = settings.value("Appearance/Theme", "light").toString();

    // Setup theme menu (after setting m_currentTheme)
    setupThemeMenu();
    //connect(ui.tool, &TreeView::sendindex, this, &MainWindow::OpenMould);
    connect(ui.treeView, &TreeView::update, this, &MainWindow::update_treeview);
    connect(ui.tabWidget, &QTabWidget::currentChanged, this, &MainWindow::ShowColorBar);
    connect(ui.actionQuit, &QAction::triggered, this, &MainWindow::close);

    // Initialize interfaces for switching
    initializeInterfaces(model, project);
    // Add interface switching menu to View
    setupInterfaceSwitchingMenu();

    // No project opened - show welcome screen
    m_interfaceManager->switchToInterface("welcome");
}
MainWindow::MainWindow(QString str, QWidget* parent) : QMainWindow(parent)
{
    if (!this->Process)
    {
        Process = NULL;
    }
    ui.setupUi(this);
    this->project = new XMLFile;
    this->double_click_open_project_file = "";


    // Set APP icon
    this->setWindowTitle("SatExplorer");
    this->setWindowIcon(QIcon(APP_ICON));
    ui.treeView->init_tree();
    ui.tool->init_mould();

    ui.tabWidget->setTabsClosable(true);
    cout << ui.tabWidget->count();

    // Load initial theme from Config.ini
    QSettings settings("Config.ini", QSettings::IniFormat);
    m_currentTheme = settings.value("Appearance/Theme", "light").toString();

    connect(ui.treeView, SIGNAL(sendindex(QModelIndex)), this, SLOT(ShowImage(QModelIndex)));
    //connect(ui.tool, &TreeView::sendindex, this, &MainWindow::OpenMould);
    connect(ui.treeView, &TreeView::update, this, &MainWindow::update_treeview);
    connect(ui.actionQuit, &QAction::triggered, this, &MainWindow::close);

    // Setup theme menu (after setting m_currentTheme)
    setupThemeMenu();
    connect(ui.treeView, &TreeView::update, this, &MainWindow::update_treeview);
    connect(ui.actionQuit, &QAction::triggered, this, &MainWindow::close);

    // Initialize interfaces for switching
    initializeInterfaces(model, project, str);
    // Add interface switching menu to View
    setupInterfaceSwitchingMenu();

    // Open project file and switch to workspace
    this->open_from_project_file(str);

    // Project opened - switch to workspace or last used interface
    QString lastInterface = m_interfaceManager->loadLastInterfaceFromProject(project);
    if (!lastInterface.isEmpty()) {
        m_interfaceManager->switchToInterface(lastInterface);
    } else {
        m_interfaceManager->switchToInterface("workspace");
    }
    updateInterfaceMenuCheckState();
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

    if (!this->Process)
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
    ui.treeView->NewProject(name, save_path);
    RenewTree(ui.treeView->model);
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
    QGridLayout* TabLayout = new QGridLayout;
    QWidget* TabChild = new QWidget;
    int index = ui.tabWidget->addTab(TabChild, bmp_name);
    ui.tabWidget->setCurrentWidget(TabChild);


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

    QString filename = str;
    QFileInfo fileinfo = QFileInfo(filename);
    QString abs_path = fileinfo.absolutePath();
    model = ui.treeView->model;
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
            model->appendRow(Project);
            model->setItem(model->rowCount() - 1, 1, Project_Path);
            for (p = p->NextSiblingElement(); p != NULL; p = p->NextSiblingElement())
            {
                QStandardItem* Data_Node = new QStandardItem;
                Data_Node->setText(p->Attribute("name"));
                Data_Node->setToolTip(Project->text());
                Data_Node->setIcon(QIcon(FOLDER_ICON));
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
                    Data_Node->setChild(i, 1, Data_Path);
                }

            }
            //model->setHeaderData(0, Qt::Horizontal, tr("workspace"));
            this->RenewTree(model);
        }
        this->project->XMLFile_save(str.toStdString().c_str());
    }
    else
        QMessageBox::warning(NULL, "Warning!", "*.Insar is empty!");
    close();
}
void MainWindow::update_treeview()
{
    if (ui.treeView->model)
    {
        if (ui.treeView->model->rowCount() < 1)
        {
            ui.Process->setDisabled(1);
            ui.menuInSAR->setDisabled(1);
            ui.menuDInSAR->setDisabled(1);
        }
        else
        {
            ui.Process->setDisabled(0);
            ui.menuInSAR->setDisabled(0);
            ui.menuDInSAR->setDisabled(0);
        }
    }
}

bool MainWindow::eventFilter(QObject* target, QEvent* event)
{
    if (target == ui.tabWidget)
    {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Move)
        {
            if (mColors.size())
                mColors.at(ui.tabWidget->currentIndex())->move(ui.tabWidget->currentWidget()->mapToGlobal(QPoint(0, 0)));
        }
    }
    if (target == this)
    {
        if (event->type() == QEvent::Move)
        {
            if (mColors.size())
                mColors.at(ui.tabWidget->currentIndex())->move(ui.tabWidget->currentWidget()->mapToGlobal(QPoint(0, 0)));
        }
    }
    return false;
}

void MainWindow::ShowImage(QModelIndex image)
{

    QString name = ui.treeView->model->index(image.row(), 0, image.parent()).data().toString();
    QString path = ui.treeView->model->index(image.row(), 1, image.parent()).data().toString();
    QString type = ui.treeView->model->itemFromIndex(ui.treeView->model->index(image.row(),0,image.parent()))->toolTip();
    if (!path.isEmpty())
    {
        QFileInfo fileinfo = QFileInfo(path);
        QString bmp = QString("%1/%2.jpg").arg(fileinfo.absolutePath()).arg(fileinfo.baseName());
        QString path_abs = QString("%1%2%3%4").arg(fileinfo.absolutePath()).arg("/").arg(name).arg(".jpg");
        this->bmp_name = name;
        this->bmp_path = path_abs;

        QFileInfo fileinfo1 = QFileInfo(path_abs);
        QDir dir(fileinfo1.absolutePath());
        if (dir.exists(fileinfo1.baseName() + ".jpg"))
        {
            mData_path = path;
            mType = type;
            Loading(mData_path, mType);
        }
        else
        {
            if(!image.child(0,0).isValid() && image.parent().isValid())
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
    NewProject* newpro = new NewProject;
    connect(this, &MainWindow::sendModel, newpro, &NewProject::ReceiveModel);
    emit sendModel(ui.treeView->model);
    newpro->show();
    connect(newpro, &NewProject::sendPath, this, &MainWindow::Addproject);
    newpro->setAttribute(Qt::WA_DeleteOnClose, true);

}
void MainWindow::on_actionOpen_triggered()
{
    OpenProject* open_Window = new OpenProject;
    open_Window->show();
    connect(this, &MainWindow::sendModel, open_Window, &OpenProject::LoadModel);
    emit sendModel(ui.treeView->model);
    connect(open_Window, &OpenProject::sendModel, this, &MainWindow::RenewTree);
    open_Window->setAttribute(Qt::WA_DeleteOnClose, true);
}
void MainWindow::on_actionTSX_triggered()
{

    Import_TSX* TSX_win = new Import_TSX;
    connect(this, &MainWindow::sendModel, TSX_win, &Import_TSX::ShowProjectList);
    emit sendModel(ui.treeView->model);
    TSX_win->show();

    connect(TSX_win, &Import_TSX::sendCopy, this, &MainWindow::RenewTree);
    TSX_win->setAttribute(Qt::WA_DeleteOnClose, true);
}
void MainWindow::on_actionSentinel_1_triggered()
{
    import_sentinel* sentinel_wnd = new import_sentinel;
    connect(this, &MainWindow::sendModel, sentinel_wnd, &import_sentinel::ShowProjectList);
    emit sendModel(ui.treeView->model);
    sentinel_wnd->show();

    connect(sentinel_wnd, &import_sentinel::sendCopy, this, &MainWindow::RenewTree);
    sentinel_wnd->setAttribute(Qt::WA_DeleteOnClose, true);
}
void MainWindow::on_actionCut_triggered()
{
    Cut* cut = new Cut();
    connect(this, &MainWindow::sendModel, cut, &Cut::ShowProjectList);
    emit sendModel(ui.treeView->model);
    cut->show();
    connect(cut, &Cut::sendCopy, this, &MainWindow::RenewTree);
    cut->setAttribute(Qt::WA_DeleteOnClose, true);
}
void MainWindow::on_actionRegistration_triggered()
{
    Registration_ui* regis = new Registration_ui();
    connect(this, &MainWindow::sendModel, regis, &Registration_ui::ShowProjectList);
    emit sendModel(ui.treeView->model);
    regis->show();
    connect(regis, &Registration_ui::sendCopy, this, &MainWindow::RenewTree);
    regis->setAttribute(Qt::WA_DeleteOnClose, true);
}
void MainWindow::on_actionS1_TOPS_BackGeocoding_triggered()
{
    S1_TOPS_BackGeocoding* backGeocoding = new S1_TOPS_BackGeocoding();
    connect(this, &MainWindow::sendModel, backGeocoding, &S1_TOPS_BackGeocoding::ShowProjectList);
    emit sendModel(ui.treeView->model);
    backGeocoding->show();
    connect(backGeocoding, &S1_TOPS_BackGeocoding::sendCopy, this, &MainWindow::RenewTree);
    backGeocoding->setAttribute(Qt::WA_DeleteOnClose, true);
}

void MainWindow::on_actionS1_Deburst_triggered()
{
    S1_Deburst* deburst = new S1_Deburst();
    connect(this, &MainWindow::sendModel, deburst, &S1_Deburst::ShowProjectList);
    emit sendModel(ui.treeView->model);
    deburst->show();
    connect(deburst, &S1_Deburst::sendCopy, this, &MainWindow::RenewTree);
    deburst->setAttribute(Qt::WA_DeleteOnClose, true);
}

void MainWindow::on_actionSBAS_deformation_triggered()
{
    SBAS_time_series_analysis* SBAS_time_series = new SBAS_time_series_analysis();
    connect(this, &MainWindow::sendModel, SBAS_time_series, &SBAS_time_series_analysis::ShowProjectList);
    emit sendModel(ui.treeView->model);
    SBAS_time_series->show();
    connect(SBAS_time_series, &SBAS_time_series_analysis::sendCopy, this, &MainWindow::RenewTree);
    SBAS_time_series->setAttribute(Qt::WA_DeleteOnClose, true);
}

void MainWindow::on_actionDeformation_Preview_triggered()
{
    Deformation_Average* bl = new Deformation_Average();
    connect(this, &MainWindow::sendModel, bl, &Deformation_Average::ShowProjectList);
    emit sendModel(ui.treeView->model);
    bl->show();
    bl->setAttribute(Qt::WA_DeleteOnClose, true);
}

void MainWindow::on_actionreference_re_selection_triggered()
{
    SBAS_reference_reselection* bl = new SBAS_reference_reselection();
    connect(this, &MainWindow::sendModel, bl, &SBAS_reference_reselection::ShowProjectList);
    emit sendModel(ui.treeView->model);
    bl->show();
    bl->setAttribute(Qt::WA_DeleteOnClose, true);
}

void MainWindow::on_actionExport_KML_triggered()
{
    Export_KML* bl = new Export_KML();
    connect(this, &MainWindow::sendModel, bl, &Export_KML::ShowProjectList);
    emit sendModel(ui.treeView->model);
    bl->show();
    bl->setAttribute(Qt::WA_DeleteOnClose, true);
}

void MainWindow::on_actionBaseline_Preview_triggered()
{
    Baseline* bl = new Baseline();
    connect(this, &MainWindow::sendModel, bl, &Baseline::ShowProjectList);
    emit sendModel(ui.treeView->model);
    bl->show();
    bl->setAttribute(Qt::WA_DeleteOnClose, true);
}
void MainWindow::on_actionSLC_deramp_triggered()
{
    SLC_deramp* deramp = new SLC_deramp();
    connect(this, &MainWindow::sendModel, deramp, &SLC_deramp::ShowProjectList);
    emit sendModel(ui.treeView->model);
    deramp->show();
    deramp->setAttribute(Qt::WA_DeleteOnClose, true);
}
void MainWindow::on_actionBaseline_Formation_triggered()
{
    Baseline_Formation* BF = new Baseline_Formation();
    connect(this, &MainWindow::sendModel, BF, &Baseline_Formation::ShowProjectList);
    emit sendModel(ui.treeView->model);
    BF->show();
    BF->setAttribute(Qt::WA_DeleteOnClose, true);
}
void MainWindow::on_actionInterferometric_Formation_triggered()
{
    Interferometric_Formation* IF = new Interferometric_Formation();
    connect(this, &MainWindow::sendModel, IF, &Interferometric_Formation::ShowProjectList);
    emit sendModel(ui.treeView->model);
    IF->show();
    connect(IF, &Interferometric_Formation::sendCopy, this, &MainWindow::RenewTree);
    IF->setAttribute(Qt::WA_DeleteOnClose, true);
}
void MainWindow::on_actionDenoise_triggered()
{
    Filter_ui *Denoise = new Filter_ui();
    connect(this, &MainWindow::sendModel, Denoise, &Filter_ui::ShowProjectList);
    emit sendModel(ui.treeView->model);
    Denoise->show();
    connect(Denoise, &Filter_ui::sendCopy, this, &MainWindow::RenewTree);
    Denoise->setAttribute(Qt::WA_DeleteOnClose, true);
}
void MainWindow::on_actionUnwrap_triggered()
{
    Unwrap_ui* unwrap = new Unwrap_ui();
    connect(this, &MainWindow::sendModel, unwrap, &Unwrap_ui::ShowProjectList);
    emit sendModel(ui.treeView->model);
    unwrap->show();
    connect(unwrap, &Unwrap_ui::sendCopy, this, &MainWindow::RenewTree);
    unwrap->setAttribute(Qt::WA_DeleteOnClose, true);
}
void MainWindow::on_actionDEM_triggered()
{
    Dem_ui* Dem = new Dem_ui();
    connect(this, &MainWindow::sendModel, Dem, &Dem_ui::ShowProjectList);
    emit sendModel(ui.treeView->model);
    Dem->show();
    connect(Dem, &Dem_ui::sendCopy, this, &MainWindow::RenewTree);
    Dem->setAttribute(Qt::WA_DeleteOnClose, true);
}
void MainWindow::on_actiongeocode_triggered()
{
    Geocoding* geocode = new Geocoding();
    connect(this, &MainWindow::sendModel, geocode, &Geocoding::ShowProjectList);
    emit sendModel(ui.treeView->model);
    geocode->show();
    connect(geocode, &Geocoding::sendCopy, this, &MainWindow::RenewTree);
    geocode->setAttribute(Qt::WA_DeleteOnClose, true);
}
void MainWindow::on_actionS1_swath_merge_triggered()
{
    S1_swath_merge* swath_merge = new S1_swath_merge();
    connect(this, &MainWindow::sendModel, swath_merge, &S1_swath_merge::ShowProjectList);
    emit sendModel(ui.treeView->model);
    swath_merge->show();
    connect(swath_merge, &S1_swath_merge::sendCopy, this, &MainWindow::RenewTree);
    swath_merge->setAttribute(Qt::WA_DeleteOnClose, true);
}
void MainWindow::on_actionS1_frame_merge_triggered()
{
    S1_frame_merge* frame_merge = new S1_frame_merge();
    connect(this, &MainWindow::sendModel, frame_merge, &S1_frame_merge::ShowProjectList);
    emit sendModel(ui.treeView->model);
    frame_merge->show();
    connect(frame_merge, &S1_frame_merge::sendCopy, this, &MainWindow::RenewTree);
    frame_merge->setAttribute(Qt::WA_DeleteOnClose, true);
}
void MainWindow::on_actionCOSMOS_SkyMed_triggered()
{
    import_CSK* csk = new import_CSK;
    connect(this, &MainWindow::sendModel, csk, &import_CSK::ShowProjectList);
    emit sendModel(ui.treeView->model);
    csk->show();

    connect(csk, &import_CSK::sendCopy, this, &MainWindow::RenewTree);
    csk->setAttribute(Qt::WA_DeleteOnClose, true);
}
void MainWindow::on_actionALOS_2_triggered()
{
    import_ALOS2* alos2 = new import_ALOS2;
    connect(this, &MainWindow::sendModel, alos2, &import_ALOS2::ShowProjectList);
    emit sendModel(ui.treeView->model);
    alos2->show();

    connect(alos2, &import_ALOS2::sendCopy, this, &MainWindow::RenewTree);
    alos2->setAttribute(Qt::WA_DeleteOnClose, true);
}

void MainWindow::RenewTree(QStandardItemModel* copy)
{

    if (ui.treeView->isHidden())
    {
        ui.treeView->setHidden(0);
        ui.tool->setHidden(0);
        ui.tabWidget->setHidden(0);
    }
    if(!ui.Process->isEnabled())
        ui.Process->setDisabled(0);
    if (!ui.menuInSAR->isEnabled())
        ui.menuInSAR->setDisabled(0);
    if (!ui.menuDInSAR->isEnabled())
        ui.menuDInSAR->setDisabled(0);
    ui.treeView->setColumnHidden(1, 1);
    ui.treeView->setModel(copy);
}
void MainWindow::ShowColorBar(int index)
{
    if ( ui.tabWidget->count()== mExist_Color.size())
    {
        if (ui.tabWidget->count() - 1 >= index)
        {
            cout << ui.tabWidget->count();
            cout << "\n" << "new";
            if (mExist_Color.at(index))
            {

                mColors.at(index)->resize(ui.tabWidget->currentWidget()->width() / 5, ui.tabWidget->currentWidget()->height() / 3);
                mColors.at(index)->move(ui.tabWidget->currentWidget()->mapToGlobal(QPoint(0, 0)));
                mColors.at(index)->raise();
                mColors.at(index)->show();
            }
        }

        if (ColorBar_Before >= 0)
        {
            if (mColors.size())
            {
                cout << ui.tabWidget->count();
                cout << "\n" << "hide";
                mColors.at(ColorBar_Before)->hide();
            }

        }
        cout << ui.tabWidget->count();
        cout << "\n" << "change";

    }
    if (TabCount_Before >= 0 && TabCount_Before< ui.tabWidget->count())
    {
        if (mColors.size())
        {
            cout << ui.tabWidget->count();
            cout << "\n" << "hide";
            mColors.at(ColorBar_Before)->hide();
        }
    }
    TabCount_Before = ui.tabWidget->count();
    ColorBar_Before = index;
}

bool MainWindow::CheckTab(QModelIndex image)
{
    int n = ui.tabWidget->count();
    int i = 0;
    QString name = ui.treeView->model->index(image.row(), 0, image.parent()).data().toString();
    for (i = 0; i < n; i++)
    {
        if (!QString::compare(ui.tabWidget->tabText(i), name))
        {
            ui.tabWidget->setCurrentIndex(i);
            return true;
        }
    }

    return false;
}

void MainWindow::on_tabWidget_tabCloseRequested(int index)
{
    if (ui.tabWidget->widget(index))
    {
        cout << ui.tabWidget->count();
        cout << "\n" << "close";
       // ui.tabWidget->removeTab(index);
        delete(ui.tabWidget->widget(index));
        mColors.removeAt(index);
        mExist_Color.removeAt(index);
        if (ui.tabWidget->currentIndex() >= 0)
        {
            if (mExist_Color.at(ui.tabWidget->currentIndex()))
            {
                mColors.at(ui.tabWidget->currentIndex())->resize(ui.tabWidget->currentWidget()->width() / 5, ui.tabWidget->currentWidget()->height() / 3);
                mColors.at(ui.tabWidget->currentIndex())->move(ui.tabWidget->currentWidget()->mapToGlobal(QPoint(0, 0)));
                mColors.at(ui.tabWidget->currentIndex())->raise();
                mColors.at(ui.tabWidget->currentIndex())->show();
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

void MainWindow::setTheme(const QString &theme)
{
    // Apply theme using global function
    applyTheme(theme);

    // Update current theme variable
    m_currentTheme = theme;

    // Save to Config.ini
    QSettings settings("Config.ini", QSettings::IniFormat);
    settings.setValue("Appearance/Theme", theme);
    settings.sync();

    // Update theme menu check state
    QMenu* settingsMenu = ui.Setteing;
    QList<QMenu*> submenus = settingsMenu->findChildren<QMenu*>();
    for (QMenu* submenu : submenus) {
        if (submenu->title().contains(QString::fromUtf8("主题"))) {
            updateThemeCheckState(submenu, theme);
            break;
        }
    }
}

void MainWindow::setupThemeMenu()
{
    // Find or create Settings menu
    QMenu* settingsMenu = ui.Setteing;

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

    // WorkspaceUI already has the model set up by MainWindow
    // Create workflow UI (node editor interface)
    // Parent is nullptr - will be managed by InterfaceManager
    m_workflowUI = new WorkflowUI(nullptr);

    // Get project name from file path
    QString projectName = filePath;
    if (!filePath.isEmpty()) {
        QFileInfo info(filePath);
        projectName = info.baseName();
    }

    m_workflowUI->setProjectContext(model, filePath, projectName);
    m_workflowUI->setQtNodesTheme(m_currentTheme);

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
    workspaceAction->setIcon(QIcon(":/SatExplorer/icon/project.png"));
    workspaceAction->setCheckable(true);
    interfaceGroup->addAction(workspaceAction);
    connect(workspaceAction, &QAction::triggered, this, &MainWindow::switchToWorkspace);

    // Add workflow action
    QAction* workflowAction = viewMenu->addAction(QString::fromUtf8("工作流界面"));
    workflowAction->setIcon(QIcon(":/SatExplorer/icon/flow_editor.png"));
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
        // Open project file
        open_from_project_file(filePath);

        // Switch to workspace
        m_interfaceManager->switchToInterface("workspace");
        updateInterfaceMenuCheckState();
    }
}

void MainWindow::onRecentProjectFromWelcome(const QString &filePath)
{
    // Open the recent project file
    open_from_project_file(filePath);

    // Switch to workspace
    m_interfaceManager->switchToInterface("workspace");
    updateInterfaceMenuCheckState();
}
