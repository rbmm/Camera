#include "stdafx.h"

_NT_BEGIN
#include "log.h"

#include "../asio/io.h"
#include "utils.h"
#include "dump.h"
#include "read.h"

void KsRead::PushFrame(PVOID Frame)
{
	DbgPrint("<<[%x]\r\n", GetFrameNumber(Frame));
	InterlockedPushEntrySList(&_head, (PSLIST_ENTRY)Frame);
}

void KsRead::OnReadComplete(KS_HEADER_AND_INFO* SHGetImage)
{
	ULONG DataUsed = SHGetImage->DataUsed;
	PVOID Frame = SHGetImage->Data;
	InterlockedExchangeAddSizeTNoFence(&_Bytes, DataUsed);

	DbgPrint("ReadComplete(Frame<%x> %x(%I64x) [%I64x] %x)\r\n", GetFrameNumber(Frame), 
		DataUsed, _Bytes, SHGetImage->FrameCompletionNumber, SHGetImage->OptionsFlags);

	Read();

	switch (biCompression)
	{
	case '2YUY':
		YUY2toRGBA((PBYTE)Frame, (PBYTE)_vid->GetBits(), biWidth, biHeight);
		PostMessageW(_hwnd, VBmp::e_update, 0, 0);
		break;
	default:
		if (PVOID buf = _vid->GetBuf())
		{
			if (_vid->BufSize() >= DataUsed)
			{
				memcpy(buf, Frame, DataUsed);
				PostMessageW(_hwnd, VBmp::e_update, DataUsed, 0);
			}
		}
		break;
	}

	PushFrame(Frame);
}

void KsRead::IOCompletionRoutine(CDataPacket* , DWORD Code, NTSTATUS status, ULONG_PTR dwNumberOfBytesTransfered, PVOID Pointer)
{
	ULONG64 Time = GetTickCount64();

	DbgPrint("[%u / %u | %u]:IOCompletionRoutine<%.4hs>(%x, %x)\r\n", 
		InterlockedIncrementNoFence(&_nReadCount), (ULONG)(Time - _StartTime), (ULONG)(Time - _LastCompleteTime), 
		&Code, status, dwNumberOfBytesTransfered);

	_LastCompleteTime = Time;
	
	if (0 <= status)
	{
		switch (Code)
		{
		case op_read:
			OnReadComplete(reinterpret_cast<KS_HEADER_AND_INFO*>(Pointer));
			break;
		default:
			__debugbreak();
		}
	}
	else 
	{
		if (Pointer)
		{
			PushFrame(reinterpret_cast<KS_HEADER_AND_INFO*>(Pointer)->Data);
		}

		Close();
	}
}

KsRead::~KsRead()
{
	if (PVOID Data = _FrameData)
	{
		VirtualFree(Data, 0, MEM_RELEASE);
	}
	_vid->Release();
	PostMessageW(_hwnd, VBmp::e_set, 0, 0);
	DbgPrint("%hs<%p>\r\n", __FUNCTION__, this);
}

void KsRead::Read(PVOID Data)
{
	DbgPrint("read to(Frame<%x>)\r\n", GetFrameNumber(Data));

	if (NT_IRP* Irp = new(sizeof(KS_HEADER_AND_INFO)) NT_IRP(this, op_read, 0))
	{
		KS_HEADER_AND_INFO* SHGetImage = (KS_HEADER_AND_INFO*)Irp->SetPointer();

		RtlZeroMemory(SHGetImage, sizeof(KS_HEADER_AND_INFO));

		SHGetImage->ExtendedHeaderSize = sizeof (KS_FRAME_INFO);
		SHGetImage->Size = sizeof (KS_HEADER_AND_INFO);
		SHGetImage->FrameExtent = biSizeImage;
		SHGetImage->Data = Data;
		SHGetImage->OptionsFlags = KSSTREAM_HEADER_OPTIONSF_FRAMEINFO;
	
		NTSTATUS status = STATUS_INVALID_HANDLE;

		HANDLE PinHandle;
		if (LockHandle(PinHandle))
		{
			status = NtDeviceIoControlFile(PinHandle, 0, 0, Irp, Irp, IOCTL_KS_READ_STREAM, 
				0, 0, SHGetImage, sizeof(KS_HEADER_AND_INFO));

			UnlockHandle();
		}

		Irp->CheckNtStatus(this, status);
	}
	else
	{
		InterlockedPushEntrySList(&_head, (PSLIST_ENTRY)Data);
		IOCompletionRoutine(0, op_read, STATUS_INSUFFICIENT_RESOURCES, 0, 0);
	}
}

