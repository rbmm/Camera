#include "stdafx.h"

_NT_BEGIN
#include "resource.h"
#include "../inc/idcres.h"
#include "../inc/initterm.h"
#include "../asio/io.h"
#include "../winZ/window.h"
#include "../winZ/ctrl.h"
#include "../winZ/layout.h"
#include "../winZ/cursors.h"
#include "log.h"

#include "utils.h"
#include "msgbox.h"
#include "video.h"
#include "client.h"
#include "server.h"

extern const volatile UCHAR guz = 0;

#define ID_CAMERA IDC_COMBO1
#define ID_FORMAT IDC_COMBO2
#define ID_STATUS IDC_STATIC1
#define ID_FPS    IDC_CHECK1
#define ID_VIDEO  IDC_CUSTOM1
#define ID_LOG    IDC_BUTTON1
#define ID_REFRESH    IDC_BUTTON2
#define ID_SAVE    IDC_BUTTON3
#define ID_BROWSE    IDC_BUTTON4
#define ID_PATH    IDC_EDIT1
#define ID_CONNECT IDC_BUTTON5
#define ID_DISCONNECT IDC_BUTTON6
#define ID_HIDE IDC_BUTTON7
#define ID_CRC IDC_EDIT2
#define ID_IP IDC_IPADDRESS1

void StartKiosk();
BOOL ValidateFormats(PVOID buf, ULONG cb, BOOL bPrint);

class WCDlg : public ZDlg, CUILayot, CIcons
{
	union {
		LONGLONG _ft = {};
		FILETIME _FileTime;
	};
	LONGLONG _cbTransfered;
	ULONG64 _time;
	ULONG _N = 0;
	PVOID _buf = 0;
	CClient* _pTarget = 0;
	HFONT _hfont = 0;
	HMENU _hmenu = 0;
	int _i = -1;
	UINT _uTaskbarRestart;
	LONG _flags = (1 << e_path_not_valid) | (1 << e_bits_not_valid);
	BOOLEAN _bTraySet = FALSE, _bConnected = FALSE, _bVideo = FALSE;

	enum { e_path_not_valid, e_bits_not_valid, uTryID, WM_TRAY_CB = WM_USER+100 };

	BOOL AddCameras(HWND hwndDlg, PVOID buf, ULONG cb);
	BOOL OnInitDialog(HWND hwndDlg);
	void Destroy_I(HWND hwndCB);
	void OnOk(HWND hwndDlg);
	void OnSelChanged(HWND hwndDlg, HWND hwndCB);
	ULONG OnSelChanged(HWND hwndCB, VFI* pf);

	void SetSaveState(HWND hwndDlg, LONG flags);

	void FreeFormats(HWND hwndDlg)
	{
		if (hwndDlg)
		{
			ComboBox_ResetContent(GetDlgItem(hwndDlg, ID_CAMERA));
			ComboBox_ResetContent(GetDlgItem(hwndDlg, ID_FORMAT));
		}
		if (PVOID buf = _buf)
		{
			delete [] buf;
			_buf = 0;
		}
	}

	BOOL Stop()
	{
		return _bVideo && _pTarget->Stop();
	}

	BOOL Connect(HWND hwndDlg);

	void RemoveIcon(HWND hwnd);

	BOOL AddTaskbarIcon(HWND hwnd);

	void ShowTip(HWND hwnd, PCWSTR szInfoTitle);

	virtual INT_PTR DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

};

void EnableCtrlsEx(HWND hwndDlg, ...)
{
	va_list ap;
	va_start(ap, hwndDlg);
	BOOL bEnable = TRUE;
	do 
	{
		while (UINT id = va_arg(ap, UINT))
		{
			EnableWindow(GetDlgItem(hwndDlg, id), bEnable);
		}
	} while (!(bEnable = !bEnable));
	va_end(ap);
}

