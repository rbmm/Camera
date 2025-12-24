#include "stdafx.h"

_NT_BEGIN

#include "dump.h"

#ifdef _WIN64
#define __stosp __stosq
#else
#define __stosp __stosd
#endif

struct DATAPINCONNECT : public KSPIN_CONNECT, public KS_DATAFORMAT_VIDEOINFOHEADER {};

struct KS_STREAM_INFO : public KSSTREAM_HEADER, public KS_FRAME_INFO {};

void pat()
{
	PVOID pv[32];
	if (ULONG n = RtlWalkFrameChain(pv, _countof(pv), 0))
	{
		do 
		{
			DbgPrint(">> %p\n", pv[--n]);
		} while (n);
	}
}

struct DEV_EXTENSION : EX_RUNDOWN_REF
{
	PDEVICE_OBJECT _AttachedToDeviceObject;

	static DEV_EXTENSION* get(PDEVICE_OBJECT DeviceObject)
	{
		return reinterpret_cast<DEV_EXTENSION*>(DeviceObject->DeviceExtension);
	}
};

void Dump(ULONG IoControlCode, PVOID OutputBuffer, ULONG OutputBufferLength, bool bUserBuffer)
{
	__try
	{
		switch (IoControlCode)
		{
		case IOCTL_KS_PROPERTY:
			DbgPrint("--IOCTL_KS_PROPERTY: %x\r\n", OutputBufferLength);
			if (bUserBuffer) ProbeForRead(OutputBuffer, OutputBufferLength, 1);
			DumpBytes((PBYTE)OutputBuffer, OutputBufferLength);
			break;

		case IOCTL_KS_ENABLE_EVENT:
			DbgPrint("--IOCTL_KS_ENABLE_EVENT:\r\n");
			if (bUserBuffer) ProbeForRead(OutputBuffer, OutputBufferLength, 1);
			DumpBytes((PBYTE)OutputBuffer, OutputBufferLength);
			break;

		case IOCTL_KS_READ_STREAM:
			DbgPrint("--READ_STREAM:\r\n");
			if (OutputBufferLength >= sizeof(KS_STREAM_INFO))
			{
				if (bUserBuffer) ProbeForRead(OutputBuffer, sizeof(KS_STREAM_INFO), __alignof(KS_STREAM_INFO));
				DumpHeader((KS_STREAM_INFO*)OutputBuffer);
				DumpFrame((KS_STREAM_INFO*)OutputBuffer);
			}
			break;
		}

	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{

	}
}

#define FLAGS_SET(v, f) ((f) == ((v) & (f)))

NTSTATUS CALLBACK OnComplete (PDEVICE_OBJECT DeviceObject, PIRP Irp, DEV_EXTENSION* pExt)
{
	PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);

	if (Irp->PendingReturned)
	{
		IrpSp->Control |= SL_PENDING_RETURNED;
	}

	ExReleaseRundownProtection(pExt);

	ULONG dwTheadId = 0;

	if (PETHREAD Thread = Irp->Tail.Overlay.Thread)
	{
		dwTheadId = (ULONG)(ULONG_PTR)PsGetThreadId(Thread);
	}

	PEPROCESS Process = IoGetRequestorProcess(Irp);
	ULONG dwProcessId = Process ? (ULONG)(ULONG_PTR)PsGetProcessId(Process) : 0, dwCurProcessId = (ULONG)(ULONG_PTR)PsGetCurrentProcessId();

	DbgPrint("--%p[%x]>%x [%x.%x=%x.%x] P=%x (%x.%x)->(%x.%x)\r\n", 
		Irp, Irp->Flags, KeGetCurrentIrql(), 
		IrpSp->MajorFunction, IrpSp->MinorFunction, Irp->IoStatus.Status, Irp->IoStatus.Information,
		Irp->PendingReturned, dwProcessId, dwTheadId, 
		dwCurProcessId, (ULONG)(ULONG_PTR)PsGetCurrentThreadId());

	if (IrpSp->MajorFunction == IRP_MJ_PNP && IrpSp->MinorFunction == IRP_MN_REMOVE_DEVICE)
	{
		// if dispatch !!
		DbgPrint("%p>++ ExWaitForRundownProtectionRelease\n", PsGetCurrentThreadId());
		pat();
		ExWaitForRundownProtectionRelease(pExt);
		DbgPrint("%p>-- ExWaitForRundownProtectionRelease\n", PsGetCurrentThreadId());

		IoDetachDevice(pExt->_AttachedToDeviceObject);
		IoDeleteDevice(DeviceObject);
	}
	else if (IrpSp->MajorFunction == IRP_MJ_DEVICE_CONTROL)
	{
		ULONG OutputBufferLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
		PVOID OutputBuffer = Irp->UserBuffer;

		__try 
		{
			KAPC_STATE ApcState;

			switch (ULONG IoControlCode = IrpSp->Parameters.DeviceIoControl.IoControlCode)
			{
			case IOCTL_KS_PROPERTY:
			case IOCTL_KS_ENABLE_EVENT:
			case IOCTL_KS_READ_STREAM:
				if (Irp->AssociatedIrp.SystemBuffer && FLAGS_SET(Irp->Flags, IRP_BUFFERED_IO|IRP_INPUT_OPERATION))
				{
					Dump(IoControlCode, Irp->AssociatedIrp.SystemBuffer, OutputBufferLength, FALSE);
				}
				else if (dwProcessId == dwCurProcessId)
				{
					Dump(IoControlCode, OutputBuffer, OutputBufferLength, TRUE);
				}
				else
				{
					if (KeGetCurrentIrql())
					{
						Dump(IoControlCode, 0, 0, FALSE);
					}
					else
					{
						KeStackAttachProcess(Process, &ApcState);
						Dump(IoControlCode, OutputBuffer, OutputBufferLength, TRUE);
						KeUnstackDetachProcess(&ApcState);
					}
				}
				break;
			}
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			DbgPrint("========@@@@@@@========\n");
		}
	}

	return STATUS_SUCCESS;
}

