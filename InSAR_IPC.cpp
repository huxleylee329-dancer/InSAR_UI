#include"InSAR_IPC.h"
#include <atlstr.h>
#include <windows.h>

InSAR_IPC::InSAR_IPC()
{
	hMapFile = INVALID_HANDLE_VALUE;

	selfEvent = INVALID_HANDLE_VALUE;

	otherEvent = INVALID_HANDLE_VALUE;

	GetSystemInfo(&info);
}

InSAR_IPC::~InSAR_IPC()
{
	if (hMapFile && hMapFile != INVALID_HANDLE_VALUE)
	{
		CloseHandle(hMapFile);

		hMapFile = INVALID_HANDLE_VALUE;
	}

	if (otherEvent && otherEvent != INVALID_HANDLE_VALUE)
	{
		CloseHandle(otherEvent);

		otherEvent = INVALID_HANDLE_VALUE;
	}

	if (selfEvent && selfEvent != INVALID_HANDLE_VALUE)
	{
		CloseHandle(selfEvent);

		selfEvent = INVALID_HANDLE_VALUE;
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

	if (hMapFile == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	return true;
}

bool InSAR_IPC::ReadData(DWORD& dwOffset, DWORD dwSize, char* buf)
{
	if (INVALID_HANDLE_VALUE == hMapFile)
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
	if (INVALID_HANDLE_VALUE == hMapFile)
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

	if (INVALID_HANDLE_VALUE == selfEvent)
	{
		bRet = false;
	}

	return bRet;
}

bool InSAR_IPC::InitOtherEvent(LPCWSTR eventName, BOOL bInitState)
{
	bool bRet = true;

	otherEvent = CreateEvent(NULL, TRUE, bInitState, eventName);

	if (INVALID_HANDLE_VALUE == otherEvent)
	{
		bRet = false;
	}

	return bRet;
}

bool InSAR_IPC::OpenOtherEvent(LPCWSTR eventName)
{
	bool bRet = true;

	otherEvent = OpenEvent(EVENT_ALL_ACCESS, FALSE, eventName);

	if (INVALID_HANDLE_VALUE == selfEvent || NULL == selfEvent)
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

	if (INVALID_HANDLE_VALUE == selfEvent || NULL == selfEvent)
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
		if (selfEvent != INVALID_HANDLE_VALUE)
		{
			SetEvent(selfEvent);
		}
	}
	else if (otherEvent != INVALID_HANDLE_VALUE)
	{
		SetEvent(otherEvent);
	}
}

void InSAR_IPC::ResetEventIntf(bool bSelf)
{
	if (bSelf)
	{
		if (selfEvent != INVALID_HANDLE_VALUE)
		{
			ResetEvent(selfEvent);
		}
	}
	else if (otherEvent != INVALID_HANDLE_VALUE)
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
