#include "stdafx.h"

_NT_BEGIN

#include "log.h"
#include "../inc/initterm.h"
#include "SvcBase.h"

BOOL StartServer();

bool _G_stop = FALSE;

class CSvc : public CSvcBase
{
	HANDLE _hThreadId = (HANDLE)GetCurrentThreadId();

	virtual HRESULT Run()
	{
		DbgPrint("+++ Run\r\n");
		static const LARGE_INTEGER Interval = { 0, (LONG)MINLONG };

		do 
		{
			ULONG dwState = m_dwTargetState;

			DbgPrint("state:= %x\r\n", dwState);

			SetState(dwState, SERVICE_ACCEPT_STOP|SERVICE_ACCEPT_PAUSE_CONTINUE);

			if (SERVICE_RUNNING == dwState)
			{
				_G_stop = FALSE;

				WSADATA wd;
				if (!WSAStartup(WINSOCK_VERSION, &wd))
				{
					if (0 <= MFStartup(MF_VERSION))
					{
						if (StartServer())
						{
							DbgPrint("Start Server\r\n");
							ZwWaitForAlertByThreadId(0, const_cast<PLARGE_INTEGER>(&Interval));
							DbgPrint("alert\r\n");
						}
						else
						{
							m_dwTargetState = SERVICE_STOPPED;
						}

						_G_stop = TRUE;

						MFShutdown();
					}

					WSACleanup();
				}
			}
			else
			{
				ZwWaitForAlertByThreadId( 0, const_cast<PLARGE_INTEGER>(&Interval));
			}

		} while (SERVICE_STOPPED != m_dwTargetState);

		DbgPrint("--- Run\r\n");

		return S_OK;
	}

	virtual ULONG Handler(
		ULONG    dwControl,
		ULONG    /*dwEventType*/,
		PVOID   /*lpEventData*/
		)
	{
		switch (dwControl)
		{
		case SERVICE_CONTROL_CONTINUE:
		case SERVICE_CONTROL_PAUSE:
		case SERVICE_CONTROL_STOP:
			return RtlNtStatusToDosErrorNoTeb(ZwAlertThreadByThreadId(_hThreadId));
		}

		return ERROR_SERVICE_CANNOT_ACCEPT_CTRL;
	}
};

void NTAPI ServiceMain(DWORD argc, PWSTR argv[])
{
	if (argc)
	{
		CSvc o;
		o.ServiceMain(argv[0]);
	}
}

HRESULT UnInstallService();
HRESULT InstallService();
NTSTATUS GenKeyXY();

EXTERN_C
NTSYSAPI 
VOID 
NTAPI 
RtlSetLastWin32ErrorAndNtStatusFromNtStatus( _In_ NTSTATUS Status );

void CALLBACK ep(void*)
{
	initterm();
	LOG(Init());

	PCWSTR cmd = GetCommandLineW();
	if (wcschr(cmd, '\n'))
	{
		const static SERVICE_TABLE_ENTRY ste[] = { 
			{ const_cast<PWSTR>(L"RBMMCAMERA"), ServiceMain }, {} 
		};

		if (!StartServiceCtrlDispatcher(ste))
		{
			logError("SERVICE_CONTROL_STOP");
		}

		DbgPrint("Service Exit\r\n");
	}
	else if (cmd = wcschr(cmd, '*'))
	{
		switch (cmd[1])
		{
		case 'i':
			logError("install", InstallService());
			break;
		case 'u':
			logError("uninstall", UnInstallService());
			break;
		case 'k':
			logError("GenKeyXY", GenKeyXY());
			break;
		default:
			__ip:
			logError("cmd line", STATUS_INVALID_PARAMETER);
			break;
		}
	}
	else
	{
		goto __ip;
	}

	destroyterm();
	ExitProcess(0);
}

_NT_END