NTSTATUS CALLBACK CommonDispatch(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp), nextIrpSp;

	if (IrpSp->MajorFunction == IRP_MJ_POWER)
	{
		PoStartNextPowerIrp(Irp);
	}

	DEV_EXTENSION* pExt = DEV_EXTENSION::get(DeviceObject);

	if (!ExAcquireRundownProtection(pExt))
	{
		Irp->IoStatus.Status = STATUS_DELETE_PENDING;
		Irp->IoStatus.Information = 0;
		IofCompleteRequest(Irp, IO_NO_INCREMENT);
		return STATUS_DELETE_PENDING;
	}

	PDEVICE_OBJECT AttachedToDeviceObject = pExt->_AttachedToDeviceObject;

	if (AttachedToDeviceObject->StackSize < Irp->CurrentLocation)
	{
		*(nextIrpSp = IrpSp - 1) = *IrpSp;
		nextIrpSp->Control = SL_INVOKE_ON_SUCCESS|SL_INVOKE_ON_ERROR|SL_INVOKE_ON_CANCEL;
		nextIrpSp->Context = pExt;
		nextIrpSp->CompletionRoutine = (PIO_COMPLETION_ROUTINE)OnComplete;

		PFILE_OBJECT FileObject = IrpSp->FileObject;
		PCUNICODE_STRING FileName = 0;
		ULONG Length = 0;
		union {
			PKSPIN_CONNECT p;
			DATAPINCONNECT* q;
			PVOID pv;
			PUCHAR pb;
		};

		STATIC_UNICODE_STRING(Pin, "" KSSTRING_Pin "\\");

		if (FileObject)
		{
			FileName = &FileObject->FileName;

			if (RtlPrefixUnicodeString(&Pin, FileName, TRUE))
			{
				Length = FileName->Length - Pin.Length;
				pv = (PBYTE)FileName->Buffer + Pin.Length;
				STATIC_UNICODE_STRING(szPin, "Pin");
				FileName = &szPin;
			}
		}

		DbgPrint("++%p[%x]>%p [%x.%x] \"%wZ\" +%x\n", Irp, Irp->Flags, PsGetCurrentProcessId(),
			IrpSp->MajorFunction, IrpSp->MinorFunction, FileName, Length);

		switch (IrpSp->MajorFunction)
		{
		case IRP_MJ_CREATE:
			if (sizeof(KSPIN_CONNECT) <= Length)
			{
				Dump(p);

				if (sizeof(DATAPINCONNECT) <= Length )
				{
					Dump(static_cast<PKS_DATAFORMAT_VIDEOINFOHEADER>(q), Length - sizeof(KSPIN_CONNECT));
				}
				else
				{
					DumpBytes((PUCHAR)p, Length);
				}
			}
			break;

		case IRP_MJ_DEVICE_CONTROL:
		case IRP_MJ_INTERNAL_DEVICE_CONTROL:

			ULONG InputBufferLength = IrpSp->Parameters.DeviceIoControl.InputBufferLength;
			ULONG OutputBufferLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
			PVOID InputBuffer = IrpSp->Parameters.DeviceIoControl.Type3InputBuffer;
			PVOID OutputBuffer = Irp->UserBuffer;

			__try 
			{
				switch (IrpSp->Parameters.DeviceIoControl.IoControlCode)
				{
				case IOCTL_KS_PROPERTY:
					DbgPrint("IOCTL_KS_PROPERTY:%x %x\r\n", InputBufferLength, OutputBufferLength);
					if (InputBufferLength >= sizeof(KSPROPERTY))
					{
						Dump((KSPROPERTY*)InputBuffer);
					}
					break;

				case IOCTL_KS_ENABLE_EVENT:
					DbgPrint("IOCTL_KS_ENABLE_EVENT:%x %x\r\n", InputBufferLength, OutputBufferLength);
					if (InputBufferLength >= sizeof(KSEVENT))
					{
						Dump((KSEVENT*)InputBuffer);
					}
					break;
				
				case IOCTL_KS_DISABLE_EVENT:
					DbgPrint("IOCTL_KS_DISABLE_EVENT:%x %x\r\n", InputBufferLength, OutputBufferLength);
					if (InputBufferLength >= sizeof(KSEVENTDATA ))
					{
						Dump((KSEVENTDATA *)InputBuffer);
					}
					break;

				case IOCTL_KS_METHOD:
					DbgPrint("IOCTL_KS_METHOD:%x %x\r\n", InputBufferLength, OutputBufferLength);
					if (InputBufferLength >= sizeof(KSMETHOD ))
					{
						Dump((KSMETHOD*)InputBuffer);
					}
					break;

				case IOCTL_KS_WRITE_STREAM:
					DbgPrint("IOCTL_KS_WRITE_STREAM:\r\n");
					break;
				
				case IOCTL_KS_READ_STREAM:
					DbgPrint("READ_STREAM:\r\n");
					if (OutputBufferLength >= sizeof(KS_STREAM_INFO))
					{
						ProbeForRead(OutputBuffer, sizeof(KS_STREAM_INFO), __alignof(KS_STREAM_INFO));
						DumpHeader((KS_STREAM_INFO*)OutputBuffer);
						DumpFrame((KS_STREAM_INFO*)OutputBuffer);
					}
					if (InputBufferLength || InputBuffer)
					{
						DbgPrint("!! READ_STREAM(%x, %x)\r\n", InputBufferLength, InputBuffer);

						if (InputBufferLength >= sizeof(KS_STREAM_INFO) && InputBuffer)
						{
							ProbeForRead(InputBuffer, sizeof(KS_STREAM_INFO), __alignof(KS_STREAM_INFO));
							DumpHeader((KS_STREAM_INFO*)InputBuffer);
							DumpFrame((KS_STREAM_INFO*)InputBuffer);
						}
					}
					break;

				case IOCTL_KS_RESET_STATE:
					DbgPrint("IOCTL_KS_RESET_STATE:%x\r\n", InputBufferLength);
					if (InputBufferLength >= sizeof(int))
					{
						ProbeForRead(InputBuffer, sizeof(int), __alignof(int));
						switch (*(int*)InputBuffer)
						{
						case KSRESET_BEGIN:
							DbgPrint("\tKSRESET_BEGIN\r\n");
							break;
						case KSRESET_END:
							DbgPrint("\tKSRESET_END\r\n");
							break;
						default:
							DbgPrint("\t[%x]\r\n", *(int*)InputBuffer);
							break;
						}
					}
					break;

				default:
					DbgPrint("IOCTL:[%x] %x %x\r\n", 
						IrpSp->Parameters.DeviceIoControl.IoControlCode, 
						InputBufferLength, 
						OutputBufferLength);
				}

			}
			__except(EXCEPTION_EXECUTE_HANDLER)
			{
				DbgPrint("========@@@@@@@========\n");
			}
			break;
		}
	}
	else
	{
		IoSkipCurrentIrpStackLocation(Irp);
		ExReleaseRundownProtection(pExt);

		DbgPrint("Skip(%x/%x)\r\n", AttachedToDeviceObject->StackSize, Irp->CurrentLocation);
	}

	return IofCallDriver(AttachedToDeviceObject, Irp);
}

