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
#include "dump.h"
#include "read.h"
#include "msgbox.h"
#include "video.h"

extern const volatile UCHAR guz = 0;

#define ID_CAMERA IDC_COMBO1
#define ID_FORMAT IDC_COMBO2
#define ID_STATUS IDC_STATIC1
#define ID_FPS    IDC_CHECK1
#define ID_VIDEO  IDC_CUSTOM1
#define ID_LOG    IDC_BUTTON1

struct KSMULTIPLE_ITEM_LIST
{
	KSMULTIPLE_ITEM_LIST* next;
	KSMULTIPLE_ITEM ksmi;

	static void Delete(KSMULTIPLE_ITEM_LIST* next)
	{
		KSMULTIPLE_ITEM_LIST* p;
		do 
		{
			p = next;
			next = p->next;
			HeapFree(GetProcessHeap(), 0, p);

		} while (next);
	}
};

struct Camera 
{
	PCWSTR pszItfName;
	KSMULTIPLE_ITEM_LIST* first;

	Camera(PCWSTR pszItfName, KSMULTIPLE_ITEM_LIST* first) : pszItfName(pszItfName), first(first)
	{
	}

	~Camera()
	{
		KSMULTIPLE_ITEM_LIST::Delete(first);
	}
};

class WCDlg : public ZDlg, CUILayot, CIcons
{
	PVOID _buf = 0;
	KsRead* _pin = 0;
	HFONT _hfont = 0;
	int _i = -1;
	LONG _nReadCount;

	enum { stat_timer = 1 };

	void AddCameras(HWND hwndDlg, GUID* InterfaceClassGuid);
	BOOL OnInitDialog(HWND hwndDlg);
	void OnDestroy(HWND hwndCB);
	void OnOk(HWND hwndDlg);
	void OnSelChanged(HWND hwndDlg, HWND hwndCB);
	ULONG OnSelChanged(HWND hwndCB, KSMULTIPLE_ITEM_LIST* pList);

	BOOL Stop()
	{
		if (_pin)
		{
			_pin->Stop();
			_pin->Release();
			_pin = 0;
			return TRUE;
		}

		return FALSE;
	}

	void ShowStat(HWND hwndDlg)
	{
		if (KsRead* pin = _pin)
		{
			LONGLONG Bytes;
			ULONG nReadCount = pin->GetStat(&Bytes);
			WCHAR sz[0x40];
			swprintf_s(sz, _countof(sz), L"%u FPS [%I64x]", nReadCount - _nReadCount, Bytes);
			SetDlgItemTextW(hwndDlg, IDC_STATIC1, sz);
			_nReadCount = nReadCount;
		}
	}

	virtual INT_PTR DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

	void EnableCtrls(HWND hwndDlg, BOOL bEnable)
	{
		static const UINT _s_ids[] = { IDCANCEL, IDOK, ID_FPS, ID_FORMAT, ID_CAMERA };
		ULONG n = _countof(_s_ids);
		do 
		{
			if (!--n) bEnable = !bEnable;
			EnableWindow(GetDlgItem(hwndDlg, _s_ids[n]), bEnable);
		} while (n);
	}
};

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

INT_PTR WCDlg::DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_DESTROY:
		Stop();
		OnDestroy(GetDlgItem(hwndDlg, ID_CAMERA));
		if (_hfont) DeleteObject(_hfont);
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
		_hfont = CreateFont();
		break;

	case WM_CLOSE:
		EndDialog(hwndDlg, 0);
		return 0;

	case WM_TIMER:
		switch (wParam)
		{
		case stat_timer:
			ShowStat(hwndDlg);
			break;
		}
		break;

	case WM_COMMAND:
		switch (wParam)
		{
		case IDCANCEL:
			if (Stop())
			{
				KillTimer(hwndDlg, stat_timer);
				EnableCtrls(hwndDlg, TRUE);
				SetDlgItemTextW(hwndDlg, IDC_STATIC1, 0);
			}
			break;

		case MAKEWPARAM(ID_CAMERA, CBN_SELCHANGE):
			OnSelChanged(hwndDlg, (HWND)lParam);
			break;

		case IDOK:
			OnOk(hwndDlg);
			break;
		
		case ID_LOG:
			if (HWND hwnd = CreateWindowExW(0, WC_EDIT, L" [LOG] ", WS_OVERLAPPEDWINDOW|WS_VSCROLL|WS_HSCROLL|ES_MULTILINE,
				CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwndDlg, 0, 0, 0))
			{
				ULONG n = 8;
				SendMessage(hwnd, EM_SETTABSTOPS, 1, (LPARAM)&n);
				SendMessage(hwnd, EM_LIMITTEXT, MAXLONG, 0);
				if (_hfont)
				{
					SendMessage(hwnd, WM_SETFONT, (WPARAM)_hfont, 0);
				}
				_G_log >> hwnd;
				_G_log.Init(0x10000);
				ShowWindow(hwnd, SW_SHOWNORMAL);
			}
			break;
		}
		break;
	}
	return __super::DialogProc(hwndDlg, uMsg, wParam, lParam);
}

