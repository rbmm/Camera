#pragma once

extern const volatile UCHAR guz;

CONFIGRET GetFriendlyName(_Out_ PWSTR* ppszName, _In_ PCWSTR pszDeviceInterface);

CONFIGRET GetInterfaces(_Out_ PWSTR* pBuffer, _In_ GUID* InterfaceClassGuid);

BOOL IsRDPSource(PCWSTR pszDeviceInterface);

ULONG GetVidPid(PCWSTR DeviceID);

NTSTATUS
SynchronousDeviceControl(
	_In_ HANDLE      Handle,
	_In_ ULONG       IoControl,
	_In_reads_bytes_opt_(InLength) PVOID   InBuffer,
	_In_ ULONG       InLength,
	_Out_writes_bytes_opt_(OutLength) PVOID  OutBuffer,
	_In_ ULONG       OutLength,
	_Inout_opt_ PULONG BytesReturned
);

BOOL IsCorresponds(PKS_VIDEO_STREAM_CONFIG_CAPS ConfigCaps, PKS_VIDEOINFOHEADER VideoInfoHeader);

struct __declspec(novtable) EnumVideoFormats
{
	virtual void OnList(_In_ PKSMULTIPLE_ITEM pCategories) = 0;

	void DoEnum(_In_ HANDLE hFile, _In_ PKSMULTIPLE_ITEM pCategories, _In_ ULONG cbBuf);
	void DoEnum(_In_ PCWSTR lpInterfaceName, _In_ PKSMULTIPLE_ITEM pCategories, _In_ ULONG cbBuf);
};
