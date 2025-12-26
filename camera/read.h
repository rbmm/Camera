#pragma once

#include "video.h"

struct KS_HEADER_AND_INFO : public KSSTREAM_HEADER, public KS_FRAME_INFO {};

class KsRead : public IO_OBJECT, KS_BITMAPINFOHEADER
{
	SLIST_HEADER _head;
	LONGLONG _Bytes = 0;
	HWND _hwnd;
	VBmp* _vid;
	ULONG64 _StartTime = 0;
	ULONG64 _LastCompleteTime = 0;
	ULONG64 _ReadTime = 0;
	PVOID _FrameData = 0;
	LONG _nReadCount = 0;

	enum { op_read = 'daer' };

	void PushFrame(PVOID Frame);

	ULONG GetFrameNumber(PVOID Frame)
	{
		return (ULONG)(((ULONG_PTR)Frame - (ULONG_PTR)_FrameData)/biSizeImage);
	}

	void OnReadComplete(KS_HEADER_AND_INFO* SHGetImage);

	virtual void IOCompletionRoutine(CDataPacket* , DWORD Code, NTSTATUS status, ULONG_PTR dwNumberOfBytesTransfered, PVOID Pointer);

	virtual ~KsRead();

	void Read(PVOID Data);

	void Read();

public:

	KsRead(HWND hwnd, VBmp* vid);

	void Stop();

	NTSTATUS SetState(KSSTATE state);

	NTSTATUS GetState(PKSSTATE state);

	enum MODE { e_primary, e_secondary, e_exclusive };
	NTSTATUS Create(_In_ HANDLE FilterHandle, _In_ PKS_DATARANGE_VIDEO pDRVideo, _Out_ MODE* mode);

	void Start();

	ULONG GetStat(PLONGLONG Bytes)
	{
		*Bytes = _Bytes;
		return _nReadCount;
	}
};