void KsRead::Read()
{
	if (PVOID Data = InterlockedPopEntrySList(&_head))
	{
		Read(Data);
	}
	else
	{
		__debugbreak();
	}
}

KsRead::KsRead(HWND hwnd, VBmp* vid) : _hwnd(hwnd), _vid(vid)
{
	DbgPrint("%hs<%p>\r\n", __FUNCTION__, this);
	InitializeSListHead(&_head);
	vid->AddRef();
	SendMessageW(_hwnd, VBmp::e_set, 0, (LPARAM)vid);
}

void KsRead::Stop()
{
	SetState(KSSTATE_STOP);
	Close();
}

NTSTATUS KsRead::SetState(KSSTATE state)
{
	KSPROPERTY KsProperty = { KSPROPSETID_Connection, KSPROPERTY_CONNECTION_STATE, KSPROPERTY_TYPE_SET };

	NTSTATUS status = STATUS_INVALID_HANDLE;

	ULONG t = GetTickCount();
	HANDLE PinHandle;
	if (LockHandle(PinHandle))
	{
		IO_STATUS_BLOCK iosb;

		status = NtDeviceIoControlFile(PinHandle, 0, 0, 0, &iosb, IOCTL_KS_PROPERTY, 
			&KsProperty, sizeof(KSPROPERTY), &state, sizeof(KSSTATE));

		UnlockHandle();
	}

	DbgPrint("SetState(%u)=%x [%u]\r\n", state, status, GetTickCount()-t);
	if (status == STATUS_PENDING)
	{
		__debugbreak();
	}

	return status;
}

NTSTATUS KsRead::GetState(PKSSTATE state)
{
	KSPROPERTY KsProperty = { KSPROPSETID_Connection, KSPROPERTY_CONNECTION_STATE, KSPROPERTY_TYPE_GET };

	NTSTATUS status = STATUS_INVALID_HANDLE;

	HANDLE PinHandle;
	if (LockHandle(PinHandle))
	{
		IO_STATUS_BLOCK iosb;

		status = NtDeviceIoControlFile(PinHandle, 0, 0, 0, &iosb, IOCTL_KS_PROPERTY, 
			&KsProperty, sizeof(KSPROPERTY), state, sizeof(KSSTATE));

		UnlockHandle();
	}

	if (status == STATUS_PENDING)
	{
		__debugbreak();
	}

	return status;
}

NTSTATUS KsRead::Create(_In_ HANDLE FilterHandle, _In_ PKS_DATARANGE_VIDEO pDRVideo)
{
	ULONG SampleSize = pDRVideo->DataRange.SampleSize;

	DbgPrint("KsRead::Create(%x %x)\r\n", pDRVideo->VideoInfoHeader.bmiHeader.biSizeImage, SampleSize);

	union {
		PVOID Data;
		PBYTE pb;
		PSLIST_ENTRY entry;
	};

	SampleSize = ((SampleSize + __alignof(SLIST_ENTRY) - 1) & ~(__alignof(SLIST_ENTRY) - 1));

	if (Data = VirtualAlloc(0, SampleSize << 4, MEM_COMMIT, PAGE_READWRITE))
	{
		_FrameData = Data;

		PSLIST_HEADER head = &_head;
		ULONG n = 1 << 4;
		do 
		{
			InterlockedPushEntrySList(head, entry);
			pb += SampleSize;
		} while (--n);

		HANDLE hFile;
		NTSTATUS status = CreatePin(FilterHandle, pDRVideo, GENERIC_READ, &hFile);

		if (0 <= status)
		{
			if (0 <= (status = NT_IRP::BindIoCompletion(this, hFile)))
			{
				*static_cast<PKS_BITMAPINFOHEADER>(this) = pDRVideo->VideoInfoHeader.bmiHeader;

				Assign(hFile);
				return STATUS_SUCCESS;
			}
		}

		return status;
	}

	return RtlGetLastNtStatus();
}

void KsRead::Start()
{
	_LastCompleteTime = _StartTime = GetTickCount64();

	ULONG n = 8;
	do 
	{
		Read();
	} while (--n);
}

_NT_END