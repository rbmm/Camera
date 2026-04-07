#include "stdafx.h"

_NT_BEGIN

#include "../log/log.h"
#include "utils.h"
#include "dump.h"

NTSTATUS
SynchronousDeviceControl(
	_In_ HANDLE      Handle,
	_In_ ULONG       IoControl,
	_In_reads_bytes_opt_(InLength) PVOID   InBuffer,
	_In_ ULONG       InLength,
	_Out_writes_bytes_opt_(OutLength) PVOID  OutBuffer,
	_In_ ULONG       OutLength,
	_Inout_opt_ PULONG BytesReturned
)
{
	IO_STATUS_BLOCK iosb;

	NTSTATUS status = NtDeviceIoControlFile(Handle, 0, 0, 0, &iosb, IoControl, InBuffer, InLength, OutBuffer, OutLength);

	*BytesReturned = (ULONG)iosb.Information;

	if (status == STATUS_PENDING)
	{
		__debugbreak();
	}

	return status;
}

CONFIGRET Get_GetFriendlyName(PCWSTR pszDeviceInterface,
	DEVPROPTYPE* PropertyType,
	PBYTE pb,
	PULONG pcb)
{
	ULONG s = *pcb;
	if (CONFIGRET cr = CM_Get_Device_Interface_PropertyW(pszDeviceInterface, &DEVPKEY_NAME, PropertyType, pb, pcb, 0))
	{
		DEVINST dnDevInst;
		WCHAR InstanceId[MAX_DEVICE_ID_LEN];
		*pcb = s, s = sizeof(InstanceId);
		CR_SUCCESS == (cr = CM_Get_Device_Interface_PropertyW(pszDeviceInterface,
			&DEVPKEY_Device_InstanceId, PropertyType, (PBYTE)InstanceId, &s, 0)) &&
			CR_SUCCESS == (cr = CM_Locate_DevNodeW(&dnDevInst, InstanceId, CM_LOCATE_DEVNODE_NORMAL)) &&
			CR_SUCCESS == (cr = CM_Get_DevNode_PropertyW(dnDevInst, &DEVPKEY_NAME, PropertyType, pb, pcb, 0));

		return cr;
	}
	return CR_SUCCESS;
}

CONFIGRET GetFriendlyName(_Out_ PWSTR* ppszName, _In_ PCWSTR pszDeviceInterface)
{
	DEVPROPTYPE PropertyType;

	union {
		PVOID pv;
		PWSTR sz;
		PBYTE pb;
	};

	ULONG cb = 0x80;
	CONFIGRET cr;

	do
	{
		cr = CR_OUT_OF_MEMORY;

		if (pv = LocalAlloc(0, cb))
		{
			if (CR_SUCCESS == (cr = Get_GetFriendlyName(pszDeviceInterface, &PropertyType, pb, &cb)))
			{
				if (PropertyType == DEVPROP_TYPE_STRING)
				{
					*ppszName = sz;
					return CR_SUCCESS;
				}

				cr = CR_WRONG_TYPE;
			}
			LocalFree(pv);
		}

	} while (cr == CR_BUFFER_SMALL);

	return cr;
}

CONFIGRET GetInterfaces(_Out_ PWSTR* pBuffer, _In_ GUID* InterfaceClassGuid)
{
	CONFIGRET cr;

	do
	{
		ULONG ulLen;
		if (cr = CM_Get_Device_Interface_List_SizeW(&ulLen, InterfaceClassGuid, 0, CM_GET_DEVICE_INTERFACE_LIST_PRESENT))
		{
			return cr;
		}

		if (ulLen <= 1)
		{
			return CR_NO_SUCH_DEVNODE;
		}

		cr = CR_OUT_OF_MEMORY;

		if (PZZWSTR Buffer = (PZZWSTR)LocalAlloc(0, ulLen * sizeof(WCHAR)))
		{
			if (!(cr = CM_Get_Device_Interface_ListW(InterfaceClassGuid, 
				0, Buffer, ulLen, CM_GET_DEVICE_INTERFACE_LIST_PRESENT)))
			{
				*pBuffer = Buffer;
				return CR_SUCCESS;
			}
			LocalFree(Buffer);
		}

	} while (cr == CR_BUFFER_SMALL);

	return cr;
}

// \\?\RDCAMERA_BUS#UMB#...&RDCamera_Device_...#{e5323777-f976-4f5b-9b55-b94699c46e44}\RDCameraSource