void WCDlg::OnOk(HWND hwndDlg)
{
	int i = _i;
	if (0 > i || _pin)
	{
		return ;
	}

	PCWSTR pszItfName = reinterpret_cast<Camera*>(ComboBox_GetItemData(GetDlgItem(hwndDlg, ID_CAMERA), i))->pszItfName;
	
	i = ComboBox_GetCurSel(GetDlgItem(hwndDlg, ID_FORMAT));
	
	if (0 > i)
	{
		return ;
	}

	PKS_DATARANGE_VIDEO pDRVideo = reinterpret_cast<PKS_DATARANGE_VIDEO>(ComboBox_GetItemData(GetDlgItem(hwndDlg, ID_FORMAT), i));
	
	pDRVideo->VideoInfoHeader.AvgTimePerFrame = IsDlgButtonChecked(hwndDlg, ID_FPS) == BST_CHECKED ? 
		pDRVideo->ConfigCaps.MaxFrameInterval : pDRVideo->ConfigCaps.MinFrameInterval;

	NTSTATUS status;

	if (HANDLE hFile = fixH(CreateFileW(pszItfName, 0, 0, 0, OPEN_EXISTING, 0, 0)))
	{
		status = STATUS_NO_MEMORY;

		KsRead* pin = 0;

		HWND hwnd = GetDlgItem(hwndDlg, ID_VIDEO);

		if (VBmp* vid = new VBmp(pDRVideo->bFixedSizeSamples ? 0 : pDRVideo->VideoInfoHeader.bmiHeader.biSizeImage))
		{
			if (vid->Create(pDRVideo->VideoInfoHeader.bmiHeader.biWidth, pDRVideo->VideoInfoHeader.bmiHeader.biHeight))
			{
				pin = new KsRead(hwnd, vid);
			}

			vid->Release();
		}

		if (pin)
		{
			status = pin->Create(hFile, pDRVideo);
		}

		NtClose(hFile);

		if (0 <= status)
		{
			if (0 <= (status = pin->SetState(KSSTATE_RUN)))
			{
				EnableCtrls(hwndDlg, FALSE);

				_nReadCount = 0;

				SetTimer(hwndDlg, stat_timer, 1000, 0);
				_pin = pin;
				pin->Start();
				return ;
			}
			pin->Release();
		}
	}
	else
	{
		status = RtlGetLastNtStatus();
	}

	ShowErrorBox(hwndDlg, HRESULT_FROM_NT(status), 0);
}

void WCDlg::OnDestroy(HWND hwndCB)
{
	if (PVOID buf = _buf)
	{
		LocalFree(buf);
	}
	
	int i = ComboBox_GetCount(hwndCB);

	if (0 < i)
	{
		do 
		{
			delete (Camera*)ComboBox_GetItemData(hwndCB, --i);
		} while (i);
	}
}

