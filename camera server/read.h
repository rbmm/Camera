#pragma once

#include "H264.h"

struct KS_HEADER_AND_INFO : public KSSTREAM_HEADER, public KS_FRAME_INFO {};

class KsRead : public IO_OBJECT
{
  SLIST_HEADER _head;
  LONGLONG _Bytes = 0;
  H264* _pTarget = 0;
  PVOID _FrameData = 0;
  ULONG _biSizeImage;
  BOOLEAN _bStoping = FALSE;

  enum { op_read = 'daer' };

  void PushFrame(PVOID Frame);

  void OnReadComplete(KS_HEADER_AND_INFO* SHGetImage);

  virtual void IOCompletionRoutine(CDataPacket* , DWORD Code, NTSTATUS status, ULONG_PTR dwNumberOfBytesTransfered, PVOID Pointer);

  virtual ~KsRead();

  void Read(PVOID Data);

  void Read();

public:

  KsRead(H264* pTarget);

  void Stop();

  NTSTATUS SetState(KSSTATE state);

  NTSTATUS GetState(PKSSTATE state);

  NTSTATUS Create(_In_ HANDLE FilterHandle, _In_ PKS_DATARANGE_VIDEO pDRVideo);

  void Start();
};
