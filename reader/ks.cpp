#include "stdafx.h"

_NT_BEGIN

#include "../log/log.h"
#include "../inc/rundown.h"
#include "reader.h"
#include "utils.h"
#include "dump.h"
#include "KsRead.h"

// one of possible implementation:

int compare(_In_ PKS_DATARANGE_VIDEO pDRVideo1, _In_ PKS_DATARANGE_VIDEO pDRVideo2)
{
	ULONG a1 = '2YUY' - pDRVideo1->VideoInfoHeader.bmiHeader.biCompression;
	ULONG a2 = '2YUY' - pDRVideo2->VideoInfoHeader.bmiHeader.biCompression;

	if (a1 < a2) return +1;
	if (a1 > a2) return -1;

	enum { eBestSampleSize = 640 * 480 * 2 };

	a1 = pDRVideo1->DataRange.SampleSize - eBestSampleSize;
	a2 = pDRVideo2->DataRange.SampleSize - eBestSampleSize;

	if (a1 < a2) return +1;
	if (a1 > a2) return -1;

	return 0;
}

struct FindBestVideo : public EnumVideoFormats
{
	PKS_DATARANGE_VIDEO pBestDRVideo;
	PCWSTR lpInterfaceName = 0;
	BOOL bFound = FALSE;

	virtual void OnList(_In_ PKSMULTIPLE_ITEM pCategories);

	FindBestVideo(PKS_DATARANGE_VIDEO pBestDRVideo) : pBestDRVideo(pBestDRVideo)
	{
	}
};

void FindBestVideo::OnList(PKSMULTIPLE_ITEM pCategories)
{
	if (ULONG Count = pCategories->Count)
	{
		ULONG Size = pCategories->Size - sizeof(KSMULTIPLE_ITEM);

		union {
			PVOID pv;
			PBYTE pb;
			PKSDATARANGE pVideoDataRanges;
			PKS_DATARANGE_VIDEO pDRVideo;
		};

		pv = pCategories + 1;

		ULONG FormatSize;

		do
		{
			if (Size < sizeof(KSDATARANGE) ||
				(FormatSize = pVideoDataRanges->FormatSize) < sizeof(KSDATARANGE) ||
				Size < (FormatSize = ((FormatSize + __alignof(KSDATARANGE) - 1) & ~(__alignof(KSDATARANGE) - 1)))
				)
			{
				break;
			}

			Size -= FormatSize;

			//DbgPrint("{ FormatSize=%x Flags=%x SampleSize=%x } [remaing=%x]\r\n",
			//	pVideoDataRanges->FormatSize,
			//	pVideoDataRanges->Flags,
			//	pVideoDataRanges->SampleSize,
			//	Size);

			//DumpGuid(&pVideoDataRanges->MajorFormat, "MajorFormat=");
			//DumpGuid(&pVideoDataRanges->SubFormat, "SubFormat=");
			//DumpGuid(&pVideoDataRanges->Specifier, "Specifier=");

			if (pVideoDataRanges->FormatSize >= sizeof(KS_DATARANGE_VIDEO) &&
				pVideoDataRanges->Specifier == KSDATAFORMAT_SPECIFIER_VIDEOINFO &&
				pVideoDataRanges->MajorFormat == KSDATAFORMAT_TYPE_VIDEO &&
				IsCorresponds(&pDRVideo->ConfigCaps, &pDRVideo->VideoInfoHeader))
			{
				if (0 < compare(pDRVideo, pBestDRVideo))
				{
					Dump(&pDRVideo->VideoInfoHeader);
					*pBestDRVideo = *pDRVideo;
					bFound = TRUE;
				}
			}

		} while (pb += FormatSize, --Count);
	}
}

PCWSTR FindBestVideoRange(_In_ PCWSTR Buffer, _Inout_ PKS_DATARANGE_VIDEO pBestDRVideo, BOOL bSingle = FALSE)
{
	enum { cb_buf = 0x10000 };

	FindBestVideo fbv(pBestDRVideo);

	if (PKSMULTIPLE_ITEM pCategories = (PKSMULTIPLE_ITEM)LocalAlloc(LMEM_FIXED, cb_buf))
	{
		while (*Buffer)
		{
			DbgPrint("%ws\r\n", Buffer);

			if (!IsRDPSource(Buffer))
			{
				fbv.DoEnum(Buffer, pCategories, cb_buf);

				if (fbv.bFound)
				{
					fbv.bFound = FALSE;
					fbv.lpInterfaceName = Buffer;
				}
			}

			if (bSingle)
			{
				break;
			}
			Buffer += wcslen(Buffer) + 1;
		}

		LocalFree(pCategories);
	}

	return fbv.lpInterfaceName;
}

