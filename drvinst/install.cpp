#include "stdafx.h"
#include "zip.h"

extern const UCHAR codesec_exe_begin[], codesec_exe_end[];

_NT_BEGIN

#include "print.h"

static const WCHAR DriverFileName[] = L"\\systemroot\\system32\\drivers\\D8F86BDE6363440e821FB5F0B9C9FF4F.sys";
static const WCHAR DriverServiceName[] = L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\D8F86BDE6363440e821FB5F0B9C9FF4F";

NTSTATUS DropDriver()
{
	PVOID pv;
	LARGE_INTEGER as {};

	HRESULT hr = Unzip(codesec_exe_begin, RtlPointerToOffset(codesec_exe_begin, codesec_exe_end), &pv, &as.LowPart);
	if (S_OK == hr)
	{
		UNICODE_STRING ObjectName;
		OBJECT_ATTRIBUTES oa = { sizeof(oa), 0, &ObjectName, OBJ_CASE_INSENSITIVE };
		HANDLE hFile;
		IO_STATUS_BLOCK iosb;

		RtlInitUnicodeString(&ObjectName, DriverFileName);

		if (0 <= (hr = NtCreateFile(&hFile, FILE_APPEND_DATA|SYNCHRONIZE, &oa, &iosb, &as, 
			FILE_ATTRIBUTE_SYSTEM|FILE_ATTRIBUTE_HIDDEN, 0, FILE_OVERWRITE_IF, 
			FILE_SYNCHRONOUS_IO_NONALERT|FILE_NON_DIRECTORY_FILE|FILE_OPEN_REPARSE_POINT, 0, 0)))
		{
			hr = NtWriteFile(hFile, 0, 0, 0, &iosb, pv, as.LowPart, 0, 0);
			NtClose(hFile);
		}
		LocalFree(pv);
	}

	return hr;
}

NTSTATUS DeleteDriver()
{
	UNICODE_STRING ObjectName;
	OBJECT_ATTRIBUTES oa = { sizeof(oa), 0, &ObjectName, OBJ_CASE_INSENSITIVE };
	RtlInitUnicodeString(&ObjectName, DriverFileName);
	return ZwDeleteFile(&oa);
}

NTSTATUS LoadDriver()
{
	UNICODE_STRING ObjectName;
	RtlInitUnicodeString(&ObjectName, DriverServiceName);
	return ZwLoadDriver(&ObjectName);
}

NTSTATUS SetValueKey( _In_ HANDLE hKey, _In_ PCWSTR ValueName, _In_ ULONG Type, _In_ const void* Data, _In_ ULONG DataSize )
{
	UNICODE_STRING ObjectName;
	RtlInitUnicodeString(&ObjectName, ValueName);
	return ZwSetValueKey(hKey, &ObjectName, 0, Type, const_cast<void*>(Data), DataSize);
}

NTSTATUS SetValueKey( _In_ HANDLE hKey, _In_ PCWSTR ValueName, _In_ ULONG Value )
{
	return SetValueKey(hKey, ValueName, REG_DWORD, &Value, sizeof(Value));
}

NTSTATUS RegisterDriver()
{
	UNICODE_STRING ObjectName;
	OBJECT_ATTRIBUTES oa = { sizeof(oa), 0, &ObjectName, OBJ_CASE_INSENSITIVE };
	RtlInitUnicodeString(&ObjectName, DriverServiceName);
	HANDLE hKey;
	NTSTATUS status = ZwCreateKey(&hKey, KEY_ALL_ACCESS, &oa, 0, 0, 0, 0);

	if (0 <= status)
	{
		if (0 > (status = SetValueKey(hKey, L"ImagePath", REG_SZ, DriverFileName, sizeof(DriverFileName))) || 
			0 > (status = SetValueKey(hKey, L"Start", SERVICE_DEMAND_START)) || 
			0 > (status = SetValueKey(hKey, L"Type", SERVICE_KERNEL_DRIVER)) || 
			0 > (status = SetValueKey(hKey, L"ErrorControl", SERVICE_ERROR_IGNORE)) ||
			0 > (status = DropDriver()))
		{
			ZwDeleteKey(hKey);
		}

		NtClose(hKey);
	}

	return status;
}

