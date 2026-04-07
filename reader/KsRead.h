#pragma once

#include "reader.h"
#include "../asio/io.h"

struct KS_HEADER_AND_INFO : public KSSTREAM_HEADER, public KS_FRAME_INFO {};

class KsRead : public IO_OBJECT, public CCameraBase, KS_BITMAPINFOHEADER
{
	SLIST_HEADER _head;
	IReaderCallback* _pcb = 0;
	PVOID _FrameData = 0;

	enum { op_read = 'daer' };

	void PushFrame(PVOID Frame);

	ULONG GetFrameNumber(PVOID Frame)
	{
		return (ULONG)(((ULONG_PTR)Frame - (ULONG_PTR)_FrameData) / biSizeImage);
	}

	void OnReadComplete(KS_HEADER_AND_INFO* SHGetImage);

	virtual void IOCompletionRoutine(CDataPacket*, DWORD Code, NTSTATUS status, ULONG_PTR dwNumberOfBytesTransfered, PVOID Pointer);

	virtual ~KsRead();

	void Read(PVOID Data);

	void Read();

public:

	virtual ULONG STDMETHODCALLTYPE AddRef()
	{
		IO_OBJECT::AddRef();
		return 0;
	}

	virtual ULONG STDMETHODCALLTYPE Release()
	{
		IO_OBJECT::Release();
		return 0;
	}

	virtual HRESULT Start(IReaderCallback* pcb);

	virtual void Stop();

	KsRead();

	NTSTATUS SetState(KSSTATE state);

	NTSTATUS GetState(PKSSTATE state);

	NTSTATUS Create(_In_ PCWSTR pszDeviceInterface, _In_ PKS_DATARANGE_VIDEO pDRVideo, _Out_ KS_READ_MODE* mode);
};