PVOID NotificationEntry;

NTSTATUS NTAPI AddDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT PhysicalDeviceObject)
{
	PDEVICE_OBJECT DeviceObject, TargetDevice = IoGetAttachedDeviceReference(PhysicalDeviceObject);

	NTSTATUS status = IoCreateDevice(DriverObject, sizeof(DEV_EXTENSION), 0, TargetDevice->DeviceType, 
		TargetDevice->Characteristics & (FILE_REMOVABLE_MEDIA|FILE_DEVICE_SECURE_OPEN), FALSE, &DeviceObject);

	if (0 <= status)
	{
		DeviceObject->Flags |= TargetDevice->Flags & 
			(DO_BUFFERED_IO|DO_DIRECT_IO|DO_SUPPORTS_TRANSACTIONS|DO_POWER_PAGABLE|DO_POWER_INRUSH);

		DEV_EXTENSION* pExt = (DEV_EXTENSION*)DeviceObject->DeviceExtension;

		ExInitializeRundownProtection(pExt);

		if (0 > (status = IoAttachDeviceToDeviceStackSafe(DeviceObject, TargetDevice, &pExt->_AttachedToDeviceObject)))
		{
			IoDeleteDevice(DeviceObject);
		}
		else
		{
			DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

			DbgPrint("++DeviceObject<%p> %x\n", DeviceObject, DeviceObject->Flags);
		}
	}

	ObfDereferenceObject(TargetDevice);

	return status;
}