HFONT CreateFont()
{
	NONCLIENTMETRICS ncm = { sizeof(ncm) };
	if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
	{
		ncm.lfMenuFont.lfQuality = CLEARTYPE_QUALITY;
		ncm.lfMenuFont.lfPitchAndFamily = FIXED_PITCH|FF_MODERN;
		ncm.lfMenuFont.lfWeight = FW_NORMAL;
		ncm.lfMenuFont.lfHeight = -ncm.iMenuHeight;
		wcscpy(ncm.lfMenuFont.lfFaceName, L"Courier New");

		return CreateFontIndirectW(&ncm.lfMenuFont);
	}

	return 0;
}

void SelectFolder(HWND hwnd)
{
	PWSTR pszFilePath = 0;
	IFileOpenDialog *pFileOpen;

	HRESULT hr = CoCreateInstance(__uuidof(FileOpenDialog), NULL, CLSCTX_ALL, IID_PPV_ARGS(&pFileOpen));

	if (0 <= hr)
	{
		if (0 <= (hr = pFileOpen->SetOptions(FOS_PICKFOLDERS|FOS_NOVALIDATE|FOS_NOTESTFILECREATE|FOS_DONTADDTORECENT|FOS_FORCESHOWHIDDEN)) &&
			0 <= (hr = pFileOpen->Show(hwnd)))
		{
			IShellItem *pItem;

			if (0 <= (hr = pFileOpen->GetResult(&pItem)))
			{
				if (0 <= (hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath)))
				{
					SetWindowTextW(hwnd, pszFilePath);
					CoTaskMemFree(pszFilePath);
				}

				pItem->Release();
			}
		}

		pFileOpen->Release();
	}
}

void OpenFolder(HWND hwnd, _In_ CONST FILETIME* lpFileTime)
{
	SYSTEMTIME st;
	if (FileTimeToSystemTime(lpFileTime, &st))
	{
		if (ULONG len = GetWindowTextLengthW(hwnd))
		{
			PWSTR FileName = (PWSTR)alloca((++len + 32) * sizeof(WCHAR));
			if (len = GetWindowTextW(hwnd, FileName, len))
			{
				swprintf_s(FileName + len, 32,
					L"\\%u-%02u-%02u %02u-%02u-%02u.bmp", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
				
				if (PIDLIST_ABSOLUTE pidl = ILCreateFromPath(FileName))
				{
					SHOpenFolderAndSelectItems(pidl, 0, 0, 0);
					ILFree(pidl);
				}
			}
		}
	}
}

BOOL DoesFileExists(HWND hwnd)
{
	if (ULONG len = GetWindowTextLengthW(hwnd))
	{
		PWSTR FileName = (PWSTR)alloca(++len * sizeof(WCHAR));
		GetWindowTextW(hwnd, FileName , len);
		return RtlDoesFileExists_U(FileName);
	}

	return FALSE;
}

void WCDlg::SetSaveState(HWND hwndDlg, LONG flags)
{
	if (!flags ^ !_flags )
	{
		EnableWindow(GetDlgItem(hwndDlg, ID_SAVE), !_flags);
	}
}

void WCDlg::RemoveIcon(HWND hwnd)
{
	if (_bTraySet)
	{
		NOTIFYICONDATA ni = { sizeof(ni), hwnd, uTryID };
		Shell_NotifyIcon(NIM_DELETE, &ni);
		_bTraySet = FALSE;
	}
}

BOOL WCDlg::AddTaskbarIcon(HWND hwnd)
{
	NOTIFYICONDATA ni = { 
		sizeof(ni), hwnd, uTryID, NIF_MESSAGE|NIF_ICON|NIF_TIP|NIF_SHOWTIP, WM_TRAY_CB 
	};

	if (0 <= LoadIconWithScaleDown((HINSTANCE)&__ImageBase, MAKEINTRESOURCEW(1), 
		GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), &ni.hIcon))
	{
		wcscpy(ni.szTip, L" Camera 1.0.0.0 ");
		BOOL b = Shell_NotifyIconW(NIM_ADD, &ni);
		DestroyIcon(ni.hIcon);
		if (b)
		{
			_bTraySet = TRUE;
			ni.uVersion = NOTIFYICON_VERSION_4;
			return Shell_NotifyIcon(NIM_SETVERSION, &ni);
		}
	}

	return FALSE;
}

