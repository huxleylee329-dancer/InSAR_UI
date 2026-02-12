#include<QMessageBox>
#include<qcoreapplication.h>
#include"template_dem_IPC_thread.h"
#include"InSAR_IPC.h"
#include"para_struct.h"

template_dem_IPC_thread::template_dem_IPC_thread(QObject* parent)
{
	IPC = NULL;
    callback_para = NULL;
    b_thread_runing = false;
}
template_dem_IPC_thread::~template_dem_IPC_thread()
{

}
void template_dem_IPC_thread::StopProcess()
{
	QMutexLocker locker(&lock);
	this->stop_flag = false;
}

void template_dem_IPC_thread::RunThread(InSAR_IPC* ipc, template_DEM_para_back* callback_para_copy)
{
    if (!ipc || !callback_para_copy) return;
    this->callback_para = callback_para_copy;
    IPC = ipc;
    b_thread_runing = true;
    DWORD dwOffset = 0;
    stcIPCMsgHeader header;
    int  percent = 10;
    //DWORD dw;
    while (b_thread_runing)
    {
        while (WaitForSingleObject(IPC->GetEvent(true), 3000) != WAIT_OBJECT_0)
        {
            if (QThread::currentThread()->isInterruptionRequested()) return;
        }
        dwOffset = 0;
        IPC->ReadData(dwOffset, sizeof(stcIPCMsgHeader), (char*)&header);
        if (header.msgType == eCallbackType_MsgError)
        {

            b_thread_runing = false;
        }
        else if (header.msgType == eCallbackType_MsgFinished)
        {
            // 更新界面
            updateProcess(100, "Finished");
            memcpy(callback_para, &(header.callback_para), sizeof(template_DEM_para_back));
            Sleep(3000);
            b_thread_runing = false;
        }

        else if (header.msgType == eCallbackType_UpdateCmplt)
        {

            updateProcess(header.progress, QString(header.message));

        }
        else
        {

        }

        // 操作完毕，设置子进程为有信号
        IPC->SetEventIntf(false);
        // 设置本进程为无信号
        IPC->ResetEventIntf(true);
    }
    emit endProcess();
}

