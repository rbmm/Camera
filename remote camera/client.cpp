#include "stdafx.h"

_NT_BEGIN
#include "log.h"
#include "video.h"
#include "file.h"
#include "utils.h"
#include "server.h"
#include "client.h"

BOOL ValidateFormats(PVOID buf, ULONG cb, BOOL bPrint)
{
	if ( (cb & (__alignof(WCHAR) - 1)) || !(cb & (__alignof(ULONG) - 1)) )
	{
		return FALSE;
	}
	union {
		ULONG_PTR up;
		PWSTR name;
		VFI* pf;
		PVOID pv;
		PULONG pu;
	};

	pv = buf;

	up += cb - sizeof(WCHAR) - sizeof(ULONG);

	if (*pu++ || *name)
	{
		return FALSE;
	}

	PVOID end = name;

	pv = buf;

	ULONG m = 0;
	while (*name)
	{
		if (bPrint) DbgPrint("%x: \"%ws\"\r\n", m, name);

		name += wcslen(name) + 1;

		up = (up + __alignof(VFI) - 1) & ~(__alignof(VFI) - 1);

		ULONG n = 0;
		while (pf < end && pf->biCompression)
		{
			if (bPrint) DbgPrint("\t%x: %.4hs [%u x %u] %u FPS\r\n", n, &pf->biCompression, pf->biWidth, pf->biHeight, pf->FPS);

			pf++, n++;
		}

		if (pf >= end)
		{
			return FALSE;
		}

		pu++, m++;
	}

	return TRUE;
}

CClient::~CClient()
{
	if (_vid) _vid->Release();
	//PostMessageW(_hwnd, VBmp::e_set, 0, 0);
	DbgPrint("%hs<%p>\r\n", __FUNCTION__, this);
}

NTSTATUS CClient::OnConnect()
{
	DbgPrint("%hs<%p>\r\n", __FUNCTION__, this);
	PostMessageW(_hwndDlg, VBmp::e_connected, 0, 1);
	return 0;
}

void CClient::OnDisconnect()
{
	DbgPrint("%hs<%p>\r\n", __FUNCTION__, this);
	if (_vid) _vid->Release(), _vid = 0;
	PostMessageW(_hwndDlg, VBmp::e_disconnected, 0, 0);
}

NTSTATUS CClient::OnUserData(ULONG type, PBYTE pb, ULONG cb)
{
	if ('H264' != type) DbgPrint("%hs<%p>(%.4hs %x)\r\n", __FUNCTION__, this, &type, cb);

	switch (type)
	{
	case 'fmts':
		if (ValidateFormats(pb, cb, TRUE))
		{
			return PostMessageW(_hwndDlg, VBmp::e_list, cb, (LPARAM)pb) ? S_OK : E_FAIL;
		}
		break;

	case 'stop':
		//PostMessageW(_hwnd, VBmp::e_set, 0, 0);
		PostMessageW(_hwndDlg, VBmp::e_set, 0, 0);
		if (_vid)
		{
			_vid->Release();
			_vid = 0;
		}
		return S_OK;

	case 'H264':
		_cbData += cb;
		switch (_biCompression)
		{
		case '2YUY':
			if (S_OK == OnFrame((PULONG)_vid->GetBits(), pb, cb))
			{
				return PostMessageW(_hwnd, VBmp::e_update, 0, 0) ? S_OK : E_FAIL;
			}
			return S_OK;
		}
	}

	return STATUS_BAD_DATA;
}

BOOL CClient::Start(VBmp* vid, ULONG biCompression, ULONG i, ULONG j)
{
	if (_vid)
	{
		_vid->Release();
	}
	_vid = vid;
	vid->AddRef();
	_biCompression = biCompression;
	_cbData = 0;

	SendMessageW(_hwnd, VBmp::e_set, 0, (LPARAM)vid);

	SELECT s = { i, j };
	if (SendUserData('strt', (PBYTE)&s, sizeof(s)))
	{
		SendMessageW(_hwnd, VBmp::e_set, 0, 0);
		_vid = 0;
		vid->Release();
		return FALSE;
	}
	return TRUE;
}

NTSTATUS CClient::Accept(PSOCKADDR_INET /*psi*/)
{
	return STATUS_CONNECTION_REFUSED;
}

class TClientServerR : public CClientServerR
{
	virtual Endpoint* CreateEndpoint()
	{
		return 0;
	}
public:
	using CClientServerR::CClientServerR;
};

BOOL CreateClient(CClient** ppcln, HWND hwndDlg, HWND hwnd)
{
	BOOL fOk = FALSE;
	if (CClientServerR* c = new TClientServerR(0, 0, "client"))
	{
		SOCKADDR_INET sa{};
		sa.Ipv4.sin_family = AF_INET;

		if (c->Start(4, &sa))
		{
			if (CClient* pcln = new CClient(c, hwndDlg, hwnd))
			{
				*ppcln = pcln;

				fOk = TRUE;
			}
		}

		c->Release();
	}
	
	return fOk;
}

_NT_END