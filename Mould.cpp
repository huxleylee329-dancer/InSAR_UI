#include<Mould.h>
#include<qdialog.h>
#include<qcheckbox.h>
#include<qscrollarea.h>
#include<Utils.h>
#include<FormatConversion.h>
#include<qmessagebox.h>
#include<QLineEdit>
#include<QPushButton>
#include<QDialogButtonBox>
#include<QFileDialog>
#include<QDir>
#include<ctime>
#include <atlbase.h>
#include <atlconv.h>
#include"icon_source.h"
#ifdef _DEBUG
#pragma comment(lib, "Utils_d.lib")
#pragma comment(lib, "FormatConversion_d.lib")
#else
#pragma comment(lib, "Utils.lib")
#pragma comment(lib, "FormatConversion.lib")
#endif

//DWORD WINAPI createIPCResponseThread(LPVOID pVoid)
//{
//    Mould* pMould = (Mould*)pVoid;
//    if (!pMould) { return 0; }
//
//    pMould->RunThread();
//
//    return 0;
//}

Mould::Mould(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::Mould)
{
    ui->setupUi(this);
    ui->progressBar->setHidden(true);
    b_job_finished = false;
    callback_para = new template_DEM_para_back;
    DOC = NULL;
    processHandle = NULL;
    m_pCurIPC = NULL;
    IPC_thread = NULL;
    copy_model = NULL;
    parameter = new template_DEM_para;
    shared_memory_name[0] = 0;
    self_event[0] = 0;
    other_event[0] = 0;
    project_name = "toDEM";
    project_path = "c:/";
    sensor_name = "TSX";//卫星名暂定为TerraSAR-X,可修改
    this->setWindowIcon(QIcon(APP_ICON));
    this->setWindowTitle(QString::fromLocal8Bit("DEM模板参数设置"));
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &Mould::accepted);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &Mould::rejected);
    connect(ui->browse, &QPushButton::clicked, this, &Mould::Browse_Savepath);
    for (int i = 0; i < 7; i++)
    {
        MyPage* page = new MyPage(ui->ToolBox, i+1, 7);
        ui->ToolBox->addItem(page);
    }
    this->installEventFilter(this);
    
}

Mould::~Mould()
{
    if (DOC)
    {
        delete DOC;
        DOC = NULL;
    }
    if (parameter)
    {
        delete parameter;
        parameter = NULL;
    }
    if (IPC_thread)
    {
        delete IPC_thread;
        IPC_thread = NULL;
    }
    if (m_pCurIPC)
    {
        delete m_pCurIPC;
        m_pCurIPC = NULL;
    }
    if (callback_para)
    {
        delete callback_para;
        callback_para = NULL;
    }
    delete ui;
}

bool Mould::eventFilter(QObject* watched, QEvent* event)
{
    //重新改变控件的显示大小
    switch ((int)event->type())
    {
    case QEvent::Resize:
        if (ui->ToolBox)
        {
            //ui->ToolBox->ToolBox_size = ui->ToolBox->size();
            ui->ToolBox->Wnd_Resize();
        }
        break;
    case QEvent::Close:
        TerminateSubProcess();
        StopThread();
        break;
    }

    return QWidget::eventFilter(watched, event);
}

