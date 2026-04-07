#include "stdafx.h"

_NT_BEGIN
#include "log.h"

#include "../asio/io.h"
#include "utils.h"
#include "dump.h"
#include "read.h"

void YUY2ToI420(ULONG width, ULONG height, _In_ const BYTE* yuy2, _Out_ BYTE* i420)
{
	ULONG h = 0;
	// Y-плоскость занимает width * height
	PBYTE y_plane = i420;
	// U-плоскость начинаетс€ сразу после Y (размер: width * height / 4)
	PBYTE u_plane = i420 + (width * height);
	// V-плоскость начинаетс€ после U (размер: width * height / 4)
	PBYTE v_plane = i420 + (width * height) + (width * height / 4);

	do
	{
		ULONG uv_width = width / 2;
		do
		{
			union {
				ULONG u;
				struct {
					UCHAR y0, u_val, y1, v_val; // —труктура YUY2: [Y0 U Y1 V]
				};
			};

			u = *((ULONG*&)yuy2)++;

			// «аписываем €ркость в Y-плоскость
			*y_plane++ = y0;
			*y_plane++ = y1;

			// «аписываем цветность только дл€ каждой второй строки (4:2:0)
			if (!(1 & h))
			{
				*u_plane++ = u_val;
				*v_plane++ = v_val;
			}

		} while (--uv_width);

	} while (h++, --height);
}

void KsRead::PushFrame(PVOID Frame)
{
	//DbgPrint("<<[%x]\r\n", GetFrameNumber(Frame));
	InterlockedPushEntrySList(&_head, (PSLIST_ENTRY)Frame);
}
//#undef DbgPrint

#define CASE(x) case NAL_UNIT_ ## x: return #x;

PCSTR GetNalType(uint32_t type)
{
	switch (type)
	{
		CASE(CODED_SLICE_TRAIL_N);
		CASE(CODED_SLICE_TRAIL_R);
		CASE(CODED_SLICE_TSA_N);
		CASE(CODED_SLICE_TSA_R);
		CASE(CODED_SLICE_STSA_N);
		CASE(CODED_SLICE_STSA_R);
		CASE(CODED_SLICE_RADL_N);
		CASE(CODED_SLICE_RADL_R);
		CASE(CODED_SLICE_RASL_N);
		CASE(CODED_SLICE_RASL_R);
		CASE(CODED_SLICE_BLA_W_LP);
		CASE(CODED_SLICE_BLA_W_RADL);
		CASE(CODED_SLICE_BLA_N_LP);
		CASE(CODED_SLICE_IDR_W_RADL);
		CASE(CODED_SLICE_IDR_N_LP);
		CASE(CODED_SLICE_CRA);
		CASE(VPS);
		CASE(SPS);
		CASE(PPS);
		CASE(ACCESS_UNIT_DELIMITER);
		CASE(EOS);
		CASE(EOB);
		CASE(FILLER_DATA);
		CASE(PREFIX_SEI);
		CASE(SUFFIX_SEI);
		CASE(UNSPECIFIED);
		CASE(INVALID);
	}
	return "***";
}

