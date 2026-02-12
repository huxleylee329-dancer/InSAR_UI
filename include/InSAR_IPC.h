#ifndef INSAR_IPC_H_
#define INSAR_IPC_H_

#include <string>
#include <Windows.h>
#include"para_struct.h"

// 事件类型

// 使用注意:
// CLYXCIPC server, client
// server.InitIPCMemory(true, "server", 0);
// server.InitSelfEvent("serverEvent", FALSE);
// client.InitIPCMemory(false, "client");
// client.InitSelfEvent("clientEvent", TRUE);
// server.InitOtherEvent("clientEvent");
// client.InitOtherEvent("serverEvent");

class InSAR_IPC {

public:
	InSAR_IPC(void);
	~InSAR_IPC();

private:
	HANDLE hMapFile;
	bool bServer;
	HANDLE selfEvent, otherEvent;
	SYSTEM_INFO info;

public:
	// 初始化共享内存
	bool InitIPCMemory(bool bSever, LPCWSTR fileName, DWORD dwServerMapSize = 0);
	// 共享内存读取
	bool ReadData(DWORD& dwOffset, DWORD dwSize, char* buf);
	// 共享内存写入
	bool WriteData(DWORD& dwOffset, char* buf, DWORD dwSize);

public:
	// 通信事件初始化，初始化自己事件
	bool InitSelfEvent(LPCWSTR eventName, BOOL bInitState);
	// 通信初始化，初始化对方事件 
	bool InitOtherEvent(LPCWSTR eventName, BOOL bInitState);
	// 通信初始化，初始化对方事件 
	bool OpenSelfEvent(LPCWSTR eventName);
	// 通信初始化，初始化对方事件 
	bool OpenOtherEvent(LPCWSTR eventName);
	// 设置有信号状态
	void SetEventIntf(bool bSelf);
	// 设置无信号状态
	void ResetEventIntf(bool bSelf);
	// 获取事件
	HANDLE GetEvent(bool bSelf);
};



enum eCallbackType
{
	eCallbackType_Unknown = 0,			// 未知类型
	eCallbackType_UpdateCmplt,			// 更新当前完成进度
	eCallbackType_MsgFinished,			// 通知任务完成
	eCallbackType_MsgError,				// 通知任务发生错误
	eCallbackType_RequestData,			// 请求数据
	eCallbackType_SaveData				// 存储数据
};

// IPC消息头
struct stcIPCMsgHeader
{
	/*消息类型*/
	int msgType;
	/*进度信息*/
	int progress;
	/*消息内容*/
	char message[2048];
	/*回传参数*/
	template_DEM_para_back callback_para;
	stcIPCMsgHeader()
	{
		msgType = eCallbackType::eCallbackType_Unknown;
		message[0] = 0;
		progress = 1;
	}
};

#endif