ULONG WCDlg::OnSelChanged(HWND hwndCB, KSMULTIPLE_ITEM_LIST* pList)
{
	ComboBox_ResetContent(hwndCB);
	
	ULONG n = 0;

	do 
	{
		ULONG Count = pList->ksmi.Count;
		union {
			PVOID pv;
			PBYTE pb;
			PKSDATARANGE pVideoDataRanges;
			PKS_DATARANGE_VIDEO pDRVideo;
		};

		pv = &pList->ksmi + 1;
		ULONG FormatSize;
		
		do 
		{
			FormatSize = ((pVideoDataRanges->FormatSize + __alignof(KSDATARANGE)-1) & ~(__alignof(KSDATARANGE)-1));

			if (pVideoDataRanges->FormatSize >= sizeof(KS_DATARANGE_VIDEO) &&
				pVideoDataRanges->Specifier == KSDATAFORMAT_SPECIFIER_VIDEOINFO &&
				pVideoDataRanges->MajorFormat == KSDATAFORMAT_TYPE_VIDEO &&
				IsCorresponds(&pDRVideo->ConfigCaps, &pDRVideo->VideoInfoHeader))
			{
				WCHAR sz[64];
				ULONG biWidth = pDRVideo->VideoInfoHeader.bmiHeader.biWidth;
				ULONG biHeight = pDRVideo->VideoInfoHeader.bmiHeader.biHeight;
				swprintf_s(sz, _countof(sz), L"%.4hs [%u x %u] %u..%u..%u FPS %c", 
					(PCSTR)&pDRVideo->VideoInfoHeader.bmiHeader.biCompression,
					biWidth, biHeight,
					(ULONG)(10000000/pDRVideo->ConfigCaps.MaxFrameInterval),
					(ULONG)(10000000/pDRVideo->VideoInfoHeader.AvgTimePerFrame),
					(ULONG)(10000000/pDRVideo->ConfigCaps.MinFrameInterval),
					pDRVideo->bFixedSizeSamples ? 'F' : 'V');

				int i = ComboBox_AddString(hwndCB, sz);

				if (0 <= i)
				{
					n++;
					ComboBox_SetItemData(hwndCB, i, pDRVideo);

					if (!i)
					{
						ComboBox_SetCurSel(hwndCB, 0);
					}
				}
			}
		} while (pb += FormatSize, --Count);

	} while (pList = pList->next);

	return n;
}

void WCDlg::OnSelChanged(HWND hwndDlg, HWND hwndCB)
{
	int i = ComboBox_GetCurSel(hwndCB);
	if (0 <= i && _i != i)
	{
		_i = i;
		EnableWindow(GetDlgItem(hwndDlg, IDOK), 
			OnSelChanged(GetDlgItem(hwndDlg, ID_FORMAT), reinterpret_cast<Camera*>(ComboBox_GetItemData(hwndCB, i))->first));
	}
}