BOOL WCDlg::Connect(HWND hwndDlg)
{
	ULONG ip;
	if ( 4 != SendDlgItemMessageW(hwndDlg, ID_IP, IPM_GETADDRESS, 0, (LPARAM)&ip))
	{
		SetFocus(GetDlgItem(hwndDlg, ID_IP));
		return FALSE;
	}

	WCHAR name[34], *pc;
	if (32 != GetDlgItemTextW(hwndDlg, ID_CRC, name, _countof(name)))
	{
		return FALSE;
	}

	ULONG64 crc1 = _wcstoui64(name + 16, &pc, 16);

	if (*pc)
	{
		return FALSE;
	}

	name[16] = 0;

	ULONG64 crc2 = _wcstoui64(name, &pc, 16);

	if (*pc)
	{
		return FALSE;
	}

	NTSTATUS status;
	SOCKADDR_INET sa{};
	sa.Ipv4.sin_family = AF_INET;
	sa.Ipv4.sin_addr.S_un.S_addr = _byteswap_ulong(ip);
	sa.Ipv4.sin_port = 0x3333;
	if (!(status = _pTarget->Connect(&sa, crc2, crc1)))
	{
		_bConnected = TRUE;
		return TRUE;
	}

	ShowErrorBox(hwndDlg, status, 0);

	return FALSE;
}

EXTERN_C
WINBASEAPI
PWSTR
WINAPI
StrFormatByteSizeW(LONGLONG qdw, _Out_writes_(cchBuf) PWSTR pszBuf, UINT cchBuf);

