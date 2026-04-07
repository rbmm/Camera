#include "stdafx.h"

_NT_BEGIN
#include "../log/log.h"
#include "utils.h"
#include "dump.h"
#include "KsRead.h"

void KsRead::PushFrame(PVOID Frame)
{
	//DbgPrint("<<[%x]\r\n", GetFrameNumber(Frame));
	InterlockedPushEntrySList(&_head, (PSLIST_ENTRY)Frame);
}

void KsRead::OnReadComplete(KS_HEADER_AND_INFO* SHGetImage)
{
	ULONG DataUsed = SHGetImage->DataUsed;
	PVOID Frame = SHGetImage->Data;

	//	DbgPrint("ReadComplete(Frame<%x> %x [%I64x] %x)\r\n", GetFrameNumber(Frame), 
	//		DataUsed, SHGetImage->FrameCompletionNumber, SHGetImage->OptionsFlags);

	Read();

	if (DataUsed)
	{
		_pcb->OnReadSample((PBYTE)Frame, DataUsed);
	}

	PushFrame(Frame);
}

void KsRead::IOCompletionRoutine(CDataPacket*, DWORD Code, NTSTATUS status, 
	[[maybe_unused]] ULONG_PTR dwNumberOfBytesTransfered, PVOID Pointer)
{
	if (status)
	{
		DbgPrint("IOCompletionRoutine<%.4hs>(%x, %x)\r\n", &Code, status, dwNumberOfBytesTransfered);
	}

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

		switch (status)
		{
		case STATUS_CANCELLED:
		case STATUS_TOO_LATE:
			break;
		default:
			_pcb->OnError(status);
			break;
		}
	}
}

KsRead::~KsRead()
{
	if (PVOID Data = _FrameData)
	{
		VirtualFree(Data, 0, MEM_RELEASE);
	}

	if (_pcb)
	{
		_pcb->OnStop();
		_pcb->Release();
	}

	DbgPrint("%hs<%p>\r\n", __FUNCTION__, this);
}