NTSTATUS ProcessP(HANDLE hFile, KSMULTIPLE_ITEM_LIST** pfirst)
{
	ULONG BytesReturned;
	KSP_PIN KsProperty = {{ KSPROPSETID_Pin, KSPROPERTY_PIN_CTYPES, KSPROPERTY_TYPE_GET }};
	
	// Clients use the KSPROPERTY_PIN_CTYPES property to determine how many pin types a KS filter supports.

	NTSTATUS status = SynchronousDeviceControl(hFile, IOCTL_KS_PROPERTY, 
		&KsProperty, sizeof(KSPROPERTY), &KsProperty.PinId, sizeof(KsProperty.PinId), &BytesReturned);

	if (0 > status)
	{
		DbgPrint("IOCTL_KS_PROPERTY=%x\r\n", status);
		return status;
	}

	if (!KsProperty.PinId)
	{
		return STATUS_NO_MORE_ENTRIES;
	}
	
	DbgPrint("dwPinCount=%x\r\n", KsProperty.PinId);

	union {
		GUID guidCategory;
		KSPIN_DATAFLOW dwFlowDirection;
	};

	char lb[32];
	do 
	{
		DbgPrint("PinId=%x\r\n", --KsProperty.PinId);

		// The KSPROPERTY_PIN_DATAFLOW property specifies the direction of data flow on pins instantiated by the pin factory

		KsProperty.Property.Id = KSPROPERTY_PIN_DATAFLOW;

		status = SynchronousDeviceControl(hFile, IOCTL_KS_PROPERTY, 
			&KsProperty, sizeof(KSP_PIN), &dwFlowDirection, sizeof(dwFlowDirection), &BytesReturned);

		DbgPrint("KSPIN_DATAFLOW=%hs [s=%x]\r\n", get(dwFlowDirection, lb, _countof(lb)), status);

		if (0 > status || KSPIN_DATAFLOW_OUT != dwFlowDirection)
		{
			continue;
		}

		KsProperty.Property.Id = KSPROPERTY_PIN_CATEGORY;

		status = SynchronousDeviceControl(hFile, IOCTL_KS_PROPERTY, 
			&KsProperty, sizeof(KSP_PIN), &guidCategory, sizeof(guidCategory), &BytesReturned);

		if (0 > status)
		{
			continue;
		}

		DumpGuid(&guidCategory, "PIN_CATEGORY=");

		if (PINNAME_VIDEO_CAPTURE != guidCategory)
		{
			continue;
		}

		// use the KSPROPERTY_PIN_DATARANGES property to determine the data ranges supported by pins instantiated by the pin factory
		// out: A KSMULTIPLE_ITEM structure, followed by a sequence of 64-bit aligned KSDATARANGE structures.

		KsProperty.Property.Id = KSPROPERTY_PIN_DATARANGES;

		union {
			PVOID buf;
			KSMULTIPLE_ITEM_LIST* pList;
		};

		enum { cb_buf = 0x10000 };
		
		HANDLE hHeap = GetProcessHeap();

		if (buf = HeapAlloc(hHeap, 0, cb_buf))
		{
			status = SynchronousDeviceControl(hFile, IOCTL_KS_PROPERTY, 
				&KsProperty, sizeof(KSP_PIN), &pList->ksmi, cb_buf - FIELD_OFFSET(KSMULTIPLE_ITEM_LIST, ksmi), &BytesReturned);

			if (0 <= status)
			{
				PKSMULTIPLE_ITEM pCategories = &pList->ksmi;

				if (ULONG Count = pCategories->Count)
				{
					ULONG Size = pCategories->Size;

					if (sizeof(KSMULTIPLE_ITEM) < Size && Size == BytesReturned)
					{
						Size -= sizeof(KSMULTIPLE_ITEM);

						union {
							PVOID pv;
							PBYTE pb;
							PKSDATARANGE pVideoDataRanges;
							PKS_DATARANGE_VIDEO pDRVideo;
						};

						pv = pCategories + 1;

						ULONG FormatSize;

						do 
						{

							if (Size < sizeof(KSDATARANGE) ||
								(FormatSize = pVideoDataRanges->FormatSize) < sizeof(KSDATARANGE) ||
								Size < (FormatSize = ((FormatSize + __alignof(KSDATARANGE)-1) & ~(__alignof(KSDATARANGE)-1)))
								)
							{
								__debugbreak();
								break;
							}

							Size -= FormatSize;

							DbgPrint("{ FormatSize=%x Flags=%x SampleSize=%x } [remaing=%x]\r\n",
								pVideoDataRanges->FormatSize,
								pVideoDataRanges->Flags,
								pVideoDataRanges->SampleSize,
								Size);

							DumpGuid(&pVideoDataRanges->MajorFormat, "MajorFormat=");
							DumpGuid(&pVideoDataRanges->SubFormat, "SubFormat=");
							DumpGuid(&pVideoDataRanges->Specifier, "Specifier=");

							if (pVideoDataRanges->FormatSize >= sizeof(KS_DATARANGE_VIDEO) &&
								pVideoDataRanges->Specifier == KSDATAFORMAT_SPECIFIER_VIDEOINFO &&
								pVideoDataRanges->MajorFormat == KSDATAFORMAT_TYPE_VIDEO &&
								IsCorresponds(&pDRVideo->ConfigCaps, &pDRVideo->VideoInfoHeader))
							{
								DbgPrint("FixedSize=%x independent=%x\r\n", pDRVideo->bFixedSizeSamples, pDRVideo->bTemporalCompression);
								Dump(&pDRVideo->VideoInfoHeader); 
							}

						} while (pb += FormatSize, --Count);

						if (!Count)
						{
							buf = HeapReAlloc(hHeap, 0, buf, BytesReturned + FIELD_OFFSET(KSMULTIPLE_ITEM_LIST, ksmi));
							pList->next = *pfirst;
							*pfirst = pList;
							buf = 0;
						}
					}
				}
			}

			if (buf) HeapFree(hHeap, 0, buf);
		}

	} while (KsProperty.PinId);

	return STATUS_SUCCESS;
}

// {FCEBBA03-9D13-4C13-9940-CC84FCD132D1}
// long FSCheckIsVCamInstanceId(unsigned short const *);

DEFINE_GUIDSTRUCT("588c8d20-c0e3-4fd3-b511-8f2f692156f8", KSCATEGORY_VIRTUAL_VIDEO_CAMERA);
#define KSCATEGORY_VIRTUAL_VIDEO_CAMERA DEFINE_GUIDNAMED(KSCATEGORY_VIRTUAL_VIDEO_CAMERA)

DEFINE_GUIDSTRUCT("1d813233-9cde-42bf-b446-8f47067b4946", KSCATEGORY_MEP_CAMERA);
#define KSCATEGORY_MEP_CAMERA DEFINE_GUIDNAMED(KSCATEGORY_MEP_CAMERA)

