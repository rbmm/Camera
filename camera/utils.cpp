#include "stdafx.h"

_NT_BEGIN

#include "utils.h"

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

NTSTATUS CreatePin(_In_ HANDLE FilterHandle, 
				   _In_ PKS_DATARANGE_VIDEO pDRVideo, 
				   _In_ ACCESS_MASK DesiredAccess, 
				   _Out_ PHANDLE ConnectionHandle)
{
	static const WCHAR KsPin[] = KSSTRING_Pin L"\\";

	union {
		PVOID pv;
		ULONG_PTR pb;
		PWSTR FileName;
	};

	struct DATAPINCONNECT : public KSPIN_CONNECT, public KS_DATAFORMAT_VIDEOINFOHEADER 
	{
	};

	pv = alloca(((sizeof(KSSTRING_Pin) + __alignof(DATAPINCONNECT) - 1) & ~(__alignof(DATAPINCONNECT) - 1)) + 
		sizeof(DATAPINCONNECT));

	union {
		ULONG_PTR pb2;
		DATAPINCONNECT* Pin;
	};

	pb2 = (pb + sizeof(KSSTRING_Pin) + __alignof(DATAPINCONNECT) - 1) & ~(__alignof(DATAPINCONNECT) - 1);

	pb = pb2 - sizeof(KSSTRING_Pin);

	wcscpy(FileName, KsPin);

	RtlZeroMemory(static_cast<PKSPIN_CONNECT>(Pin), sizeof(KSPIN_CONNECT));

	Pin->DataFormat = pDRVideo->DataRange;
	Pin->VideoInfoHeader = pDRVideo->VideoInfoHeader;

	Pin->Interface.Set = KSINTERFACESETID_Standard;
	Pin->Medium.Set = KSMEDIUMSETID_Standard;
	Pin->Priority.PriorityClass = KSPRIORITY_NORMAL;
	Pin->Priority.PrioritySubClass = KSPRIORITY_NORMAL;
	Pin->DataFormat.FormatSize = sizeof(KS_DATAFORMAT_VIDEOINFOHEADER);

	UNICODE_STRING ObjectName = { 
		sizeof(KSSTRING_Pin) + sizeof(DATAPINCONNECT), 
		sizeof(KSSTRING_Pin) + sizeof(DATAPINCONNECT), 
		FileName 
	};

	OBJECT_ATTRIBUTES oa = { sizeof(oa), FilterHandle, &ObjectName };

	IO_STATUS_BLOCK iosb;

	return NtOpenFile(ConnectionHandle, DesiredAccess, &oa, &iosb, 0, 0);
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


_NT_END