#pragma once

#include "../asio/socket.h"

struct PACKET_HEADER 
{
	ULONG size;
	ULONG type_xor_crc;
	UCHAR buf[];
};

class __declspec(novtable) CEncTcp : public CTcpEndpoint
{
	using CTcpEndpoint::CTcpEndpoint;

	ULONG64 _M_DisconnectTime = 0;
	BCRYPT_KEY_HANDLE _hKey = 0;
	PTP_TIMER _M_Timer = 0;

	static VOID NTAPI _onTimer(
		_Inout_     PTP_CALLBACK_INSTANCE /*Instance*/,
		_Inout_opt_ PVOID                 Context,
		_Inout_     PTP_TIMER             /*Timer*/
		);

	static VOID NTAPI _scb (
		_Inout_     PTP_CALLBACK_INSTANCE Instance,
		_Inout_opt_ PVOID                 Context
		);

	void deleteTimer();

protected:

	virtual void onTimer(
		_Inout_     PTP_CALLBACK_INSTANCE /*Instance*/,
		_Inout_     PTP_TIMER             /*Timer*/
		);

	void SetKey(BCRYPT_KEY_HANDLE hKey)
	{
		if (hKey = (BCRYPT_KEY_HANDLE)InterlockedExchangePointer(&_hKey, hKey))
		{
			BCryptDestroyKey(hKey);
		}
	}

	virtual ~CEncTcp();

	virtual void OnDisconnect();

	virtual BOOL OnUserData(ULONG type, PBYTE Buffer, ULONG cbTransferred) = 0;

	BOOL OnRecv(PSTR Buffer, ULONG cbTransferred);


	BOOL InitSession(PBYTE pb, ULONG cb);

	NTSTATUS InitSession(BCRYPT_KEY_HANDLE hRsaKey);

	NTSTATUS InitSession(ULONGLONG crc);

public:

	ULONG SendUserData(ULONG type);
	ULONG SendUserData(ULONG type, const void* pbIn, ULONG cbIn);

	void SetDisconnectTime(ULONG dwMilliseconds)
	{
		_M_DisconnectTime = GetTickCount64() + dwMilliseconds;
	}

	NTSTATUS SetTimer(ULONG Period);
};