void WCDlg::AddCameras(HWND hwndDlg, GUID* InterfaceClassGuid)
{
	union {
		CONFIGRET cr;
		NTSTATUS status;
	};

	ULONG cch;
	union {
		PVOID buf;
		PZZWSTR Buffer;
	};

	do 
	{
		cr = CM_Get_Device_Interface_List_SizeW(&cch, InterfaceClassGuid, 0, CM_GET_DEVICE_INTERFACE_LIST_PRESENT);

		if (cr != CR_SUCCESS)
		{
			return ;
		}

		if (cch <= 1)
		{
			return ;
		}

		cr = CR_OUT_OF_MEMORY;

		if (buf = LocalAlloc(0, cch * sizeof(WCHAR)))
		{
			if (CR_SUCCESS != (cr = CM_Get_Device_Interface_ListW(
				InterfaceClassGuid, 0, Buffer, cch, CM_GET_DEVICE_INTERFACE_LIST_PRESENT)))
			{
				LocalFree(buf);
			}
		}

	} while (cr == CR_BUFFER_SMALL);

	if (cr != CR_SUCCESS)
	{
		DbgPrint("cr=%x\r\n", cr);
		return ;
	}

	PVOID pv = buf;
	BOOL b = FALSE;

	HWND hwndCB = GetDlgItem(hwndDlg, ID_CAMERA);

	PWSTR Name;

	while (*Buffer)
	{
		DbgPrint("%ws\r\n", Buffer);

		if (HANDLE hFile = fixH(CreateFileW(Buffer, 0, 0, 0, OPEN_EXISTING, 0, 0)))
		{
			KSMULTIPLE_ITEM_LIST* first = 0;

			ProcessP(hFile, &first);

			if (first)
			{
				int i;

				if (CR_SUCCESS == GetFriendlyName(&Name, Buffer))
				{
					i = ComboBox_AddString(hwndCB, Name);
					DbgPrint("[%x] = %ws\r\n", i, Name);
					LocalFree(Name);
				}
				else
				{
					i = ComboBox_AddString(hwndCB, Buffer);
				}

				if (0 <= i)
				{
					if (Camera* p = new Camera(Buffer, first))
					{
						ComboBox_SetItemData(hwndCB, i, p);
						if (!i)
						{
							ComboBox_SetCurSel(hwndCB, 0);
							OnSelChanged(GetDlgItem(hwndDlg, ID_FORMAT), first);
							_i = 0;
						}
						first = 0;
						b = TRUE;
					}
					else
					{
						ComboBox_DeleteString(hwndCB, i);
					}
				}

				if (first)
				{
					KSMULTIPLE_ITEM_LIST::Delete(first);
				}
			}

			NtClose(hFile);
		}
		else
		{
			DbgPrint("open=%x\r\n", RtlGetLastNtStatus());
		}

		Buffer += wcslen(Buffer) + 1;
	}

	if (b)
	{
		_buf = pv;
		EnableWindow(GetDlgItem(hwndDlg, IDOK), TRUE);
	}
	else
	{
		LocalFree(pv);
	}
}

BOOL WCDlg::OnInitDialog(HWND hwndDlg)
{
	static const GUID uuids[] = {
		KSCATEGORY_VIDEO_CAMERA, KSCATEGORY_SENSOR_CAMERA, KSCATEGORY_NETWORK_CAMERA, KSCATEGORY_MEP_CAMERA
	};
	ULONG n = _countof(uuids);
	do 
	{
		AddCameras(hwndDlg, const_cast<GUID*>(&uuids[--n]));
	} while (n);

	CUILayot::CreateLayout(hwndDlg);

	return TRUE;
}

void IO_RUNDOWN::RundownCompleted()
{
	destroyterm();
	ExitProcess(0);
}

class YCameraWnd
{
public:
	static BOOL Register();
	static void Unregister();
};

void CALLBACK ep(void*)
{
	initterm();
	if (YCameraWnd::Register())
	{
		if (0 <= CoInitializeEx(0, COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE))
		{
			_G_log.Init(0x10000);
			WCDlg dlg;
			dlg.DoModal((HINSTANCE)&__ImageBase, MAKEINTRESOURCEW(IDD_DIALOG1), 0, 0);
			CoUninitialize();
		}
		YCameraWnd::Unregister();
	}
	IO_RUNDOWN::g_IoRundown.BeginRundown();
}

_NT_END