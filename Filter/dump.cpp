#include "stdafx.h"

_NT_BEGIN

#include "dump.h"

void DumpBytes(const UCHAR* pb, ULONG cb)
{
	if (cb)
	{
		if (0x80 < cb)
		{
			cb = 0x80;
		}
		do 
		{
			ULONG m = min(16, cb);
			cb -= m;
			char buf[128], *sz = buf;
			do 
			{
				sz += sprintf(sz, "%02x ", *pb++);
			} while (--m);
			*sz++ = '\n', *sz++ = 0;
			DbgPrint(buf);
		} while (cb);
	}
}

DEFINE_GUIDSTRUCT("B03A874B-D32B-4213-AC38-25A718E4454F", KSPROPSETID_DevInstPropertySet);
#define KSPROPSETID_DevInstPropertySet DEFINE_GUIDNAMED(KSPROPSETID_DevInstPropertySet)

DEFINE_GUIDSTRUCT("A60D8368-5324-4893-B020-C431A50BCBE3", KSPROPSETID_Frame);
#define KSPROPSETID_Frame DEFINE_GUIDNAMED(KSPROPSETID_Frame)

static PCSTR GetMFGUIDString(const GUID* guid)
{
	struct GANS 
	{
		GUID guid;
		PCSTR name;
	};

	static const GANS _S_MFGUIDString[] = {
		// 6994AD05-93EF-11D0-A3CC-00A0C9223196
		{{ 0x6994AD05, 0x93EF, 0x11D0, { 0xA3, 0xCC, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96 }}, "KSCATEGORY_VIDEO" },
		// 1CB79112-C0D2-4213-9CA6-CD4FDB927972
		{{ 0x1CB79112, 0xC0D2, 0x4213, { 0x9C, 0xA6, 0xCD, 0x4F, 0xDB, 0x92, 0x79, 0x72 }}, "KSPROPERTYSETID_ExtendedCameraControl" },
		// 4747B320-62CE-11CF-A5D6-28DB04C10000
		{{ 0x4747B320, 0x62CE, 0x11CF, { 0xA5, 0xD6, 0x28, 0xDB, 0x04, 0xC1, 0x00, 0x00 }}, "KSMEDIUMSETID_Standard" },
		// 1D58C920-AC9B-11CF-A5D6-28DB04C10000
		{{ 0x1D58C920, 0xAC9B, 0x11CF, { 0xA5, 0xD6, 0x28, 0xDB, 0x04, 0xC1, 0x00, 0x00 }}, "KSPROPSETID_Connection" },
		// DB47DE20-F628-11D1-BA41-00A0C90D2B05
		{{ 0xDB47DE20, 0xF628, 0x11D1, { 0xBA, 0x41, 0x00, 0xA0, 0xC9, 0x0D, 0x2B, 0x05 }}, "KSEVENTSETID_VIDCAPNotify" },
		// 65E8773D-8F56-11D0-A3B9-00A0C9223196
		{{ 0x65E8773D, 0x8F56, 0x11D0, { 0xA3, 0xB9, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96 }}, "KSCATEGORY_CAPTURE" },
		// C6E13344-30AC-11D0-A18C-00A0C9118956
		{{ 0xC6E13344, 0x30AC, 0x11D0, { 0xA1, 0x8C, 0x00, 0xA0, 0xC9, 0x11, 0x89, 0x56 }}, "PROPSETID_VIDCAP_DROPPEDFRAMES" },
		// B03A874B-D32B-4213-AC38-25A718E4454F
		{{ 0xB03A874B, 0xD32B, 0x4213, { 0xAC, 0x38, 0x25, 0xA7, 0x18, 0xE4, 0x45, 0x4F }}, "KSPROPSETID_DevInstPropertySet" },
		// 47504A4D-0000-0010-8000-00AA00389B71
		{{ 0x47504A4D, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 }}, "KSDATAFORMAT_SUBTYPE_MJPG" },
		// 162AC456-83D7-4239-96DF-C75FFA138BC6
		{{ 0x162ac456, 0x83d7, 0x4239, { 0x96, 0xdf, 0xc7, 0x5f, 0xfa, 0x13, 0x8b, 0xc6 }}, "KSEVENTSETID_DynamicFormatChange" },
		// 32595559-0000-0010-8000-00AA00389B71
		{{ 0x32595559, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 }}, "MEDIASUBTYPE_YUY2" },
		// 0A3D1C5D-5243-4819-9ED0-AEE8044CEE2B
		{{ 0x0a3d1c5d, 0x5243, 0x4819, { 0x9e, 0xd0, 0xae, 0xe8, 0x4, 0x4c, 0xee, 0x2b }}, "KSPROPSETID_MemoryTransport" },
		// C6E13360-30AC-11D0-A18C-00A0C9118956
		{{ 0xC6E13360, 0x30AC, 0x11D0, { 0xA1, 0x8C, 0x00, 0xA0, 0xC9, 0x11, 0x89, 0x56 }}, "PROPSETID_VIDCAP_VIDEOPROCAMP" },
		// 8C134960-51AD-11CF-878A-94F801C10000
		{{ 0x8C134960, 0x51AD, 0x11CF, { 0x87, 0x8A, 0x94, 0xF8, 0x01, 0xC1, 0x00, 0x00 }}, "KSPROPSETID_Pin" },
		// A60D8368-5324-4893-B020-C431A50BCBE3
		{{ 0xA60D8368, 0x5324, 0x4893, { 0xB0, 0x20, 0xC4, 0x31, 0xA5, 0x0B, 0xCB, 0xE3 }}, "KSPROPSETID_Frame" },
		// 6A2E0670-28E4-11D0-A18C-00A0C9118956
		{{ 0x6A2E0670, 0x28E4, 0x11D0, { 0xA1, 0x8C, 0x00, 0xA0, 0xC9, 0x11, 0x89, 0x56 }}, "PROPSETID_VIDCAP_VIDEOCONTROL" },
		// C6E13370-30AC-11D0-A18C-00A0C9118956
		{{ 0xC6E13370, 0x30AC, 0x11D0, { 0xA1, 0x8C, 0x00, 0xA0, 0xC9, 0x11, 0x89, 0x56 }}, "PROPSETID_VIDCAP_CAMERACONTROL" },
		// 73646976-0000-0010-8000-00AA00389B71
		{{ 0x73646976, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 }}, "KSDATAFORMAT_TYPE_VIDEO" },
		// E5323777-F976-4F5B-9B55-B94699C46E44
		{{ 0xE5323777, 0xF976, 0x4F5B, { 0x9B, 0x55, 0xB9, 0x46, 0x99, 0xC4, 0x6E, 0x44 }}, "KSCATEGORY_VIDEO_CAMERA" },
		// 05589F80-C356-11CE-BF01-00AA0055595A
		{{ 0x05589F80, 0xC356, 0x11CE, { 0xBF, 0x01, 0x00, 0xAA, 0x00, 0x55, 0x59, 0x5A }}, "KSDATAFORMAT_SPECIFIER_VIDEOINFO" },
		// FB6C4281-0353-11D1-905F-0000C0CC16BA
		{{ 0xFB6C4281, 0x0353, 0x11D1, { 0x90, 0x5F, 0x00, 0x00, 0xC0, 0xCC, 0x16, 0xBA }}, "PINNAME_VIDEO_CAPTURE" },
		// 1A8766A0-62CE-11CF-A5D6-28DB04C10000
		{{ 0x1A8766A0, 0x62CE, 0x11CF, { 0xA5, 0xD6, 0x28, 0xDB, 0x04, 0xC1, 0x00, 0x00 }}, "KSINTERFACESETID_Standard" },
		// F72A76A0-EB0A-11D0-ACE4-0000C0CC16BA
		{{ 0xF72A76A0, 0xEB0A, 0x11D0, { 0xAC, 0xE4, 0x00, 0x00, 0xC0, 0xCC, 0x16, 0xBA }}, "KSDATAFORMAT_SPECIFIER_VIDEOINFO2" },
		// 1464EDA5-6A8F-11D1-9AA7-00A0C9223196
		{{ 0x1464EDA5, 0x6A8F, 0x11D1, { 0x9A, 0xA7, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96 }}, "KSPROPSETID_General" },
		// 720D4AC0-7533-11D0-A5D6-28DB04C10000
		{{ 0x720D4AC0, 0x7533, 0x11D0, { 0xA5, 0xD6, 0x28, 0xDB, 0x04, 0xC1, 0x00, 0x00 }}, "KSPROPSETID_Topology" },
		// 941C7AC0-C559-11D0-8A2B-00A0C9255AC1
		{{ 0x941C7AC0, 0xC559, 0x11D0, { 0x8A, 0x2B, 0x00, 0xA0, 0xC9, 0x25, 0x5A, 0xC1 }}, "KSNODETYPE_DEV_SPECIFIC" },
		// 7f4bcbe0-9ea5-11cf-a5d6-28db04c10000
		{{ 0x7f4bcbe0, 0x9ea5, 0x11cf, { 0xa5, 0xd6, 0x28, 0xdb, 0x04, 0xc1, 0x00, 0x00 }}, "KSEVENTSETID_Connection" },
		// DFF229E1-F70F-11D0-B917-00A0C9223196
		{{ 0xDFF229E1, 0xF70F, 0x11D0, { 0xB9, 0x17, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96 }}, "KSNODETYPE_VIDEO_STREAMING" },
		// E73FACE3-2880-4902-B799-88D0CD634E0F
		{{ 0xe73face3, 0x2880, 0x4902, { 0xb7, 0x99, 0x88, 0xd0, 0xcd, 0x63, 0x4e, 0x0f }}, "KSPROPSETID_VramCapture" },
		// DFF229E5-F70F-11D0-B917-00A0C9223196
		{{ 0xDFF229E5, 0xF70F, 0x11D0, { 0xB9, 0x17, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96 }}, "KSNODETYPE_VIDEO_PROCESSING" },
		// DFF229E6-F70F-11D0-B917-00A0C9223196
		{{ 0xDFF229E6, 0xF70F, 0x11D0, { 0xB9, 0x17, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96 }}, "KSNODETYPE_VIDEO_CAMERA_TERMINAL" },
		// 288296EC-9F94-41B4-A153-AA31AEECB33F
		{{ 0x288296EC, 0x9F94, 0x41B4, { 0xA1, 0x53, 0xAA, 0x31, 0xAE, 0xEC, 0xB3, 0x3F }}, "KSEVENTSETID_Device" },
		// FF6C4BFA-07A9-4c7b-A237-672F9D68065F
		{{ 0xff6c4bfa, 0x07a9, 0x4c7b, { 0xa2, 0x37, 0x67, 0x2f, 0x9d, 0x68, 0x06, 0x5f }}, "KSPROPSETID_MPEG4_MediaType_Attributes" },
	};

	ULONG a = 0, b = _countof(_S_MFGUIDString), o;

	do 
	{
		const GANS* p = &_S_MFGUIDString[o = (a + b) >> 1];
		int i = memcmp(&p->guid, guid, sizeof(guid));
		if (!i)
		{
			return p->name;
		}

		0 > i ? a = o + 1 : b = o;

	} while (a < b);

	return 0;
}

