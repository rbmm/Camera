#include "stdafx.h"

_NT_BEGIN
#include "log.h"
#include "read.h"
#include "server.h"
#include "utils.h"
#include "userendp.h"

PWSTR GetStringsEnd(PWSTR psz)
{
	while (*psz)
	{
		psz += wcslen(psz) + 1;
	}

	return psz + 1;
}

PBYTE CreateInfo(PBYTE pb, ULONG cb, PULONG pcb, VFI** ppf, ULONG cb2, PULONG pcb2, HANDLE hFile)
{
	ULONG BytesReturned;
	KSP_PIN KsProperty = {{ KSPROPSETID_Pin, KSPROPERTY_PIN_CTYPES, KSPROPERTY_TYPE_GET }};

	// Clients use the KSPROPERTY_PIN_CTYPES property to determine how many pin types a KS filter supports.

	NTSTATUS status = SynchronousDeviceControl(hFile, IOCTL_KS_PROPERTY, 
		&KsProperty, sizeof(KSPROPERTY), &KsProperty.PinId, sizeof(KsProperty.PinId), &BytesReturned);

	if (0 > status)
	{
		return pb;
	}

	if (!KsProperty.PinId)
	{
		return pb;
	}

	union {
		GUID guidCategory;
		KSPIN_DATAFLOW dwFlowDirection;
	};

	VFI* pf = *ppf;
	do 
	{
		--KsProperty.PinId;
		// The KSPROPERTY_PIN_DATAFLOW property specifies the direction of data flow on pins instantiated by the pin factory

		KsProperty.Property.Id = KSPROPERTY_PIN_DATAFLOW;

		status = SynchronousDeviceControl(hFile, IOCTL_KS_PROPERTY, 
			&KsProperty, sizeof(KSP_PIN), &dwFlowDirection, sizeof(dwFlowDirection), &BytesReturned);

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

		if (PINNAME_VIDEO_CAPTURE != guidCategory && PINNAME_VIDEO_PREVIEW != guidCategory)
		{
			continue;
		}

		// use the KSPROPERTY_PIN_DATARANGES property to determine the data ranges supported by pins instantiated by the pin factory
		// out: A KSMULTIPLE_ITEM structure, followed by a sequence of 64-bit aligned KSDATARANGE structures.
		KsProperty.Property.Id = KSPROPERTY_PIN_DATARANGES;

		status = SynchronousDeviceControl(hFile, IOCTL_KS_PROPERTY, &KsProperty, sizeof(KSP_PIN), pb, cb, &BytesReturned);

		if (0 <= status)
		{
			PKSMULTIPLE_ITEM pCategories = (PKSMULTIPLE_ITEM)pb;

			if (ULONG Count = pCategories->Count)
			{
				ULONG Size = pCategories->Size;

				if (sizeof(KSMULTIPLE_ITEM) < Size && Size == BytesReturned)
				{
					Size -= sizeof(KSMULTIPLE_ITEM);

					union {
						PVOID pv;
						PBYTE pc;
						PKSDATARANGE pVideoDataRanges;
						PKS_DATARANGE_VIDEO pDRVideo;
					};

					pv = pCategories + 1;

					ULONG FormatSize;

					PKS_DATARANGE_VIDEO _pDRVideo = (PKS_DATARANGE_VIDEO)pCategories;

					do 
					{
						if (Size < sizeof(KSDATARANGE) ||
							(FormatSize = pVideoDataRanges->FormatSize) < sizeof(KSDATARANGE) ||
							Size < (FormatSize = ((FormatSize + __alignof(KSDATARANGE)-1) & ~(__alignof(KSDATARANGE)-1)))
							)
						{
							break;
						}

						Size -= FormatSize;

						if (pVideoDataRanges->FormatSize == sizeof(KS_DATARANGE_VIDEO) &&
							pVideoDataRanges->Specifier == KSDATAFORMAT_SPECIFIER_VIDEOINFO &&
							pVideoDataRanges->MajorFormat == KSDATAFORMAT_TYPE_VIDEO &&
							pVideoDataRanges->SubFormat == MEDIASUBTYPE_YUY2 &&
							IsCorresponds(&pDRVideo->ConfigCaps, &pDRVideo->VideoInfoHeader))
						{
							if (cb2 < sizeof(VFI))
							{
								break;
							}

							cb2 -= sizeof(VFI);

							pf->FPS = (ULONG)(10000000/pDRVideo->VideoInfoHeader.AvgTimePerFrame);
							pf->biHeight = pDRVideo->VideoInfoHeader.bmiHeader.biHeight;
							pf->biWidth = pDRVideo->VideoInfoHeader.bmiHeader.biWidth;
							pf++->biCompression = pDRVideo->VideoInfoHeader.bmiHeader.biCompression;

							*_pDRVideo++ = *pDRVideo;
						}

					} while (pc += FormatSize, --Count);

					if (!Count)
					{
						cb -= RtlPointerToOffset(pb, _pDRVideo);
						pb = (PBYTE)_pDRVideo;
					}
				}
			}
		}

	} while (KsProperty.PinId);

	*pcb2 = cb2;
	*ppf = pf;
	*pcb = cb;
	return pb;
}

