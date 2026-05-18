#include"InSAR_IPC.h"
#include <atlstr.h>
#include <windows.h>

InSAR_IPC::InSAR_IPC()
{
	hMapFile = NULL;

	selfEvent = NULL;

	otherEvent = NULL;

	GetSystemInfo(&info);
}

InSAR_IPC::~InSAR_IPC()
{
	if (hMapFile != NULL && hMapFile != INVALID_HANDLE_VALUE)
	{
		CloseHandle(hMapFile);

		hMapFile = NULL;
	}

	if (otherEvent != NULL && otherEvent != INVALID_HANDLE_VALUE)
	{
		CloseHandle(otherEvent);

		otherEvent = NULL;
	}

	if (selfEvent != NULL && selfEvent != INVALID_HANDLE_VALUE)
	{
		CloseHandle(selfEvent);

		selfEvent = NULL;
	}
}

bool InSAR_IPC::InitIPCMemory(bool bSever, LPCWSTR fileName, DWORD dwServerMapSize /* = 0 */)
{
	PVOID pView = NULL;

	if (bSever)
	{
		dwServerMapSize = dwServerMapSize == 0 ? 65536 : dwServerMapSize;

		hMapFile = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, dwServerMapSize, fileName);
	}
	else
	{
		hMapFile = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, fileName);
	}

	if (hMapFile == NULL || hMapFile == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	return true;
}

bool InSAR_IPC::ReadData(DWORD& dwOffset, DWORD dwSize, char* buf)
{
	if (hMapFile == NULL || hMapFile == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	PVOID pView = NULL;

	pView = MapViewOfFile(hMapFile, FILE_MAP_READ, 0, dwOffset, dwSize);

	if (pView == NULL)
	{
		return false;
	}

	memcpy(buf, (char*)pView, dwSize);

	if (pView)
	{
		UnmapViewOfFile(pView);

		pView = NULL;
	}

	dwOffset = dwOffset + dwSize - 1;

	DWORD nMod = dwOffset / info.dwAllocationGranularity;

	if (dwOffset % info.dwAllocationGranularity)
	{
		nMod++;
	}

	dwOffset = nMod * info.dwAllocationGranularity;

	return true;
}

bool InSAR_IPC::WriteData(DWORD& dwOffset, char* buf, DWORD dwSize)
{
	if (hMapFile == NULL || hMapFile == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	PVOID pView = NULL;

	pView = MapViewOfFile(hMapFile, FILE_MAP_WRITE, 0, dwOffset, dwSize);

	if (pView == NULL)
	{
		return false;
	}

	memcpy(pView, buf, dwSize);

	if (pView)
	{
		UnmapViewOfFile(pView);

		pView = NULL;
	}

	dwOffset = dwOffset + dwSize - 1;

	DWORD nMod = dwOffset / info.dwAllocationGranularity;

	if (dwOffset % info.dwAllocationGranularity > 0)
	{
		nMod++;
	}

	dwOffset = nMod * info.dwAllocationGranularity;

	return true;
}

bool InSAR_IPC::InitSelfEvent(LPCWSTR eventName, BOOL bInitState)
{
	bool bRet = true;

	selfEvent = CreateEvent(NULL, TRUE, bInitState, eventName);

	if (selfEvent == NULL || selfEvent == INVALID_HANDLE_VALUE)
	{
		bRet = false;
	}

	return bRet;
}

bool InSAR_IPC::InitOtherEvent(LPCWSTR eventName, BOOL bInitState)
{
	bool bRet = true;

	otherEvent = CreateEvent(NULL, TRUE, bInitState, eventName);

	if (otherEvent == NULL || otherEvent == INVALID_HANDLE_VALUE)
	{
		bRet = false;
	}

	return bRet;
}

bool InSAR_IPC::OpenOtherEvent(LPCWSTR eventName)
{
	bool bRet = true;

	otherEvent = OpenEvent(EVENT_ALL_ACCESS, FALSE, eventName);

	if (otherEvent == NULL || otherEvent == INVALID_HANDLE_VALUE)
	{
		CString strDebug;
		strDebug.Format(_T("%s, %d\n"), eventName, GetLastError());
		bRet = false;
	}

	return bRet;
}

bool InSAR_IPC::OpenSelfEvent(LPCWSTR eventName)
{
	bool bRet = true;

	selfEvent = OpenEvent(EVENT_ALL_ACCESS, FALSE, eventName);

	if (selfEvent == NULL || selfEvent == INVALID_HANDLE_VALUE)
	{
		CString strDebug;
		strDebug.Format(_T("%s, %d\n"), eventName, GetLastError());
		bRet = false;
	}

	return bRet;
}

void InSAR_IPC::SetEventIntf(bool bSelf)
{
	if (bSelf)
	{
		if (selfEvent != NULL && selfEvent != INVALID_HANDLE_VALUE)
		{
			SetEvent(selfEvent);
		}
	}
	else if (otherEvent != NULL && otherEvent != INVALID_HANDLE_VALUE)
	{
		SetEvent(otherEvent);
	}
}

void InSAR_IPC::ResetEventIntf(bool bSelf)
{
	if (bSelf)
	{
		if (selfEvent != NULL && selfEvent != INVALID_HANDLE_VALUE)
		{
			ResetEvent(selfEvent);
		}
	}
	else if (otherEvent != NULL && otherEvent != INVALID_HANDLE_VALUE)
	{
		ResetEvent(otherEvent);
	}
}

HANDLE InSAR_IPC::GetEvent(bool bSelf)
{
	if (bSelf)
	{
		return selfEvent;
	}
	else
	{
		return otherEvent;
	}
}