NTSTATUS StartKFReader(
	_Out_ CCameraBase** ppSourceReader,
	_In_ IReaderCallback* pcb,
	_In_ PCWSTR pszDeviceInterface,
	_In_ PKS_DATARANGE_VIDEO pDRVideo,
	_Out_ KS_READ_MODE* mode)
{
	NTSTATUS status;

	if (KsRead* pSourceReader = new KsRead)
	{
		if (0 <= (status = pSourceReader->Create(pszDeviceInterface, pDRVideo, mode)))
		{
			if (0 <= (status = pSourceReader->Start(pcb)))
			{
				*ppSourceReader = pSourceReader;

				return STATUS_SUCCESS;
			}
		}

		pSourceReader->Release();
	}

	return STATUS_NO_MEMORY;
}

NTSTATUS StartMFReader(_Out_ CCameraBase** ppSourceReader, _In_ IReaderCallback* pcb, _In_ PCWSTR pszDeviceInterface);

NTSTATUS StartReader(
	_Out_ CCameraBase** ppSourceReader, 
	_In_ BOOL bRdp, 
	_In_ IReaderCallback* pcb,
	_Out_ PULONG HashValue,
	_Out_opt_ PWSTR* FriendlyName,
	_Out_ PULONG p_vid_pid,
	_In_ PCWSTR pszDeviceInterface,
	_In_ BOOL bSingle = FALSE)
{
	HRESULT hr = STATUS_NOT_FOUND;

	if (bRdp)
	{
		while (*pszDeviceInterface)
		{
			if ((0 > bRdp) ^ IsRDPSource(pszDeviceInterface))
			{
				if (!(hr = StartMFReader(ppSourceReader, pcb, pszDeviceInterface)))
				{
					*p_vid_pid = GetVidPid(pszDeviceInterface);
					HashString(pszDeviceInterface, HashValue);
					if (FriendlyName) GetFriendlyName(FriendlyName, pszDeviceInterface);
					break;
				}
			}

			if (bSingle)
			{
				break;
			}
			pszDeviceInterface += wcslen(pszDeviceInterface) + 1;
		}
	}
	else
	{
		KS_DATARANGE_VIDEO BestDRVideo = {};

		if (pszDeviceInterface = FindBestVideoRange(pszDeviceInterface, &BestDRVideo, bSingle))
		{
			DbgPrint("SELECT: %ws\r\n%.4hs [%ux%u]\r\n", pszDeviceInterface,
				&BestDRVideo.VideoInfoHeader.bmiHeader.biCompression,
				BestDRVideo.VideoInfoHeader.bmiHeader.biWidth,
				BestDRVideo.VideoInfoHeader.bmiHeader.biHeight);

			BestDRVideo.VideoInfoHeader.AvgTimePerFrame = BestDRVideo.ConfigCaps.MaxFrameInterval;

			HashString(pszDeviceInterface, HashValue);
			KS_READ_MODE mode;
			if (!(hr = StartKFReader(ppSourceReader, pcb, pszDeviceInterface, &BestDRVideo, &mode)))
			{
				*p_vid_pid = GetVidPid(pszDeviceInterface);
				if (FriendlyName) GetFriendlyName(FriendlyName, pszDeviceInterface);
			}
		}
	}

	return hr;
}

NTSTATUS StartReader(
	_Out_ CCameraBase** ppSourceReader,
	_In_ BOOL bRdp,
	_In_ IReaderCallback* pcb,
	_Out_ PULONG HashValue,
	_Out_opt_ PWSTR* FriendlyName,
	_Out_ PULONG p_vid_pid,
	_In_ PCWSTR pszDeviceInterface)
{
	if (pszDeviceInterface)
	{
		return StartReader(ppSourceReader, bRdp, pcb, HashValue, FriendlyName, p_vid_pid, pszDeviceInterface, TRUE);
	}

	PZZWSTR Buffer;

	if (CONFIGRET cr = GetInterfaces(&Buffer, const_cast<GUID*>(&KSCATEGORY_VIDEO_CAMERA)))
	{
		DbgPrint("cr=%x\r\n", cr);
		return CM_MapCrToWin32Err(cr, ERROR_GEN_FAILURE);
	}

	HRESULT hr = StartReader(ppSourceReader, bRdp, pcb, HashValue, FriendlyName, p_vid_pid, Buffer, FALSE);

	LocalFree(Buffer);

	return hr;
}

NTSTATUS HashString(_In_ PCWSTR pszDeviceInterface, _Out_ PULONG HashValue)
{
	UNICODE_STRING str;
	RtlInitUnicodeString(&str, pszDeviceInterface);
	return RtlHashUnicodeString(&str, FALSE, HASH_STRING_ALGORITHM_DEFAULT, HashValue);
}

_NT_END