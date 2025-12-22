#pragma once

extern const volatile UCHAR guz;

CONFIGRET GetFriendlyName(_Out_ PWSTR* ppszName, _In_ PCWSTR pszDeviceInterface);

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

NTSTATUS CreatePin(_In_ HANDLE FilterHandle, 
				   _In_ PKS_DATARANGE_VIDEO pDRVideo, 
				   _In_ ACCESS_MASK DesiredAccess, 
				   _Out_ PHANDLE ConnectionHandle);

BOOL IsCorresponds(PKS_VIDEO_STREAM_CONFIG_CAPS ConfigCaps, PKS_VIDEOINFOHEADER VideoInfoHeader);