void Mould::accepted()
{
    USES_CONVERSION;
    //保存模板参数
    for (int i = 0; i < 7; i++)
    {
        MyPage* page = ui->ToolBox->getItem(i);
        if (page)
        {
            if (!page->get_param(parameter)) return;
        }
        else
        {
            return;
        }
    }
    if (!ui->project_name->text().isEmpty()) project_name = ui->project_name->text();
    else return;
    if (ui->project_path->text().isEmpty()) return;
    strcpy(parameter->project_name, project_name.toStdString().c_str());
    strcpy(parameter->working_directory, project_path.toStdString().c_str());
    strcpy(parameter->sensor, sensor_name.toStdString().c_str());

    //工程防重名检查
    if (copy_model)
    {
        if (copy_model->findItems(project_name + ".insar").size() != 0) return;
    }

    m_pCurIPC = new InSAR_IPC();//初始化通信指针
    set_IPC_name_bytime();//根据时间戳创建惟一的共享内存和事件
    bool bInit = m_pCurIPC->InitIPCMemory(true, QString(shared_memory_name).toStdWString().data(), 102400000);
    if (!bInit) return;
    bInit = m_pCurIPC->InitSelfEvent(QString(self_event).toStdWString().data(), FALSE);
    if (!bInit) return;
    bInit = m_pCurIPC->InitOtherEvent(QString(other_event).toStdWString().data(), TRUE);
    if (!bInit) return;

    //禁用确定按钮
    ui->buttonBox->buttons().at(0)->setDisabled(true);

    //向共享内存写入处理参数
    DWORD dwOffset = 0;
    m_pCurIPC->WriteData(dwOffset, (char*)parameter, sizeof(template_DEM_para));
    // 默认设置子进程有信号，本进程无信号
    m_pCurIPC->SetEventIntf(false);
    m_pCurIPC->ResetEventIntf(true);

    //创建等待响应子线程

    IPC_thread = new template_dem_IPC_thread;
    IPC_thread->moveToThread(new QThread(this));

    connect(this, &Mould::operate, IPC_thread, &template_dem_IPC_thread::RunThread, Qt::QueuedConnection);
    connect(IPC_thread, &template_dem_IPC_thread::updateProcess, this, &Mould::updateProcess);
    //connect(IPC_thread->thread(), &QThread::finished, IPC_thread, &template_dem_IPC_thread::deleteLater);
    connect(IPC_thread, &template_dem_IPC_thread::endProcess, this, &Mould::endProcess);
    connect(this, &QWidget::destroyed, this, &Mould::StopThread);
    //connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &Mould::StopThread);// , Qt::QueuedConnection);
    IPC_thread->thread()->start();

    ui->progressBar->setHidden(false);
    ui->progressBar->setValue(1);
    ui->progressBar->setFormat(QString::fromLocal8Bit("开始处理……"));
    ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    this->operate(m_pCurIPC, callback_para);


    //创建子进程进行处理
    QString app_path = QCoreApplication::applicationDirPath();
    QString exe_name = "template_dem.exe";
    QString shared_mem_name(shared_memory_name);
    QString self_event_name(self_event);
    QString other_event_name(other_event);
    QString cmdline = app_path + "/" + exe_name + " " + shared_mem_name + " " + self_event_name + " " +  other_event_name;
    LPWSTR strCmdLine = new TCHAR[512];
    wcscpy(strCmdLine, cmdline.toStdWString().data());
    processHandle = CreateSubProcess(strCmdLine);
    delete[] strCmdLine;

    

}


void Mould::rejected()
{
    TerminateSubProcess();
    StopThread();
    this->close();
}

void Mould::Browse_Savepath()
{
    QString path = QFileDialog::getExistingDirectory(this, QString::fromLocal8Bit("请选择保存路径"), "");
    ui->project_path->setText(path);
    project_path = path;
}



void Mould::updateProcess(int value, QString information)
{
    ui->progressBar->setValue(value);
    ui->progressBar->setFormat(QString::fromLocal8Bit("%1：%2%").arg(information).arg(value));
    ui->progressBar->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    Sleep(3000);
}

void Mould::ReceiveModel(QStandardItemModel* model)
{
    this->copy_model = model;
}

HANDLE Mould::CreateSubProcess(LPWSTR strCmdLine)
{
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));

    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    BOOL bOK = CreateProcess(NULL, strCmdLine, NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi);

    if (bOK)
    {
        return pi.hProcess;
    }

    return NULL;
}

void Mould::set_IPC_name_bytime()
{
    time_t t = std::time(0);
    sprintf(shared_memory_name, "SatExplorer_shared_memory_%lld", t);
    sprintf(self_event, "SatExplorer_self_event_%lld", t);
    sprintf(other_event, "SatExplorer_self_event_%lld", t);
}

void Mould::endProcess()
{
    b_job_finished = true;
    add_project();
    emit sendCopy(this->copy_model);
    IPC_thread->thread()->quit();
    IPC_thread->thread()->wait();
    ui->progressBar->hide();
    this->close();
}

