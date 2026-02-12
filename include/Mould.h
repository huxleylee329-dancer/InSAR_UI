#pragma once
#include <QtWidgets/QMainWindow>
#include<qstandarditemmodel.h>
#include<MyToolBox.h>
#include<MyPage.h>
#include"para_struct.h"
#include"InSAR_IPC.h"
#include"template_dem_IPC_thread.h"

class QFrame;
class MyToolBox;
class Mould : public QWidget
{
    Q_OBJECT
        enum { LEFT, TOP, RIGHT, BOTTOM };
public:
    explicit Mould(QWidget* parent = 0);
    ~Mould();
public slots:
    void updateProcess(int, QString);
    void ReceiveModel(QStandardItemModel*);
    HANDLE CreateSubProcess(LPWSTR strCmdLine);
    void TerminateSubProcess();
    void set_IPC_name_bytime();
    void endProcess();
    void StopThread();
    void endThread();
    /*向界面工程树中加入新的工程，并新建xml工程文件*/
    void add_project();
signals:
    void operate(InSAR_IPC* IPC, template_DEM_para_back* callback_para);
    void sendCopy(QStandardItemModel*);

protected:
    bool eventFilter(QObject* watched, QEvent* event);


private:
    Ui::Mould* ui;
    InSAR_IPC* m_pCurIPC;
    HANDLE processHandle;
    template_DEM_para* parameter;
    /*计算任务是否正常结束*/
    bool b_job_finished;
    /*子进程回传参数（用于更新界面工程树和新建xml工程文件）*/
    template_DEM_para_back* callback_para;
    char shared_memory_name[512];
    char self_event[512];
    char other_event[512];
    QString project_name;
    QString project_path;
    template_dem_IPC_thread* IPC_thread;
    QStandardItemModel* copy_model;
    XMLFile* DOC;
    QString sensor_name;

private slots:
    void accepted();
    void rejected();
    void Browse_Savepath();
};