void DumpGuid(const GUID* guid, PCSTR Preffix /*= ""*/, PCSTR Suffix /*= "\n"*/)
{
	if (PCSTR guidname = GetMFGUIDString(guid))
	{
		DbgPrint("%hs%hs%hs", Preffix, guidname, Suffix);
		return ;
	}

	DbgPrint("%hs\"%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X\"%hs", 
		Preffix,
		guid->Data1, guid->Data2, guid->Data3,
		guid->Data4[0],
		guid->Data4[1],
		guid->Data4[2],
		guid->Data4[3],
		guid->Data4[4],
		guid->Data4[5],
		guid->Data4[6],
		guid->Data4[7],
		Suffix
		);
}

void Dump(PRECT prc, PCSTR Preffix /*= ""*/, PCSTR Suffix/* = "\n"*/)
{
	DbgPrint("%hs\"(%u, %u) %u*%u\"%hs", 
		Preffix,
		prc->left, prc->top, prc->right - prc->left, prc->bottom - prc->top,
		Suffix
		);
}

void Dump(PKS_BITMAPINFOHEADER bmiHeader)
{
	DbgPrint("BMP:{%x, %u*%u, %u, %u, %x, %x(%.4hs) }\r\n",	
		bmiHeader->biSize,
		bmiHeader->biWidth,
		bmiHeader->biHeight,
		bmiHeader->biPlanes,
		bmiHeader->biBitCount,
		bmiHeader->biSizeImage,
		bmiHeader->biCompression,
		&bmiHeader->biCompression);
}