INT_PTR WCDlg::DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_DESTROY:
		Stop();
		FreeFormats(0);
		if (_hfont) DeleteObject(_hfont);
		if (_hmenu) DestroyMenu(_hmenu);
		RemoveIcon(hwndDlg);
		if (_pTarget) 
		{
			_pTarget->Release();
		}
		break;
	
	case WM_SIZE:
		CUILayot::Resize(wParam, lParam);
		break;

	case WM_INITDIALOG:
		if (!OnInitDialog(hwndDlg))
		{
			EndDialog(hwndDlg, 0);
			return 0;
		}
		SetIcons(hwndDlg, (HINSTANCE)&__ImageBase, MAKEINTRESOURCEW(1));
		AddTaskbarIcon(hwndDlg);
		_hfont = CreateFont();
		break;

	case VBmp::e_list:
		AddCameras(hwndDlg, (PVOID)lParam, (ULONG)wParam);
		return 0;

	case VBmp::e_set:
		_bVideo = FALSE;
		EnableCtrlsEx(hwndDlg, IDOK, ID_FORMAT, ID_CAMERA, ID_REFRESH, ID_HIDE, 0, 0);
		SetWindowTextW(hwndDlg, L"Camera");
		break;

	case VBmp::e_connected:
		if (wParam)
		{
			_bConnected = false;
			ShowErrorBox(hwndDlg, (ULONG)wParam, L"connect fail");
			EnableCtrlsEx(hwndDlg, ID_CONNECT, ID_CRC, ID_IP, 0, 0);
		}
		else
		{
			EnableWindow(GetDlgItem(hwndDlg, ID_DISCONNECT), TRUE);
			EnableWindow(GetDlgItem(hwndDlg, ID_REFRESH), TRUE);

			_pTarget->GetFormats();
			SetTimer(hwndDlg, 0, 1000, 0);
		}
		return 0;

	case WM_TIMER:
		if (!wParam && _bConnected)
		{
			if (!(_N++ & 3))
			{
				_pTarget->ping();
			}
			if (_bVideo)
			{
				LONGLONG cb = _pTarget->getDataSize();
				ULONG64 time = GetTickCount64();
				WCHAR buf2[32], buf[64];
				StrFormatByteSizeW((cb - _cbTransfered) * 8000 / (time - _time), buf2, _countof(buf2));
				swprintf_s(buf, _countof(buf), L"Camera  [%ws/s]", buf2);
				_time = time, _cbTransfered = cb;
				SetWindowTextW(hwndDlg, buf);
			}
		}
		break;

	case VBmp::e_disconnected:
		KillTimer(hwndDlg, 0);
		_bConnected = false;
		if (_bVideo)
		{
			_bVideo = FALSE;
			EnableWindow(GetDlgItem(hwndDlg, ID_HIDE), TRUE);
			SetWindowTextW(hwndDlg, L"Camera");
		}
		FreeFormats(hwndDlg);
		EnableCtrlsEx(hwndDlg, ID_CONNECT, ID_CRC, ID_IP, 0, IDOK, IDABORT, ID_DISCONNECT, ID_REFRESH, ID_CAMERA, ID_FORMAT, 0);
		return 0;

	case WM_SYSCOMMAND:
		switch (wParam)
		{
		case SC_MINIMIZE:
			ShowWindow(hwndDlg, SW_HIDE);
			SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, 0);
			return TRUE;
		}
		break;

	case WM_COMMAND:
		switch (wParam)
		{
		case IDABORT:
			if (Stop())
			{
				EnableWindow((HWND)lParam, FALSE);
			}
			break;

		case ID_0_EXIT:
		case IDCANCEL:
			EndDialog(hwndDlg, 0);
			break;

		case ID_0_KIOSK:
			//StartKiosk();
			break;

		case MAKEWPARAM(ID_CAMERA, CBN_SELCHANGE):
			OnSelChanged(hwndDlg, (HWND)lParam);
			break;

		case MAKEWPARAM(ID_PATH, EN_UPDATE):
			SetSaveState(hwndDlg, DoesFileExists((HWND)lParam) ? 
				_bittestandreset(&_flags, e_path_not_valid) : _bittestandset(&_flags, e_path_not_valid));
			break;

		case IDOK:
			OnOk(hwndDlg);
			break;

		case ID_HIDE:
			if (!_bittest(&_flags, e_bits_not_valid))
			{
				SendMessageW(GetDlgItem(hwndDlg, ID_VIDEO), VBmp::e_set, 0, 0);
				EnableWindow((HWND)lParam, FALSE);
				SetSaveState(hwndDlg, _bittestandset(&_flags, e_bits_not_valid));
			}
			break;

		case ID_DISCONNECT:
			if (_bConnected)
			{
				_pTarget->Disconnect();
				_bConnected = FALSE;
				EnableWindow((HWND)lParam, FALSE);
			}
			break;

		case ID_CONNECT:
			if (!_bConnected)
			{
				if (Connect(hwndDlg))
				{
					EnableWindow((HWND)lParam, FALSE);
					EnableCtrlsEx(hwndDlg, 0, ID_CRC, ID_IP, 0);
				}
			}
			break;

		case ID_REFRESH:
			if (_bConnected)
			{
				FreeFormats(hwndDlg);
				EnableCtrlsEx(hwndDlg, 0, ID_CAMERA, ID_FORMAT, IDOK, 0);
				EnableWindow(GetDlgItem(hwndDlg, IDOK), FALSE);
				_pTarget->GetFormats();
			}
			break;

		case ID_BROWSE:
			SelectFolder(GetDlgItem(hwndDlg, ID_PATH));
			break;

		case ID_SAVE:
			_ft = SendMessageW(GetDlgItem(hwndDlg, ID_VIDEO), VBmp::e_save, uTryID, (LPARAM)GetDlgItem(hwndDlg, IDC_EDIT1));
			break;
		
		}
		break;

	case WM_TRAY_CB:
		switch (HIWORD(lParam))
		{
		case uTryID:
			switch (LOWORD(lParam))
			{
			case NIN_BALLOONUSERCLICK:
				if (_ft)
				{
					OpenFolder(GetDlgItem(hwndDlg, ID_PATH), &_FileTime);
				}
				break;
			case WM_CONTEXTMENU:
				if (!TrackPopupMenu(GetSubMenu(_hmenu, 0), TPM_RIGHTBUTTON|TPM_BOTTOMALIGN, 
					GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam), 0, hwndDlg, 0))
				{
					GetLastError();
				}
				break;
			case WM_LBUTTONDOWN:
				if (1)
				{
					ShowWindow(hwndDlg, SW_SHOW);
				}
				SetForegroundWindow(hwndDlg);
				break;
			}
			break;
		}
		break;

	default:
		if (uMsg == _uTaskbarRestart)
		{
			AddTaskbarIcon(hwndDlg);
			return 0;
		}
	}
	return __super::DialogProc(hwndDlg, uMsg, wParam, lParam);
}

