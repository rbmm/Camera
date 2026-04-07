#include "stdafx.h"

_NT_BEGIN
#include "log.h"

#include "../asio/io.h"
#include "utils.h"
#include "read.h"

void KsRead::PushFrame(PVOID Frame)
{
	InterlockedPushEntrySList(&_head, (PSLIST_ENTRY)Frame);
}

void KsRead::OnReadComplete(KS_HEADER_AND_INFO* SHGetImage)
{
	ULONG DataUsed = SHGetImage->DataUsed;
	PVOID Frame = SHGetImage->Data;
	InterlockedExchangeAddSizeTNoFence(&_Bytes, DataUsed);

	Read();

	if (DataUsed)
	{
		if (!_pTarget->ProcessFrame((PBYTE)Frame, DataUsed))
		{
			if (!_bStoping)
			{
				Stop();
				_pTarget->Stop();
			}
		}
	}

	PushFrame(Frame);
}

void KsRead::IOCompletionRoutine(CDataPacket* , DWORD Code, NTSTATUS status, ULONG_PTR /*dwNumberOfBytesTransfered*/, PVOID Pointer)
{
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

		if (!_bStoping)
		{
			_pTarget->Stop();
			Close();
		}
	}
}

KsRead::~KsRead()
{
	if (PVOID Data = _FrameData)
	{
		VirtualFree(Data, 0, MEM_RELEASE);
	}
	_pTarget->Stop(!_bStoping);
	_pTarget->Release();
	DbgPrint("%hs<%p>\r\n", __FUNCTION__, this);
}

void KsRead::Read(PVOID Data)
{
	if (NT_IRP* Irp = new(sizeof(KS_HEADER_AND_INFO)) NT_IRP(this, op_read, 0))
	{
		KS_HEADER_AND_INFO* SHGetImage = (KS_HEADER_AND_INFO*)Irp->SetPointer();

		RtlZeroMemory(SHGetImage, sizeof(KS_HEADER_AND_INFO));

		SHGetImage->ExtendedHeaderSize = sizeof (KS_FRAME_INFO);
		SHGetImage->Size = sizeof (KS_HEADER_AND_INFO);
		SHGetImage->FrameExtent = _biSizeImage;
		SHGetImage->Data = Data;
		SHGetImage->OptionsFlags = KSSTREAM_HEADER_OPTIONSF_FRAMEINFO|KSSTREAM_HEADER_OPTIONSF_PERSIST_SAMPLE;

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
}

KsRead::KsRead(H264* pTarget) : _pTarget(pTarget)
{
	DbgPrint("%hs<%p>\r\n", __FUNCTION__, this);
	InitializeSListHead(&_head);
	pTarget->AddRef();
}

void KsRead::Stop()
{
	_bStoping = TRUE;
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

	_biSizeImage = pDRVideo->VideoInfoHeader.bmiHeader.biSizeImage;
	SampleSize = ((SampleSize + __alignof(SLIST_ENTRY) - 1) & ~(__alignof(SLIST_ENTRY) - 1));

	ULONG SamplesBufferSize = 16*SampleSize;

	if (Data = VirtualAlloc(0, SamplesBufferSize, MEM_COMMIT, PAGE_READWRITE))
	{
		_FrameData = Data;

		HANDLE hFile;
		NTSTATUS status = CreatePin(FilterHandle, pDRVideo, GENERIC_READ, &hFile);

		if (0 <= status)
		{
			if (0 <= (status = NT_IRP::BindIoCompletion(this, hFile)))
			{
				PSLIST_HEADER head = &_head;
				ULONG n = 16;
				do 
				{
					InterlockedPushEntrySList(head, entry);
					pb += SampleSize;
				} while (--n);

				Assign(hFile);
				return STATUS_SUCCESS;
			}

			NtClose(hFile);
		}

		return status;
	}

	return RtlGetLastNtStatus();
}

void KsRead::Start()
{
	ULONG n = 8;
	do 
	{
		Read();
	} while (--n);
}

_NT_END