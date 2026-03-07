#include "stdafx.h"

_NT_BEGIN
#include "log.h"
#include "https.h"
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

ULONG CClient::GetConnectData(void** ppSendBuffer)
{
	*ppSendBuffer = &_crc;
	return sizeof(_crc);
}

BOOL CClient::OnConnect(ULONG dwError)
{
	DbgPrint("%hs<%p>(%x)\r\n", __FUNCTION__, this, dwError);
	PostMessageW(_hwndDlg, VBmp::e_connected, dwError, 0);
	return !dwError;
}

void CClient::OnDisconnect()
{
	DbgPrint("%hs<%p>\r\n", __FUNCTION__, this);
	if (_vid) _vid->Release(), _vid = 0;
	PostMessageW(_hwndDlg, VBmp::e_disconnected, 0, 0);
	__super::OnDisconnect();
}

BOOL CClient::OnRecv(PSTR Buffer, ULONG cb)
{
	if (InterlockedExchange64(&_crc, 0))
	{
		if (InitSession((PBYTE)Buffer, cb))
		{
			DbgPrint("client: !! InitSession !!\r\n");
			PostMessageW(_hwndDlg, VBmp::e_connected, 0, 1);
			return TRUE;
		}

		return FALSE;
	}

	return __super::OnRecv(Buffer, cb);
}

BOOL CClient::OnUserData(ULONG type, PBYTE pb, ULONG cb)
{
	if ('H264' != type) DbgPrint("%hs<%p>(%.4hs %x)\r\n", __FUNCTION__, this, &type, cb);

	switch (type)
	{
	case 'PING':
		SetDisconnectTime(15000);
		return TRUE;

	case 'fmts':
		if (ValidateFormats(pb, cb, TRUE))
		{
			return PostMessageW(_hwndDlg, VBmp::e_list, cb, (LPARAM)pb);
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
		return TRUE;

	case 'H264':
		_cbData += cb;
		switch (_biCompression)
		{
		case '2YUY':
			if (S_OK == OnFrame((PULONG)_vid->GetBits(), pb, cb))
			{
				PostMessageW(_hwnd, VBmp::e_update, 0, 0);
				return TRUE;
			}
			break;
		}
		return FALSE;
	}

	return FALSE;
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

	SELECT s = { i, j};
	if (SendUserData('strt', &s, sizeof(s)))
	{
		SendMessageW(_hwnd, VBmp::e_set, 0, 0);
		_vid = 0;
		vid->Release();
		return FALSE;
	}
	return TRUE;
}

NTSTATUS GetKeyCrc(BCRYPT_KEY_HANDLE hKey, PULONGLONG pcrc)
{
	NTSTATUS status;
	PBYTE pb = 0;
	ULONG cb = 0;
	while(0 <= (status = BCryptExportKey(hKey, 0, BCRYPT_RSAPUBLIC_BLOB, pb, cb, &cb, 0)))
	{
		if (pb)
		{
			*pcrc = RtlCrc64(pb, cb, 0);
			break;
		}

		pb = (PBYTE)alloca(cb);
	}

	return status;
}

NTSTATUS CClient::OpenKey(PCWSTR name)
{
	PBYTE pb;
	ULONG cb;

	if (32 != wcslen(name))
	{
		return STATUS_OBJECT_NAME_INVALID;
	}

	NTSTATUS status = ReadFromFile(name, &pb, &cb, 0x80, 0x800);
	if (0 <= status)
	{
		BCRYPT_KEY_HANDLE hKey = 0;
		BCRYPT_ALG_HANDLE hAlgorithm;
		if (0 <= (status = BCryptOpenAlgorithmProvider(&hAlgorithm, BCRYPT_RSA_ALGORITHM, 0, 0)))
		{
			status = BCryptImportKeyPair(hAlgorithm, 0, BCRYPT_RSAPRIVATE_BLOB, &hKey, pb, cb, 0);
			BCryptCloseAlgorithmProvider(hAlgorithm, 0);
		}

		delete [] pb;

		if (0 <= status)
		{
			ULONGLONG crc;
			if (0 <= (status = GetKeyCrc(hKey, &crc)))
			{
				if (_wcstoui64(name + 16, const_cast<WCHAR**>(&name), 16) == crc && !*name)
				{
					SetKey(hKey), _crc = crc;

					return STATUS_SUCCESS;
				}

				status = STATUS_BAD_KEY;
			}

			BCryptDestroyKey(hKey);
		}
	}

	return status;
}

BOOL StartClient(CClient** ppcln, HWND hwndDlg, HWND hwnd)
{
	if (CClient* pcln = new CClient(hwndDlg, hwnd))
	{
		if (!pcln->Create(1920*1080*4*2))//
		{
			*ppcln = pcln;

			return TRUE;
		}
		pcln->Release();
	}

	return FALSE;
}

_NT_END