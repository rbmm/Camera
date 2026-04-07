#pragma once

NTSTATUS GetKeyCrc(BCRYPT_KEY_HANDLE hKey, PULONGLONG pcrc);

#include "video.h"
#include "H264.h"
#include "TUDP.h"

class CClient : public Endpoint, public H264
{
	LONGLONG _crc = 0, _cbData;
	HWND _hwnd, _hwndDlg;
	VBmp* _vid = 0;
	ULONG _biCompression, _biWidth, _biHeight;

	virtual NTSTATUS Accept(PSOCKADDR_INET /*psi*/);

	virtual NTSTATUS OnConnect();

	virtual void OnDisconnect();

	virtual NTSTATUS OnUserData(ULONG type, PBYTE pb, ULONG cb);

	virtual ~CClient();

public:

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

	void OnStop()
	{
		DbgPrint("%hs<%p>\r\n", __FUNCTION__, this);
		SendUserData('stop');
	}

	CClient(CClientServerR* parent, HWND hwndDlg, HWND hwnd) : Endpoint(parent), _hwnd(hwnd), _hwndDlg(hwndDlg)
	{
	}
};

BOOL CreateClient(CClient** ppcln, HWND hwndDlg, HWND hwnd);