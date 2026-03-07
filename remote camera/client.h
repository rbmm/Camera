#pragma once

NTSTATUS GetKeyCrc(BCRYPT_KEY_HANDLE hKey, PULONGLONG pcrc);

#include "video.h"
#include "H264.h"
#include "https.h"

class CClient : public CEncTcp, public H264
{
	LONGLONG _crc = 0, _cbData;
	HWND _hwnd, _hwndDlg;
	VBmp* _vid = 0;
	ULONG _biCompression, _biWidth, _biHeight;

	virtual ~CClient();

	virtual ULONG GetConnectData(void** ppSendBuffer);

	virtual BOOL OnConnect(ULONG dwError);

	virtual void OnDisconnect();

	virtual BOOL OnUserData(ULONG type, PBYTE pb, ULONG cb);

	virtual BOOL OnRecv(PSTR Buffer, ULONG cb);

public:

	NTSTATUS OpenKey(PCWSTR name);

	CClient(HWND hwndDlg, HWND hwnd) : _hwnd(hwnd), _hwndDlg(hwndDlg)
	{
	}

	ULONG GetFormats()
	{
		return SendUserData('init');
	}

	BOOL Stop()
	{
		return !SendUserData('stop');
	}

	void test()
	{
		SendUserData('test');
	}

	LONGLONG getDataSize()
	{
		return _cbData;
	}

	BOOL Start(VBmp* vid, ULONG biCompression, ULONG i, ULONG j);
};

BOOL StartClient(CClient** ppcln, HWND hwndDlg, HWND hwnd);