void Mould::StopThread()
{
    if (IPC_thread != NULL)
    {
        if (IPC_thread->thread()->isRunning())
        {
            IPC_thread->thread()->requestInterruption();
            IPC_thread->thread()->quit();
            IPC_thread->thread()->wait();
        }
        IPC_thread = NULL;
    }
}

void Mould::endThread()
{
    IPC_thread->thread()->quit();
    IPC_thread->thread()->wait();
}

void Mould::add_project()
{
    if (!copy_model || !b_job_finished || !callback_para) return;

    /*---------------------------------------*/
    /*        向界面工程树中加入新的工程     */
    /*---------------------------------------*/

    QStandardItem* project = new QStandardItem(project_name + ".insar");
    project->setIcon(QIcon(PROJECT_ICON));
    project->setStatusTip(NOT_IN_PROCESS);
    QStandardItem* item1_path = new QStandardItem(project_path + "/" + project_name);
    copy_model->setRowCount(copy_model->rowCount() + 1);
    copy_model->setItem(copy_model->rowCount() - 1, 0, project);
    copy_model->setItem(copy_model->rowCount() - 1, 1, item1_path);


    //导入数据
    
    QStandardItem* origin = new QStandardItem(callback_para->import_node_name);
    origin->setIcon(QIcon(FOLDER_ICON));
    project->appendRow(origin);
    QStandardItem* Rank_import = new QStandardItem("complex-0.0");
    project->setChild(project->rowCount() - 1, 1, Rank_import);

    QStandardItem* import_master_name = new QStandardItem(callback_para->import_master_name);
    import_master_name->setToolTip("complex");
    QStandardItem* import_master_file = new QStandardItem(callback_para->import_master_file);
    import_master_name->setIcon(QIcon(IMAGEDATA_ICON));
    origin->appendRow(import_master_name);
    origin->setChild(origin->rowCount() - 1, 1, import_master_file);

    QStandardItem* import_slave_name = new QStandardItem(callback_para->import_slave_name);
    import_slave_name->setToolTip("complex");
    QStandardItem* import_slave_file = new QStandardItem(callback_para->import_slave_file);
    import_slave_name->setIcon(QIcon(IMAGEDATA_ICON));
    origin->appendRow(import_slave_name);
    origin->setChild(origin->rowCount() - 1, 1, import_slave_file);


    //裁剪

    QStandardItem* AOI_node_name = new QStandardItem(callback_para->AOI_node_name);
    AOI_node_name->setIcon(QIcon(FOLDER_ICON));
    project->appendRow(AOI_node_name);
    QStandardItem* Rank_AOI = new QStandardItem("complex-1.0");
    project->setChild(project->rowCount() - 1, 1, Rank_AOI);


    QStandardItem* AOI_master_name = new QStandardItem(callback_para->AOI_master_name);
    AOI_master_name->setToolTip("complex");
    QStandardItem* AOI_master_file = new QStandardItem(callback_para->AOI_master_file);
    AOI_master_name->setIcon(QIcon(IMAGEDATA_ICON));
    AOI_node_name->appendRow(AOI_master_name);
    AOI_node_name->setChild(AOI_node_name->rowCount() - 1, 1, AOI_master_file);

    QStandardItem* AOI_slave_name = new QStandardItem(callback_para->AOI_slave_name);
    AOI_slave_name->setToolTip("complex");
    QStandardItem* AOI_slave_file = new QStandardItem(callback_para->AOI_slave_file);
    AOI_slave_name->setIcon(QIcon(IMAGEDATA_ICON));
    AOI_node_name->appendRow(AOI_slave_name);
    AOI_node_name->setChild(AOI_node_name->rowCount() - 1, 1, AOI_slave_file);

    //配准

    QStandardItem* regis_node_name = new QStandardItem(callback_para->regis_node_name);
    regis_node_name->setIcon(QIcon(FOLDER_ICON));
    project->appendRow(regis_node_name);
    QStandardItem* Rank_regis = new QStandardItem("complex-2.0");
    project->setChild(project->rowCount() - 1, 1, Rank_regis);


    QStandardItem* regis_master_name = new QStandardItem(callback_para->regis_master_name);
    regis_master_name->setToolTip("complex");
    QStandardItem* regis_master_file = new QStandardItem(callback_para->regis_master_file);
    regis_master_name->setIcon(QIcon(IMAGEDATA_ICON));
    regis_node_name->appendRow(regis_master_name);
    regis_node_name->setChild(regis_node_name->rowCount() - 1, 1, regis_master_file);

    QStandardItem* regis_slave_name = new QStandardItem(callback_para->regis_slave_name);
    regis_slave_name->setToolTip("complex");
    QStandardItem* regis_slave_file = new QStandardItem(callback_para->regis_slave_file);
    regis_slave_name->setIcon(QIcon(IMAGEDATA_ICON));
    regis_node_name->appendRow(regis_slave_name);
    regis_node_name->setChild(regis_node_name->rowCount() - 1, 1, regis_slave_file);

    //干涉
    QStandardItem* ifg_node_name = new QStandardItem(callback_para->ifg_node_name);
    ifg_node_name->setIcon(QIcon(FOLDER_ICON));
    project->appendRow(ifg_node_name);
    QStandardItem* Rank_ifg = new QStandardItem("phase-1.0");
    project->setChild(project->rowCount() - 1, 1, Rank_ifg);


    QStandardItem* ifg_name = new QStandardItem(callback_para->ifg_name);
    ifg_name->setToolTip("phase");
    QStandardItem* ifg_file = new QStandardItem(callback_para->ifg_file);
    ifg_name->setIcon(QIcon(IMAGEDATA_ICON));
    ifg_node_name->appendRow(ifg_name);
    ifg_node_name->setChild(ifg_node_name->rowCount() - 1, 1, ifg_file);

    if (parameter->ifg_b_coh)
    {
        QStandardItem* ifg_coherence_name = new QStandardItem(callback_para->ifg_coherence_name);
        ifg_coherence_name->setToolTip("coherence");
        QStandardItem* ifg_coh_file = new QStandardItem(callback_para->ifg_file);
        ifg_coherence_name->setIcon(QIcon(IMAGEDATA_ICON));
        ifg_node_name->appendRow(ifg_coherence_name);
        ifg_node_name->setChild(ifg_node_name->rowCount() - 1, 1, ifg_coh_file);
    }


    //滤波

    QStandardItem* denoise_node_name = new QStandardItem(callback_para->denoise_node_name);
    denoise_node_name->setIcon(QIcon(FOLDER_ICON));
    project->appendRow(denoise_node_name);
    QStandardItem* Rank_denoise = new QStandardItem("phase-2.0");
    project->setChild(project->rowCount() - 1, 1, Rank_denoise);


    QStandardItem* denoise_ifg_name = new QStandardItem(callback_para->denoise_ifg_name);
    denoise_ifg_name->setToolTip("phase");
    QStandardItem* denoise_ifg_file = new QStandardItem(callback_para->denoise_ifg_file);
    denoise_ifg_name->setIcon(QIcon(IMAGEDATA_ICON));
    denoise_node_name->appendRow(denoise_ifg_name);
    denoise_node_name->setChild(denoise_node_name->rowCount() - 1, 1, denoise_ifg_file);

    //解缠

    QStandardItem* unwrap_node_name = new QStandardItem(callback_para->unwrap_node_name);
    unwrap_node_name->setIcon(QIcon(FOLDER_ICON));
    project->appendRow(unwrap_node_name);
    QStandardItem* Rank_unwrap = new QStandardItem("phase-3.0");
    project->setChild(project->rowCount() - 1, 1, Rank_unwrap);


    QStandardItem* unwrap_ifg_name = new QStandardItem(callback_para->unwrap_ifg_name);
    unwrap_ifg_name->setToolTip("phase");
    QStandardItem* unwrap_ifg_file = new QStandardItem(callback_para->unwrap_ifg_file);
    unwrap_ifg_name->setIcon(QIcon(IMAGEDATA_ICON));
    unwrap_node_name->appendRow(unwrap_ifg_name);
    unwrap_node_name->setChild(unwrap_node_name->rowCount() - 1, 1, unwrap_ifg_file);

    //高程

    QStandardItem* dem_node_name = new QStandardItem(callback_para->dem_node_name);
    dem_node_name->setIcon(QIcon(FOLDER_ICON));
    project->appendRow(dem_node_name);
    QStandardItem* Rank_dem = new QStandardItem("dem-1.0");
    project->setChild(project->rowCount() - 1, 1, Rank_dem);


    QStandardItem* dem_name = new QStandardItem(callback_para->dem_name);
    dem_name->setToolTip("dem");
    QStandardItem* dem_file = new QStandardItem(callback_para->dem_file);
    dem_name->setIcon(QIcon(IMAGEDATA_ICON));
    dem_node_name->appendRow(dem_name);
    dem_node_name->setChild(dem_node_name->rowCount() - 1, 1, dem_file);


    /*---------------------------------------*/
    /*             新建xml工程文件           */
    /*---------------------------------------*/
    XMLFile xml;
    QFileInfo info = QFileInfo(project_path);
    QString path = info.absoluteFilePath();
    QString xml_file = project_path + "/" + project_name + "/" + project_name + ".insar";
    xml.XMLFile_creat_new_project((path + "/" + project_name).toStdString().c_str(), QString("%1.insar").arg(project_name).toStdString().c_str(), "1.0");
    
    //导入数据
    DOC = new XMLFile;
    DOC->XMLFile_load(xml_file.toStdString().c_str());
    DOC->XMLFile_add_origin(QString(callback_para->import_node_name).toStdString().c_str(),
        QString(callback_para->import_master_name).toStdString().c_str(),
        QString("/" + QString(callback_para->import_node_name) + "/" + QString(callback_para->import_master_name) + ".h5").toStdString().c_str(),
        sensor_name.toStdString().c_str());
    DOC->XMLFile_add_origin(QString(callback_para->import_node_name).toStdString().c_str(),
        QString(callback_para->import_slave_name).toStdString().c_str(),
        QString("/" + QString(callback_para->import_node_name) + "/" + QString(callback_para->import_slave_name) + ".h5").toStdString().c_str(),
        sensor_name.toStdString().c_str());

    //裁剪
    DOC->XMLFile_add_cut(QString(callback_para->AOI_node_name).toStdString().c_str(),
        QString(callback_para->AOI_master_name).toStdString().c_str(),
        QString("/" + QString(callback_para->AOI_node_name) + "/" + QString(callback_para->AOI_master_name) + ".h5").toStdString().c_str(),
        0, 0, 
        parameter->AOI_center_lon, parameter->AOI_center_lat,
        parameter->AOI_width, parameter->AOI_height, "complex-1.0");
    DOC->XMLFile_add_cut(QString(callback_para->AOI_node_name).toStdString().c_str(),
        QString(callback_para->AOI_slave_name).toStdString().c_str(),
        QString("/" + QString(callback_para->AOI_node_name) + "/" + QString(callback_para->AOI_slave_name) + ".h5").toStdString().c_str(),
        0, 0,
        parameter->AOI_center_lon, parameter->AOI_center_lat,
        parameter->AOI_width, parameter->AOI_height, "complex-1.0");

    //配准

    DOC->XMLFile_add_regis(
        QString(callback_para->regis_node_name).toStdString().c_str(),
        QString(callback_para->regis_master_name).toStdString().c_str(),
        QString("/" + QString(callback_para->regis_node_name) + "/" + QString(callback_para->regis_master_name) + ".h5").toStdString().c_str(),
        0,
        0,
        1,
        parameter->coregis_interp_times,
        parameter->coregis_blocksize,
        "0 0",
        "0 0",
        "0 0"
    );
    DOC->XMLFile_add_regis(
        QString(callback_para->regis_node_name).toStdString().c_str(),
        QString(callback_para->regis_slave_name).toStdString().c_str(),
        QString("/" + QString(callback_para->regis_node_name) + "/" + QString(callback_para->regis_slave_name) + ".h5").toStdString().c_str(),
        0,
        0,
        1,
        parameter->coregis_interp_times,
        parameter->coregis_blocksize,
        "0 0",
        "0 0",
        "0 0"
    );

    //干涉

    DOC->XMLFile_add_interferometric_phase(
        QString(callback_para->ifg_node_name).toStdString().c_str(),
        QString(callback_para->ifg_name).toStdString().c_str(),
        QString("/" + QString(callback_para->ifg_node_name) + "/" + QString(callback_para->ifg_name) + ".h5").toStdString().c_str(),
        QString(callback_para->regis_master_name).toStdString().c_str(),
        "phase-1.0",
        0,
        0,
        1,
        0, 
        0,
        parameter->ifg_coh_wndsize_rg,
        parameter->ifg_coh_wndsize_az,
        parameter->ifg_multilook_rg,
        parameter->ifg_multilook_az
    );
    if (parameter->ifg_b_coh)
    {
        DOC->XMLFile_add_interferometric_phase(
            QString(callback_para->ifg_node_name).toStdString().c_str(),
            QString(callback_para->ifg_coherence_name).toStdString().c_str(),
            QString("/" + QString(callback_para->ifg_node_name) + "/" + QString(callback_para->ifg_name) + ".h5").toStdString().c_str(),
            QString(callback_para->regis_master_name).toStdString().c_str(),
            "coherence-1.0",
            0,
            0,
            1,
            0,
            1,
            parameter->ifg_coh_wndsize_rg,
            parameter->ifg_coh_wndsize_az,
            parameter->ifg_multilook_rg,
            parameter->ifg_multilook_az
        );
    }
    

    //滤波
    const char* filter_method = NULL;
    if (parameter->denoise_method == 1)
    {
        filter_method = "Slope";
    }
    else if (parameter->denoise_method == 2)
    {
        filter_method = "Goldstein";
    }
    else
    {
        filter_method = "DL";
    }
    DOC->XMLFile_add_denoise(
        QString(callback_para->denoise_node_name).toStdString().c_str(),
        QString(callback_para->denoise_ifg_name).toStdString().c_str(),
        QString("/" + QString(callback_para->denoise_node_name) + "/" + QString(callback_para->denoise_ifg_name) + ".h5").toStdString().c_str(),
        0, 
        0, 
        filter_method,
        parameter->slope_size,
        parameter->slope_pre_size,
        parameter->Goldstein_FFT_wdnsize,
        parameter->Goldstein_pad,
        parameter->Goldstein_alpha,
        " ", 
        " ", 
        " "
    );

    //解缠
    const char* unwrap_method = NULL;
    if (parameter->unwrap_method == 1)
    {
        unwrap_method = "QualityGuided";
    }
    else if (parameter->unwrap_method == 2)
    {
        unwrap_method = "MCF";
    }
    else if(parameter->unwrap_method == 3)
    {
        unwrap_method = "SNAPHU";
    }
    else
    {
        unwrap_method = "Combined";
    }
    DOC->XMLFile_add_unwrap(
        QString(callback_para->unwrap_node_name).toStdString().c_str(),
        QString(callback_para->unwrap_ifg_name).toStdString().c_str(),
        QString("/" + QString(callback_para->unwrap_node_name) + "/" + QString(callback_para->unwrap_ifg_name) + ".h5").toStdString().c_str(),
        0,
        0,
        unwrap_method,
        parameter->unwrap_coh_thresh);

    //高程

    DOC->XMLFile_add_dem(
        QString(callback_para->dem_node_name).toStdString().c_str(),
        QString(callback_para->dem_name).toStdString().c_str(),
        QString("/" + QString(callback_para->dem_node_name) + "/" + QString(callback_para->dem_name) + ".h5").toStdString().c_str(),
        0,
        0, 
        "Iteration", 
        parameter->iter_times);

    DOC->XMLFile_save(xml_file.toStdString().c_str());


}


void Mould::TerminateSubProcess()
{
    if (processHandle && processHandle != INVALID_HANDLE_VALUE)
    {
        TerminateProcess(processHandle, 0);
        processHandle = NULL;
    }
}