void KsRead::Read(PVOID Data)
{
	// DbgPrint("read to(Frame<%x>)\r\n", GetFrameNumber(Data));

	if (NT_IRP* Irp = new(sizeof(KS_HEADER_AND_INFO)) NT_IRP(this, op_read, 0))
	{
		KS_HEADER_AND_INFO* SHGetImage = (KS_HEADER_AND_INFO*)Irp->SetPointer();

		RtlZeroMemory(SHGetImage, sizeof(KS_HEADER_AND_INFO));

		SHGetImage->ExtendedHeaderSize = sizeof(KS_FRAME_INFO);
		SHGetImage->Size = sizeof(KS_HEADER_AND_INFO);
		SHGetImage->FrameExtent = biSizeImage;
		SHGetImage->Data = Data;
		SHGetImage->OptionsFlags = KSSTREAM_HEADER_OPTIONSF_FRAMEINFO | KSSTREAM_HEADER_OPTIONSF_PERSIST_SAMPLE;

		NTSTATUS status = STATUS_CANCELLED;

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

KsRead::KsRead()
{
	DbgPrint("%hs<%p>\r\n", __FUNCTION__, this);
	InitializeSListHead(&_head);
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

	DbgPrint("SetState(%u)=%x [%u]\r\n", state, status, GetTickCount() - t);
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

NTSTATUS CreatePin(
	_Out_ PHANDLE ConnectionHandle,
	_In_ POBJECT_ATTRIBUTES poa,
	_In_ PKS_DATARANGE_VIDEO pDRVideo
)
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

	poa->ObjectName = &ObjectName;

	IO_STATUS_BLOCK iosb;

	return NtOpenFile(ConnectionHandle, FILE_GENERIC_READ, poa, &iosb, 0, 0);
}

NTSTATUS CreatePin(
	_Out_ PHANDLE ConnectionHandle,
	_In_ PCWSTR pszDeviceInterface,
	_In_ PKS_DATARANGE_VIDEO pDRVideo)
{
	UNICODE_STRING ObjectName;
	OBJECT_ATTRIBUTES oa = { sizeof(oa), 0, &ObjectName, OBJ_CASE_INSENSITIVE };
	NTSTATUS status = RtlDosPathNameToNtPathName_U_WithStatus(pszDeviceInterface, &ObjectName, 0, 0);

	if (0 <= status)
	{
		int i = 4;
		IO_STATUS_BLOCK iosb;
		do 
		{
			status = NtOpenFile(&oa.RootDirectory, SYNCHRONIZE, &oa, &iosb, 0, 0);
		} while (STATUS_CANCELLED == status && --i);

		RtlFreeUnicodeString(&ObjectName);
		if (0 <= status)
		{
			status = CreatePin(ConnectionHandle, &oa, pDRVideo);
			NtClose(oa.RootDirectory);
		}
	}

	return status;
}

#define IOCTL_KS_CUSTOM CTL_CODE(FILE_DEVICE_KS, 0x833, METHOD_OUT_DIRECT, FILE_READ_ACCESS)

NTSTATUS KsRead::Create(_In_ PCWSTR pszDeviceInterface, _In_ PKS_DATARANGE_VIDEO pDRVideo, _Out_ KS_READ_MODE* mode)
{
	ULONG SampleSize = pDRVideo->DataRange.SampleSize;

	DbgPrint("KsRead::Create(%x %x)\r\n", pDRVideo->VideoInfoHeader.bmiHeader.biSizeImage, SampleSize);

	union {
		PVOID Data;
		PBYTE pb;
		PSLIST_ENTRY entry;
	};

	SampleSize = ((SampleSize + __alignof(SLIST_ENTRY) - 1) & ~(__alignof(SLIST_ENTRY) - 1));

	ULONG SamplesBufferSize = SampleSize << 4;

	if (sizeof(KS_DATARANGE_VIDEO) > SamplesBufferSize)
	{
		return STATUS_BUFFER_OVERFLOW;
	}

	if (Data = VirtualAlloc(0, SamplesBufferSize, MEM_COMMIT, PAGE_READWRITE))
	{
		_FrameData = Data;

		HANDLE hFile;
		NTSTATUS status = CreatePin(&hFile, pszDeviceInterface, pDRVideo);

		DbgPrint("CreatePin=%x\r\n", status);

		if (0 <= status)
		{
			*static_cast<PKS_BITMAPINFOHEADER>(this) = pDRVideo->VideoInfoHeader.bmiHeader;

			memcpy(Data, pDRVideo, sizeof(KS_DATARANGE_VIDEO));
			ULONG cb;
			switch (status = SynchronousDeviceControl(hFile, IOCTL_KS_CUSTOM, 0, 0, Data, SamplesBufferSize, &cb))
			{
			case STATUS_INVALID_DEVICE_REQUEST:
				*mode = e_exclusive;
				goto __ok;
			case STATUS_PORT_ALREADY_SET:
				*mode = e_secondary;
				goto __ok;
			case STATUS_SUCCESS:
				*mode = e_primary;
			__ok:
				if (0 <= (status = NT_IRP::BindIoCompletion(this, hFile)))
				{
					PSLIST_HEADER head = &_head;
					ULONG n = 1 << 4;
					do
					{
						InterlockedPushEntrySList(head, entry);
						pb += SampleSize;
					} while (--n);

					Assign(hFile);
					return STATUS_SUCCESS;
				}
				break;
			}

			NtClose(hFile);
		}

		return status;
	}

	return RtlGetLastNtStatus();
}

HRESULT KsRead::Start(IReaderCallback* pcb)
{
	NTSTATUS status;

	if ((status = pcb->Init(biCompression, biWidth, biHeight, biSizeImage)) || (status = SetState(KSSTATE_RUN)))
	{
		DbgPrint("%hs = %x\r\n", __FUNCTION__, status);
		return status;
	}

	_pcb = pcb;
	pcb->AddRef();

	ULONG n = 8;
	do
	{
		Read();
	} while (--n);

	return STATUS_SUCCESS;
}

_NT_END