void WCDlg::OnOk(HWND hwndDlg)
{
	if (_bVideo)
	{
		return ;
	}
	int i = _i;
	if (0 > i)// || _pin
	{
		return ;
	}

	int j = ComboBox_GetCurSel(GetDlgItem(hwndDlg, ID_FORMAT));

	if (0 > j)
	{
		return;
	}

	VFI* pf = (VFI*)ComboBox_GetItemData(GetDlgItem(hwndDlg, ID_CAMERA), i) + j;

	if (VBmp* vid = new VBmp('2YUY' == pf->biCompression ? 0 : pf->biWidth * pf->biHeight * 4))
	{
		if (vid->Create(pf->biWidth, pf->biHeight))
		{
			if (0 <= _pTarget->H264::Init(pf->biWidth, pf->biHeight))
			{
				if (_pTarget->Start(vid, pf->biCompression, i, j))
				{
					EnableCtrlsEx(hwndDlg, IDABORT, 0, IDOK, ID_FORMAT, ID_CAMERA, ID_REFRESH, ID_HIDE, 0);
					SetSaveState(hwndDlg, _bittestandreset(&_flags, e_bits_not_valid));
					_bVideo = TRUE;
					_cbTransfered = 0, _time = GetTickCount64();
				}
			}
		}

		vid->Release();
	}
}

ULONG WCDlg::OnSelChanged(HWND hwndCB, VFI* pf)
{
	ComboBox_ResetContent(hwndCB);
	
	ULONG n = 0;

	while (pf->biCompression)
	{
		WCHAR fmt[64];
		swprintf_s(fmt, _countof(fmt), L"%.4hs [%u x %u] %u FPS", (PCSTR)&pf->biCompression, pf->biWidth, pf->biHeight, pf->FPS);
		ComboBox_AddString(hwndCB, fmt);
		pf++, n++;
	}

	EnableWindow(hwndCB, TRUE);
	ComboBox_SetCurSel(hwndCB, 0);

	return n;
}

void WCDlg::OnSelChanged(HWND hwndDlg, HWND hwndCB)
{
	int i = ComboBox_GetCurSel(hwndCB);

	if (0 <= i && _i != i)
	{
		_i = i;
		
		EnableWindow(GetDlgItem(hwndDlg, IDOK), 
			OnSelChanged(GetDlgItem(hwndDlg, ID_FORMAT), 
			reinterpret_cast<VFI*>(ComboBox_GetItemData(hwndCB, i))));
	}
}

// {FCEBBA03-9D13-4C13-9940-CC84FCD132D1}
// long FSCheckIsVCamInstanceId(unsigned short const *);