PBYTE CreateInfo(PBYTE pb, ULONG cb, PULONG pcb, PBYTE* ppb2, ULONG cb2, PULONG pcb2, GUID* InterfaceClassGuid)
{
	PWSTR pszDeviceInterface = (PWSTR)pb;
	PBYTE pb2 = *ppb2;

	if (CR_SUCCESS == CM_Get_Device_Interface_ListW(
		InterfaceClassGuid, 0, pszDeviceInterface, cb / sizeof(WCHAR), CM_GET_DEVICE_INTERFACE_LIST_PRESENT))
	{
		pb = (PBYTE)GetStringsEnd(pszDeviceInterface);
		cb -= RtlPointerToOffset(pszDeviceInterface, pb);

		while (*pszDeviceInterface)
		{
			DEVPROPTYPE PropertyType;

			ULONG s = cb2;
			if (CR_SUCCESS == Get_GetFriendlyName(pszDeviceInterface, &PropertyType, pb2, &s) &&
				DEVPROP_TYPE_STRING == PropertyType)
			{
				pb2 += s, cb2 -= s;
			}
			else
			{
				if (cb2 < 2*sizeof(WCHAR))
				{
					return 0;
				}
				*(WCHAR*)pb2 = '*';
				pb2 += sizeof(WCHAR), *(WCHAR*)pb2 = 0;
				pb2 += sizeof(WCHAR), cb2 -= 2*sizeof(WCHAR);
			}

			pb = (PBYTE)(((ULONG_PTR)pb + __alignof(KSDATARANGE) - 1) & ~(__alignof(KSDATARANGE) - 1));
			cb &= ~(__alignof(KSDATARANGE) - 1);

			pb2 = (PBYTE)(((ULONG_PTR)pb2 + __alignof(VFI) - 1) & ~(__alignof(VFI) - 1));
			cb2 &= ~(__alignof(VFI) - 1);

			if (HANDLE hFile = fixH(CreateFileW(pszDeviceInterface, 0, 0, 0, OPEN_EXISTING, 0, 0)))
			{
				pb = CreateInfo(pb, cb, &cb, (VFI**)&pb2, cb2, &cb2, hFile);
				NtClose(hFile);
			}

			if (!pb || cb < sizeof(ULONG) || cb2 < sizeof(ULONG))
			{
				return 0;
			}

			*(ULONG*)pb = 0;
			pb += sizeof(ULONG);
			cb -= sizeof(ULONG);

			*(ULONG*)pb2 = 0;
			pb2 += sizeof(ULONG);
			cb2 -= sizeof(ULONG);

			pszDeviceInterface += wcslen(pszDeviceInterface) + 1;
		}
	}

	if (cb2 < sizeof(ULONG))
	{
		return 0;
	}

	*(WCHAR*)pb2 = 0;
	pb2 += sizeof(WCHAR), cb2 -= sizeof(WCHAR);

	*ppb2 = pb2, *pcb = cb, *pcb2 = cb2;

	return pb;
}

PKS_DATARANGE_VIDEO GetFormatInfo(PWSTR *ppszDeviceInterface, PWSTR pszDeviceInterface, ULONG i, ULONG j)
{
	union {
		ULONG_PTR up;
		PWSTR name;
		PKS_DATARANGE_VIDEO pDRVideo;
	};

	name = GetStringsEnd(pszDeviceInterface);

	ULONG m = 0;

	while (*pszDeviceInterface)
	{
		up = (up + __alignof(KSDATARANGE) - 1) & ~(__alignof(KSDATARANGE) - 1);

		ULONG n = m == i ? 0 : MINLONG;

		while (pDRVideo->DataRange.FormatSize)
		{
			if (j == n)
			{
				*ppszDeviceInterface = pszDeviceInterface;

				return pDRVideo;
			}

			pDRVideo++, n++;
		}

		up += sizeof(ULONG);

		pszDeviceInterface += wcslen(pszDeviceInterface) + 1, m++;
	}

	return 0;
}

void PrintIP(SOCKADDR_INET* from)
{
	WCHAR sz[64];
	ULONG cch = _countof(sz);
	NTSTATUS status = -1;

	switch (from->si_family)
	{
	case AF_INET:
		status = RtlIpv4AddressToStringExW(&from->Ipv4.sin_addr, from->Ipv4.sin_port, sz, &cch);
		break;
	case AF_INET6:
		status = RtlIpv6AddressToStringExW(&from->Ipv6.sin6_addr, from->Ipv6.sin6_scope_id, from->Ipv6.sin6_port, sz, &cch);
		break;
	}

	if (0 <= status)
	{
		DbgPrint("connect: << %ws\r\n", sz);
	}
}

//////////////////////////////////////////////////////////////////////////

NTSTATUS CEncTcp::Stop()
{
	if (_pin)
	{
		_pin->Stop();
		_pin->Release();
		_pin = 0;
		return S_OK;
	}

	return E_FAIL;
}