void Dump(PKS_VIDEOINFOHEADER VideoInfo)
{
	Dump(&VideoInfo->rcSource, "rcSource=");
	Dump(&VideoInfo->rcTarget, "rcTarget=");

	DbgPrint("dwBitRate=%u, [%u FPS]\r\n",VideoInfo->dwBitRate, 10000000 / VideoInfo->AvgTimePerFrame);
	Dump(&VideoInfo->bmiHeader);
}

void Dump(PKS_VIDEOINFOHEADER2 VideoInfo)
{
	Dump(&VideoInfo->rcSource, "rcSource=");
	Dump(&VideoInfo->rcTarget, "rcTarget=");

	DbgPrint("dwBitRate=%u, [%u FPS]\r\n",VideoInfo->dwBitRate, 10000000 / VideoInfo->AvgTimePerFrame);
	Dump(&VideoInfo->bmiHeader);
}

void Dump(PKSDATAFORMAT DataFormat)
{
	DbgPrint("FormatSize=%x SampleSize=%x Flags=%x\r\n", DataFormat->FormatSize, DataFormat->SampleSize, DataFormat->Flags);
	DumpGuid(&DataFormat->MajorFormat, "MajorFormat=");
	DumpGuid(&DataFormat->SubFormat, "SubFormat=");
	DumpGuid(&DataFormat->Specifier, "Specifier=");
}