void KsRead::OnReadComplete(KS_HEADER_AND_INFO* SHGetImage)
{
	ULONG DataUsed = SHGetImage->DataUsed;
	PVOID Frame = SHGetImage->Data;
	InterlockedExchangeAddSizeTNoFence(&_Bytes, DataUsed);

	//DbgPrint("ReadComplete(Frame<%x> %x(%I64x) [%I64x] %x)\r\n", GetFrameNumber(Frame), 
	//	DataUsed, _Bytes, SHGetImage->FrameCompletionNumber, SHGetImage->OptionsFlags);

	Read();

	switch (biCompression)
	{
	case '2YUY':
		if (DataUsed == biSizeImage)
		{
			YUY2toRGBA((PBYTE)Frame, (PULONG)_vid->GetBits(), biWidth, biHeight);
			PostMessageW(_hwnd, VBmp::e_update, 0, 0);

			YUY2ToI420(biWidth, biHeight, (PBYTE)Frame, m_i420);

			union {
				FILETIME ft;
				int64_t pts;
			};

			GetSystemTimeAsFileTime(&ft);
			m_pic->pts = pts;

			uint32_t i_nal = 0;
			x265_nal* p_nal = 0;

			AcquireSRWLockExclusive(&m_lock);

			if (0 > x265_encoder_encode(m_encoder, &p_nal, &i_nal, m_pic, 0))
			{
				__debugbreak();
				break;
			}

			if (i_nal)
			{
				do
				{
					DbgPrint("%hs %x\r\n", GetNalType(p_nal->type), p_nal->sizeBytes);
				} while (p_nal++, --i_nal);
			}

			ReleaseSRWLockExclusive(&m_lock);
		}

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

	//DbgPrint("[%u / %u | %u]:IOCompletionRoutine<%.4hs>(%x, %x)\r\n", 
	//	InterlockedIncrementNoFence(&_nReadCount), (ULONG)(Time - _StartTime), (ULONG)(Time - _LastCompleteTime), 
	//	&Code, status, dwNumberOfBytesTransfered);

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

	if (m_encoder) x265_encoder_close(m_encoder);
	if (m_pic) x265_picture_free(m_pic);
	if (m_i420) delete [] m_i420;
}

void KsRead::Read(PVOID Data)
{
	// DbgPrint("read to(Frame<%x>)\r\n", GetFrameNumber(Data));

	if (NT_IRP* Irp = new(sizeof(KS_HEADER_AND_INFO)) NT_IRP(this, op_read, 0))
	{
		KS_HEADER_AND_INFO* SHGetImage = (KS_HEADER_AND_INFO*)Irp->SetPointer();

		RtlZeroMemory(SHGetImage, sizeof(KS_HEADER_AND_INFO));

		SHGetImage->ExtendedHeaderSize = sizeof (KS_FRAME_INFO);
		SHGetImage->Size = sizeof (KS_HEADER_AND_INFO);
		SHGetImage->FrameExtent = biSizeImage;
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

x265_encoder* Init265(_In_ PKS_VIDEOINFOHEADER VideoInfoHeader, _In_ PBYTE y_plane, _Out_ x265_picture** ppic)
{
	if (x265_param* param = x265_param_alloc())
	{
		x265_param_default_preset(param, "ultrafast", "zerolatency");
		param->fpsNum = 10000000, param->fpsDenom = (uint32_t)VideoInfoHeader->AvgTimePerFrame;
		param->sourceWidth = VideoInfoHeader->bmiHeader.biWidth;
		param->sourceHeight = VideoInfoHeader->bmiHeader.biHeight;

		param->internalCsp = X265_CSP_I420;
		param->sourceWidth = VideoInfoHeader->bmiHeader.biWidth;
		param->sourceHeight = VideoInfoHeader->bmiHeader.biHeight;

		param->rc.rateControlMode = X265_RC_ABR;
		param->rc.bitrate = 2048;
		param->rc.vbvMaxBitrate = 4096;
		param->rc.vbvBufferSize = 4096;
		int fps = param->fpsDenom ? param->fpsNum / param->fpsDenom : 30;
		param->keyframeMax = fps * 2;
		param->keyframeMin = fps;
		param->bIntraRefresh = 1; 

		param->bAnnexB = 1;
		param->bRepeatHeaders = 1;
		param->logLevel = X265_LOG_FULL;

		param->bEnableWavefront = 0;

		param->bEnableSAO = 0; 
		param->rc.cuTree = 0;
		param->bEmitInfoSEI = 0;

		if (x265_picture* pic = x265_picture_alloc())
		{
			x265_picture_init(param, pic);

			if (x265_encoder* encoder = x265_encoder_open(param))
			{
				ULONG Width = param->sourceWidth;
				ULONG cb = Width * param->sourceHeight;

				PBYTE u_plane = y_plane + cb;
				PBYTE v_plane = u_plane + (cb >> 2);

				pic->colorSpace = X265_CSP_I420;
				pic->sliceType = X265_TYPE_AUTO;

				pic->planes[0] = y_plane;
				pic->stride[0] = Width;

				pic->planes[1] = u_plane; 
				pic->stride[1] = Width >> 1;

				pic->planes[2] = v_plane;
				pic->stride[2] = Width >> 1;

				*ppic = pic;

				x265_param_free(param);

				return encoder;
			}
			x265_picture_free(pic);
		}

		x265_param_free(param);
	}

	return 0;
}

#define IOCTL_KS_CUSTOM CTL_CODE(FILE_DEVICE_KS, 0x833, METHOD_OUT_DIRECT, FILE_READ_ACCESS)

NTSTATUS KsRead::Create(_In_ HANDLE FilterHandle, _In_ PKS_DATARANGE_VIDEO pDRVideo, _Out_ MODE* mode)
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

	if (Data = VirtualAlloc(0, SamplesBufferSize, MEM_COMMIT, PAGE_READWRITE))
	{
		_FrameData = Data;

		HANDLE hFile;
		NTSTATUS status = CreatePin(FilterHandle, pDRVideo, GENERIC_READ, &hFile);

		if (0 <= status)
		{
			*static_cast<PKS_BITMAPINFOHEADER>(this) = pDRVideo->VideoInfoHeader.bmiHeader;

			if (m_i420 = new BYTE[3 * biWidth * biHeight >> 1] )
			{
				m_encoder = Init265(&pDRVideo->VideoInfoHeader, m_i420, &m_pic);
			}

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