NTSTATUS CEncTcp::Start(PWSTR pszDeviceInterface, PKS_DATARANGE_VIDEO pDRVideo)
{
	if (HANDLE hFile = fixH(CreateFileW(pszDeviceInterface, 0, 0, 0, OPEN_EXISTING, 0, 0)))
	{
		NTSTATUS status = STATUS_NO_MEMORY;

		KsRead* pin = 0;
		if (H264* p264 = new H264(this))
		{
			if (0 <= (status = p264->Init(
				pDRVideo->VideoInfoHeader.bmiHeader.biWidth,
				pDRVideo->VideoInfoHeader.bmiHeader.biHeight,
				pDRVideo->DataRange.SampleSize,
				pDRVideo->VideoInfoHeader.AvgTimePerFrame)))
			{
				if (!(pin = new KsRead(p264)))
				{
					status = STATUS_NO_MEMORY;
				}
			}
			p264->Release();
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
				_pin = pin;
				pin->Start();
				return S_OK;
			}

			pin->Release();
		}
	}

	return E_FAIL;
}

NTSTATUS CEncTcp::Refresh()
{
	NTSTATUS status = E_FAIL;

	if (_M_buf)
	{
		delete[] _M_buf;
		_M_buf = 0;
	}

	ULONG cb = 0x10000, cb2 = 0x1000;

	if (PBYTE buf = new UCHAR[cb])
	{
		if (PBYTE buf2 = new UCHAR[cb2])
		{
			PBYTE pb2 = buf2;

			if (PBYTE pb = CreateInfo(buf, cb, &cb, &pb2, cb2, &cb2, const_cast<GUID*>(&KSCATEGORY_VIDEO_CAMERA)))
			{
				_M_buf = (PWSTR)buf, buf = 0;

				status = S_OK;
				SendUserData('fmts', buf2, RtlPointerToOffset(buf2, pb2));
			}

			delete[] buf2;
		}
		delete[] buf;
	}

	return status;
}

NTSTATUS CEncTcp::Accept(PSOCKADDR_INET /*psi*/)
{
	DbgPrint("%hs<%p>\r\n", __FUNCTION__, this);
	return 0;
}

NTSTATUS CEncTcp::OnConnect()
{
	DbgPrint("%hs<%p>\r\n", __FUNCTION__, this);

	return 0;
}

void CEncTcp::OnDisconnect()
{
	DbgPrint("%hs<%p>\r\n", __FUNCTION__, this);
	Stop();

	if (_M_buf)
	{
		delete[] _M_buf;
		_M_buf = 0;
	}
}

NTSTATUS CEncTcp::OnUserData(ULONG type, PBYTE pb, ULONG cb)
{
	switch (type)
	{
	case 'strt':
		DbgPrint("%hs<%p>( start )\r\n", __FUNCTION__, this);
		if (!_pin && _M_buf && sizeof(SELECT) == cb)
		{
			PWSTR pszDeviceInterface;
			if (PKS_DATARANGE_VIDEO pDRVideo = GetFormatInfo(&pszDeviceInterface, _M_buf,
				reinterpret_cast<SELECT*>(pb)->i, reinterpret_cast<SELECT*>(pb)->j))
			{
				DbgPrint("%ws\r\n%.4hs [%u x %u] %I64u FPS\r\n",
					pszDeviceInterface,
					&pDRVideo->VideoInfoHeader.bmiHeader.biCompression,
					pDRVideo->VideoInfoHeader.bmiHeader.biWidth,
					pDRVideo->VideoInfoHeader.bmiHeader.biHeight,
					10000000 / pDRVideo->VideoInfoHeader.AvgTimePerFrame);

				return Start(pszDeviceInterface, pDRVideo);
			}
		}
		return FALSE;

	case 'stop':
		DbgPrint("%hs<%p>( stop )\r\n", __FUNCTION__, this);
		return Stop();

	case 'init':
		DbgPrint("%hs<%p>( init )\r\n", __FUNCTION__, this);
		return Refresh();
	}

	DbgPrint("%hs<%p>(%.4hs %p %x)\r\n", __FUNCTION__, this, &type, pb, cb);
	return STATUS_INVALID_DEVICE_REQUEST;
}

CEncTcp::~CEncTcp()
{
	DbgPrint("%hs<%p>\r\n", __FUNCTION__, this);
	if (_M_buf)
	{
		delete[] _M_buf;
		_M_buf = 0;
	}
	Stop();
}

class TClientServerR : public CClientServerR
{
	virtual Endpoint* CreateEndpoint()
	{
		return new CEncTcp(this);
	}
public:
	using CClientServerR::CClientServerR;
};

BOOL StartServer(ULONG64 crc2, ULONG64 crc1)
{
	BOOL fOk = FALSE;

	ULONG err = NOERROR;

	if (CClientServerR* s = new TClientServerR(crc2, crc1, "server"))
	{
		SOCKADDR_INET sa{};
		sa.Ipv4.sin_family = AF_INET;
		sa.Ipv4.sin_port = 0x3333;

		if (fOk = s->Start(4, &sa))
		{
			s->SetTimer(4000);
		}

		s->Release();
	}

	if (err)
	{
		logError("StartServer", err);
	}

	return fOk;
}

_NT_END