void Dump(PKS_DATAFORMAT_VIDEOINFOHEADER p, ULONG cb)
{
	Dump(&p->DataFormat);

	if (KSDATAFORMAT_TYPE_VIDEO == p->DataFormat.MajorFormat)
	{
		if (KSDATAFORMAT_SPECIFIER_VIDEOINFO == p->DataFormat.Specifier)
		{
			if (sizeof(KS_DATAFORMAT_VIDEOINFOHEADER) <= cb)
			{
				Dump(&p->VideoInfoHeader);
				return ;
			}
		}
		else if (KSDATAFORMAT_SPECIFIER_VIDEOINFO2 == p->DataFormat.Specifier)
		{
			if (sizeof(KS_DATAFORMAT_VIDEOINFOHEADER2) <= cb)
			{
				Dump(&reinterpret_cast<PKS_DATAFORMAT_VIDEOINFOHEADER2>(p)->VideoInfoHeader2);
				return ;
			}
		}
	}

	DumpBytes((PBYTE)p, cb);
}

void Dump(PKSPIN_CONNECT p)
{
	Dump(&p->Interface, false);
	Dump(&p->Medium, false);

	DbgPrint("PinId=%x %p Priority{%x, %x}\r\n", p->PinId, p->PinToHandle, p->Priority.PriorityClass, p->Priority.PrioritySubClass);
}

void Dump(PKSEVENTDATA p)
{
	ProbeForRead(p, sizeof(KSEVENTDATA), __alignof(KSEVENTDATA));

	DbgPrint("KSEVENTDATA: { %x, {%p, %p, %p } }\r\n", 
		p->NotificationType, p->Alignment.Unused, p->Alignment.Alignment[0], p->Alignment.Alignment[1]); 
}

void Dump(PKSIDENTIFIER p, bool bProbe /*= true*/)
{
	if (bProbe) ProbeForRead(p, sizeof(KSIDENTIFIER), __alignof(KSIDENTIFIER));

	char buf[64];
	sprintf_s(buf, _countof(buf), ", %08x, %08x }\r\n", p->Id, p->Flags);
	DumpGuid(&p->Set, "KSPROPERTY: { ", buf);
}

void DumpHeader(PKSSTREAM_HEADER psh)
{
	DbgPrint("KSSTREAM_HEADER:{%x, %x, {%I64x, %x, %x }, %I64x, %x, %x, %x, %p}\r\n", 
		psh->Size,
		psh->TypeSpecificFlags,
		psh->PresentationTime.Time,
		psh->PresentationTime.Numerator,
		psh->PresentationTime.Denominator,
		psh->Duration,
		psh->FrameExtent,
		psh->DataUsed,
		psh->OptionsFlags,
		psh->Data
		);
}

void DumpFrame(PKS_FRAME_INFO pfi)
{
	DbgPrint("KS_FRAME_INFO:{%x, %x, %I64x, %I64x, %I64x, %p, %p }\r\n", 
		pfi->ExtendedHeaderSize,
		pfi->dwFrameFlags,
		pfi->PictureNumber,
		pfi->FrameCompletionNumber,
		pfi->DropCount,
		pfi->hDirectDraw,
		pfi->hSurfaceHandle
		);
}
_NT_END