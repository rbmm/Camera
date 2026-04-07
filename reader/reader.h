#pragma once

struct __declspec(novtable) IReaderCallback
{
	virtual ULONG STDMETHODCALLTYPE AddRef() = 0;
	virtual ULONG STDMETHODCALLTYPE Release() = 0;
	virtual HRESULT Init(ULONG biCompression, LONG cx, LONG cy, ULONG cbFrame) = 0;
	virtual void OnReadSample(PBYTE pbFrame, ULONG cbFrame) = 0;
	virtual void OnStop() = 0;
	virtual void OnError(NTSTATUS status) = 0;
};

struct __declspec(novtable) CCameraBase
{
	virtual ULONG STDMETHODCALLTYPE AddRef() = 0;
	virtual ULONG STDMETHODCALLTYPE Release() = 0;
	virtual HRESULT Start(_In_ IReaderCallback* pcb) = 0;
	virtual void Stop() = 0;
};

// auto select "best" pszDeviceInterface and pDRVideo
NTSTATUS StartReader(
	_Out_ CCameraBase** ppSourceReader, 
	_In_ BOOL bRdp,
	_In_ IReaderCallback* pcb,
	_Out_ PULONG HashValue,
	_Out_opt_ PWSTR* FriendlyName,
	_Out_ PULONG p_vid_pid,
	_In_ PCWSTR pszDeviceInterface = 0);

enum KS_READ_MODE { e_primary, e_secondary, e_exclusive };

// user select pszDeviceInterface ( camera ) and pDRVideo ( size, compression, FPS ) in UI
NTSTATUS StartKFReader(
	_Out_ CCameraBase** ppSourceReader,
	_In_ IReaderCallback* pcb,
	_In_ PCWSTR pszDeviceInterface,
	_In_ PKS_DATARANGE_VIDEO pDRVideo,
	_Out_ KS_READ_MODE* mode);

NTSTATUS HashString(_In_ PCWSTR pszDeviceInterface, _Out_ PULONG HashValue);