NTSTATUS UnregisterDriver()
{
	DeleteDriver();

	UNICODE_STRING ObjectName;
	OBJECT_ATTRIBUTES oa = { sizeof(oa), 0, &ObjectName, OBJ_CASE_INSENSITIVE };
	RtlInitUnicodeString(&ObjectName, DriverServiceName);
	HANDLE hKey;
	NTSTATUS status = ZwOpenKey(&hKey, DELETE, &oa);

	if (0 <= status)
	{
		status = ZwDeleteKey(hKey);
	}

	return status;
}

extern const volatile UCHAR guz = 0;

#include <initguid.h>
#include <devpkey.h>

CONFIGRET DeleteItf(PCWSTR pszDeviceInterface)
{
	DbgPrint("process %ws\r\n", pszDeviceInterface);

	DEVPROPTYPE PropertyType;

	union {
		PBYTE PropertyBuffer;
		PVOID buf;
		DEVINSTID_W pDeviceID;
		PWSTR PDOName;
	};

	ULONG cb = 0, rcb = (ULONG)wcslen(pszDeviceInterface) * sizeof(WCHAR);
	CONFIGRET cr;
	PVOID stack = alloca(guz);

	do
	{
		if (cb < rcb)
		{
			rcb = cb = RtlPointerToOffset(buf = alloca(rcb - cb), stack);
		}

		cr = CM_Get_Device_Interface_PropertyW(pszDeviceInterface, &DEVPKEY_Device_InstanceId,
			&PropertyType, PropertyBuffer, &rcb, 0);

	} while (cr == CR_BUFFER_SMALL);

	DEVINST dnDevInst;

	if (cr == CR_SUCCESS &&
		PropertyType == DEVPROP_TYPE_STRING &&
		CR_SUCCESS == (cr = CM_Locate_DevNodeW(&dnDevInst, pDeviceID, CM_LOCATE_DEVNODE_NORMAL)))
	{
		if (CR_SUCCESS == (cr = CM_Disable_DevNode(dnDevInst, 0)))
		{
			cr = CM_Enable_DevNode(dnDevInst, 0);
		}
		DbgPrint("Disable/Enable %ws = %x\r\n", pDeviceID, cr);
	}

	return cr;
}

void Unload(GUID* InterfaceClassGuid)
{
	union {
		CONFIGRET cr;
		NTSTATUS status;
	};

	ULONG cch;
	union {
		PVOID buf = 0;
		PZZWSTR Buffer;
	};

	do
	{
		cr = CM_Get_Device_Interface_List_SizeW(&cch, InterfaceClassGuid, 0, CM_GET_DEVICE_INTERFACE_LIST_PRESENT);

		if (cr != CR_SUCCESS)
		{
			return;
		}

		if (cch <= 1)
		{
			return;
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
		if (cr = CM_MapCrToWin32Err(cr, 0))
		{
			PrintError(cr);
		}
		return;
	}

	PVOID pv = Buffer;

	while (*Buffer)
	{
		PrintError(CM_MapCrToWin32Err(DeleteItf(Buffer), 0));
		Buffer += wcslen(Buffer);
	}

	LocalFree(pv);
}

typedef IUnknown* PUNKNOWN;
#undef INTERFACE
#include <ks.h>

DEFINE_GUIDSTRUCT("588c8d20-c0e3-4fd3-b511-8f2f692156f8", KSCATEGORY_VIRTUAL_VIDEO_CAMERA);
#define KSCATEGORY_VIRTUAL_VIDEO_CAMERA DEFINE_GUIDNAMED(KSCATEGORY_VIRTUAL_VIDEO_CAMERA)

DEFINE_GUIDSTRUCT("1d813233-9cde-42bf-b446-8f47067b4946", KSCATEGORY_MEP_CAMERA);
#define KSCATEGORY_MEP_CAMERA DEFINE_GUIDNAMED(KSCATEGORY_MEP_CAMERA)

void UnloadDriver()
{
	static const GUID uuids[] = {
		KSCATEGORY_VIDEO_CAMERA, KSCATEGORY_SENSOR_CAMERA, KSCATEGORY_NETWORK_CAMERA, KSCATEGORY_MEP_CAMERA
	};
	ULONG n = _countof(uuids);
	do
	{
		Unload(const_cast<GUID*>(&uuids[--n]));
	} while (n);
}

_NT_END