NTSTATUS CALLBACK OnInterfaceChanged (
									  IN PDEVICE_INTERFACE_CHANGE_NOTIFICATION Notification,
									  IN PDRIVER_OBJECT DriverObject
									  )
{
	if (Notification->InterfaceClassGuid != KSCATEGORY_VIDEO_CAMERA ||
		Notification->Event != GUID_DEVICE_INTERFACE_ARRIVAL)
	{
		return 0;
	}

	HANDLE hFile;
	OBJECT_ATTRIBUTES oa = { sizeof(oa), 0, Notification->SymbolicLinkName, OBJ_CASE_INSENSITIVE };
	IO_STATUS_BLOCK iosb;
	NTSTATUS status = IoCreateFile(&hFile, SYNCHRONIZE, &oa, &iosb, 0, 0, FILE_SHARE_VALID_FLAGS, FILE_OPEN, 0, 0, 0, CreateFileTypeNone, 0, IO_ATTACH_DEVICE);

	DbgPrint("OnInterfaceChanged:<%wZ>=%x\n", Notification->SymbolicLinkName, status);

	if (0 <= status)
	{
		PFILE_OBJECT FileObject;

		status = ObReferenceObjectByHandle(hFile, 0, 0, 0, (void**)&FileObject, 0);

		NtClose(hFile);

		if (0 <= status)
		{
			status = AddDevice(DriverObject, FileObject->DeviceObject);

			ObfDereferenceObject(FileObject);
		}
	}
	return 0;
}

void CALLBACK DriverUnload(PDRIVER_OBJECT DriverObject)
{
	DbgPrint("++DriverUnload(%p)\n", DriverObject);
	if (NotificationEntry)
	{
		IoUnregisterPlugPlayNotificationEx(NotificationEntry);
	}
	DbgPrint("--DriverUnload(%p)\n", DriverObject);
}

VOID NTAPI FastIoDetachDevice (PDEVICE_OBJECT SourceDevice, PDEVICE_OBJECT TargetDevice)
{
	DbgPrint("%p>FastIoDetachDevice(%p, %p)\n", PsGetCurrentThreadId(), SourceDevice, TargetDevice);
	pat();
}

NTSTATUS CALLBACK DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING )
{		
	DbgPrint("DriverEntry(%p)\n", DriverObject);

	static const FAST_IO_DISPATCH FastIoDispatch = {
		sizeof(FAST_IO_DISPATCH), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, FastIoDetachDevice
	};

	DriverObject->FastIoDispatch = const_cast<PFAST_IO_DISPATCH>(&FastIoDispatch);

	DriverObject->DriverUnload = DriverUnload;

	__stosp((PULONG_PTR)DriverObject->MajorFunction, (ULONG_PTR)CommonDispatch, _countof(DriverObject->MajorFunction));

	return IoRegisterPlugPlayNotification(EventCategoryDeviceInterfaceChange,
		PNPNOTIFY_DEVICE_INTERFACE_INCLUDE_EXISTING_INTERFACES, 
		const_cast<GUID*>(&KSCATEGORY_VIDEO_CAMERA), DriverObject, 
		(PDRIVER_NOTIFICATION_CALLBACK_ROUTINE)OnInterfaceChanged, DriverObject, &NotificationEntry);
}

_NT_END