BOOL IsRDPSource(PCWSTR pszDeviceInterface)
{
	DbgPrint("IsRDPSource(%ws)\r\n", pszDeviceInterface);

	DEVPROPTYPE PropertyType;
	ULONG cb = (ULONG)wcslen(pszDeviceInterface) * sizeof(WCHAR), s = cb;
	union {
		PVOID pv;
		PBYTE pb;
		PWSTR sz;
	};
	pv = alloca(cb);

	DEVINST dnDevInst;
	if (!CM_Get_Device_Interface_PropertyW(pszDeviceInterface, &DEVPKEY_Device_InstanceId, &PropertyType, pb, &cb, 0) &&
		!CM_Locate_DevNodeW(&dnDevInst, sz, CM_LOCATE_DEVNODE_NORMAL) &&
		!CM_Get_DevNode_PropertyW(dnDevInst, &DEVPKEY_Device_EnumeratorName, &PropertyType, pb, &(cb = s), 0))
	{
		DbgPrint("Enumerator=%ws\r\n", sz);
		return !wcscmp(sz, L"RDCAMERA_BUS") || !wcscmp(sz, L"ROOT");
	}

	return FALSE;
}

ULONG GetVidPid(PCWSTR DeviceID)
{
	union {
		ULONG vid_pid;
		struct {
			USHORT vid, pid;
		};
	};

	if (DeviceID = wcsstr(DeviceID, L"VID_"))
	{
		vid = (USHORT)wcstoul(DeviceID + 4, const_cast<PWSTR*>(&DeviceID), 16);

		if ('&' == *DeviceID++ && 'P' == *DeviceID++ && 'I' == *DeviceID++ && 'D' == *DeviceID++ && '_' == *DeviceID++)
		{
			pid = (USHORT)wcstoul(DeviceID, const_cast<PWSTR*>(&DeviceID), 16);

			if ('&' == *DeviceID)
			{
				return vid_pid;
			}
		}
	}

	return 0;
}

BOOL IsCorresponds(PKS_VIDEO_STREAM_CONFIG_CAPS ConfigCaps,
	REFERENCE_TIME AvgTimePerFrame,
	LONG biWidth,
	LONG biHeight)
{
	return AvgTimePerFrame >= ConfigCaps->MinFrameInterval &&
		AvgTimePerFrame <= ConfigCaps->MaxFrameInterval &&
		biWidth <= ConfigCaps->MaxOutputSize.cx &&
		biWidth >= ConfigCaps->MinOutputSize.cx &&
		biHeight <= ConfigCaps->MaxOutputSize.cy &&
		biHeight >= ConfigCaps->MinOutputSize.cy;
}

BOOL IsCorresponds(PKS_VIDEO_STREAM_CONFIG_CAPS ConfigCaps, PKS_VIDEOINFOHEADER VideoInfoHeader)
{
	return IsCorresponds(ConfigCaps, VideoInfoHeader->AvgTimePerFrame,
		VideoInfoHeader->bmiHeader.biWidth, VideoInfoHeader->bmiHeader.biHeight);
}

void EnumVideoFormats::DoEnum(_In_ PCWSTR lpInterfaceName, _In_ PKSMULTIPLE_ITEM pCategories, _In_ ULONG cbBuf)
{
	if (HANDLE hFile = fixH(CreateFileW(lpInterfaceName, 0, 0, 0, OPEN_EXISTING, 0, 0)))
	{
		DoEnum(hFile, pCategories, cbBuf);
		NtClose(hFile);
	}
}

void EnumVideoFormats::DoEnum(_In_ HANDLE hFile, _In_ PKSMULTIPLE_ITEM pCategories, _In_ ULONG cbBuf)
{
	ULONG BytesReturned;
	KSP_PIN KsProperty = { { KSPROPSETID_Pin, KSPROPERTY_PIN_CTYPES, KSPROPERTY_TYPE_GET } };

	// Clients use the KSPROPERTY_PIN_CTYPES property to determine how many pin types a KS filter supports.

	NTSTATUS status = SynchronousDeviceControl(hFile, IOCTL_KS_PROPERTY,
		&KsProperty, sizeof(KSPROPERTY), &KsProperty.PinId, sizeof(KsProperty.PinId), &BytesReturned);

	if (0 > status)
	{
		DbgPrint("IOCTL_KS_PROPERTY=%x\r\n", status);
		return;
	}

	if (!KsProperty.PinId)
	{
		return;
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

		if (PINNAME_VIDEO_CAPTURE != guidCategory && PINNAME_VIDEO_PREVIEW != guidCategory)
		{
			continue;
		}

		// use the KSPROPERTY_PIN_DATARANGES property to determine the data ranges supported by pins instantiated by the pin factory
		// out: A KSMULTIPLE_ITEM structure, followed by a sequence of 64-bit aligned KSDATARANGE structures.
		KsProperty.Property.Id = KSPROPERTY_PIN_DATARANGES;

		if (0 <= (status = SynchronousDeviceControl(hFile, IOCTL_KS_PROPERTY,
			&KsProperty, sizeof(KSP_PIN), pCategories, cbBuf, &BytesReturned)))
		{
			if (ULONG Count = pCategories->Count)
			{
				ULONG Size = pCategories->Size;

				if (sizeof(KSMULTIPLE_ITEM) < Size && Size == BytesReturned)
				{
					OnList(pCategories);
				}
			}
		}

	} while (KsProperty.PinId);
}

_NT_END