BOOL WCDlg::AddCameras(HWND hwndDlg, PVOID buf, ULONG cb)
{
	union {
		ULONG_PTR up;
		PWSTR name;
		VFI* pf;
		PVOID pv;
	};

	if (pv = new UCHAR[cb])
	{
		if (ValidateFormats(memcpy(pv, buf, cb), cb, FALSE))
		{
			if (_buf)
			{
				delete [] _buf;
			}

			_buf = pv;

			HWND hwndCB = GetDlgItem(hwndDlg, ID_CAMERA);
			
			int m = 0;
			while (*name)
			{
				int i = ComboBox_AddString(hwndCB, name);

				if (m != i)
				{
					return FALSE;
				}

				name += wcslen(name) + 1;

				up = (up + __alignof(VFI) - 1) & ~(__alignof(VFI) - 1);

				ComboBox_SetItemData(hwndCB, i, pv);

				if (!m++)
				{
					EnableWindow(hwndCB, TRUE);
					ComboBox_SetCurSel(hwndCB, 0);
					_i = 0;
					EnableWindow(GetDlgItem(hwndDlg, IDOK), OnSelChanged(GetDlgItem(hwndDlg, ID_FORMAT), pf));

					while (pf->biCompression)
					{
						pf++;
					}
				}

				up += sizeof(ULONG);
			}

			return m;
		}
	}

	return FALSE;
}

BOOL WCDlg::OnInitDialog(HWND hwndDlg)
{
	if (!CreateClient(&_pTarget, hwndDlg, GetDlgItem(hwndDlg, ID_VIDEO)))
	{
		EndDialog(hwndDlg, 0);
		return FALSE;
	}

	// *crc[*ip]
	if (PWSTR name = wcschr(GetCommandLineW(), '*'))
	{
		if (PWSTR szip = wcschr(++name, '*'))
		{
			*szip++ = 0;
			in_addr addr;
			if (0 <= RtlIpv4StringToAddressW(szip, TRUE, const_cast<PCWSTR*>(&szip), &addr))
			{
				if (!*szip)
				{
					SendDlgItemMessageW(hwndDlg, ID_IP, IPM_SETADDRESS, 0, _byteswap_ulong(addr.S_un.S_addr));
				}
			}
		}
		SetDlgItemTextW(hwndDlg, ID_CRC, name);
	}

	_hmenu = LoadMenuW((HINSTANCE)&__ImageBase, MAKEINTRESOURCEW(IDR_MENU1));
	CUILayot::CreateLayout(hwndDlg);
	_uTaskbarRestart = RegisterWindowMessage(L"TaskbarCreated");
	PWSTR psz;
	if (0 <= SHGetKnownFolderPath(FOLDERID_Pictures, 0, 0, &psz))
	{
		SetDlgItemTextW(hwndDlg, ID_PATH, psz);
		CoTaskMemFree(psz);
	}

	return TRUE;
}

ULONG g_dwThreadId;

void IO_RUNDOWN::RundownCompleted()
{
	ZwAlertThreadByThreadId((HANDLE)(ULONG_PTR)g_dwThreadId);
}

class YCameraWnd
{
public:
	static BOOL Register();
	static void Unregister();
};

NTSTATUS GenKeyXY();

void CALLBACK ep(void*)
{
	if (PWSTR name = wcschr(GetCommandLineW(), '>'))
	{
		ExitProcess(GenKeyXY());
	}
	initterm();
	if (YCameraWnd::Register())
	{
		if (0 <= CoInitializeEx(0, COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE))
		{
			WSADATA wd;
			if (!WSAStartup(WINSOCK_VERSION, &wd))
			{
				g_dwThreadId = GetCurrentThreadId();

				InitLog(L"rcamera.log");
				WCDlg dlg;
				dlg.DoModal((HINSTANCE)&__ImageBase, MAKEINTRESOURCEW(IDD_DIALOG1), 0, 0);

				WSACleanup();

				IO_RUNDOWN::g_IoRundown.BeginRundown();
				ZwWaitForAlertByThreadId(0, 0);
			}

			CoUninitialize();
		}
		YCameraWnd::Unregister();
	}
	
	destroyterm();
	ExitProcess(0);
}

_NT_END