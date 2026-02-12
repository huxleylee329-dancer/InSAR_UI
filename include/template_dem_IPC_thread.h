#pragma once
#include <QObject>
#include<QDebug>
#include<QThread>
#include<qmutex.h>
#include<FormatConversion.h>
#include<qstandarditemmodel.h>
#include<QDir>
#include"InSAR_IPC.h"

class template_dem_IPC_thread : public QObject
{
	Q_OBJECT
public:
	template_dem_IPC_thread(QObject* parent = nullptr);
	~template_dem_IPC_thread();
public slots:
	void StopProcess();
	void RunThread(InSAR_IPC* IPC, template_DEM_para_back* callback_para);
signals:
	void updateProcess(int, QString);
	void endProcess();
	//void sendIPC(InSAR_IPC* IPC);

private:
	InSAR_IPC* IPC;
	QMutex lock;
	bool stop_flag;
	bool b_thread_runing;
	template_DEM_para_back* callback_para;
};
