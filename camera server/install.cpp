#include "stdafx.h"

_NT_BEGIN

#include "log.h"
extern volatile const UCHAR guz = 0;

struct ServiceData : SERVICE_NOTIFY
{
	ServiceData() { 
		RtlZeroMemory(this, sizeof(ServiceData)); 
		dwVersion = SERVICE_NOTIFY_STATUS_CHANGE;
		pfnNotifyCallback = ScNotifyCallback;
		pContext = this;
	}

	void OnScNotify()
	{
		DbgPrint("ScNotifyCallback(%u %08x %x %x)\r\n", 
			dwNotificationStatus, dwNotificationTriggered, 
			ServiceStatus.dwCurrentState, ServiceStatus.dwCheckPoint );
	}

	static VOID CALLBACK ScNotifyCallback (_In_ PVOID pParameter)
	{
		reinterpret_cast<ServiceData*>(pParameter)->OnScNotify();
	}
};

STATIC_WSTRING_(RBMMCAMERA);

HRESULT UnInstallService() 
{
	DbgPrint("UnInstallService()\r\n");

	if (SC_HANDLE scm = OpenSCManagerW(0, 0, 0))
	{
		HRESULT hr = S_OK;

		SC_HANDLE svc = OpenServiceW(scm, RBMMCAMERA, DELETE|SERVICE_STOP|SERVICE_QUERY_STATUS);

		CloseServiceHandle(scm);

		if (svc)					
		{
			ServiceData sd;

			if (ControlService(svc, SERVICE_CONTROL_STOP, (SERVICE_STATUS*)&sd.ServiceStatus))
			{
				ULONG64 t_end = GetTickCount64() + 4000, t;

				while (sd.ServiceStatus.dwCurrentState != SERVICE_STOPPED)
				{
					if (sd.dwNotificationStatus = NotifyServiceStatusChangeW(svc, 
						SERVICE_NOTIFY_CONTINUE_PENDING|
						SERVICE_NOTIFY_DELETE_PENDING|
						SERVICE_NOTIFY_PAUSE_PENDING|
						SERVICE_NOTIFY_PAUSED|
						SERVICE_NOTIFY_RUNNING|
						SERVICE_NOTIFY_START_PENDING|
						SERVICE_NOTIFY_STOP_PENDING|
						SERVICE_NOTIFY_STOPPED, &sd))
					{
						logError("SERVICE_CONTROL_STOP", sd.dwNotificationStatus);
						break;
					}

					sd.dwNotificationStatus = ERROR_TIMEOUT;

					if ((t = GetTickCount64()) >= t_end ||
						WAIT_IO_COMPLETION != SleepEx((ULONG)(t_end - t), TRUE) ||
						sd.dwNotificationStatus != NOERROR)
					{
						break;
					}
				}

				if (sd.ServiceStatus.dwCurrentState != SERVICE_STOPPED)
				{
					logError("dwNotificationStatus", sd.dwNotificationStatus);
				}
			}
			else
			{
				logError("SERVICE_CONTROL_STOP");
			}

			if (!DeleteService(svc))
			{
				hr = logError("DeleteService");
			}

			CloseServiceHandle(svc);
			ZwTestAlert();
		}
		else
		{
			hr = logError("OpenService");
		}
		return hr;
	}

	return logError("OpenSCManager");
}

HRESULT InstallService(ULONG64 crc2, ULONG64 crc1)
{
	HRESULT hr = E_OUTOFMEMORY;

	if (PWSTR lpBinaryPathName = new WCHAR[MAXSHORT + 1])
	{
		ULONG cch = GetModuleFileNameW((HMODULE)&__ImageBase, lpBinaryPathName + 1, MAXUSHORT - 38);
		if (!(hr = GetLastHresult()))
		{
			DbgPrint("InstallService(%ws)...\r\n", lpBinaryPathName + 1);
			*lpBinaryPathName = '\"';

			swprintf_s(lpBinaryPathName + cch + 1, 37, L"\" \n%I64x\n%I64x", crc2, crc1);

			if (SC_HANDLE scm = OpenSCManagerW(0, 0, SC_MANAGER_CREATE_SERVICE))
			{
				hr = S_OK;

				if (SC_HANDLE svc = CreateServiceW(
					scm,						// SCM database
					RBMMCAMERA,					// name of service
					L"remote camera",		// service name to display
					SERVICE_ALL_ACCESS,			// desired access
					SERVICE_WIN32_OWN_PROCESS,// service type
					SERVICE_AUTO_START,			// start type
					SERVICE_ERROR_NORMAL,		// error control type
					lpBinaryPathName,	// path to service's binary
					0,					// no load ordering group
					0,					// no tag identifier
					0,					// no dependencies
					0,					// LocalSystem account
					0))					// no password
				{

					static const SERVICE_DESCRIPTION sd = {
						const_cast<PWSTR>(L"remote controlled camera")
					};

					if (!ChangeServiceConfig2W(
						svc,						// handle to service
						SERVICE_CONFIG_DESCRIPTION, // change: description
						const_cast<SERVICE_DESCRIPTION*>(&sd)))			// new description
					{
						logError("ChangeServiceConfig2");
					}

					if (!StartServiceW(svc, 0, 0))
					{
						hr = logError("StartServiceW");
					}

					CloseServiceHandle(svc);
				}
				else
				{
					hr = logError("CreateService");
				}

				CloseServiceHandle(scm);
			}
			else
			{
				hr = logError("OpenSCManager");
			}
		}

		delete [] lpBinaryPathName;
	}

	return hr;
}

_NT_END