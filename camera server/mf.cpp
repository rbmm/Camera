#include "stdafx.h"

_NT_BEGIN

HRESULT SetMFType(
	_In_ IMFTransform* encoder, 
	_In_ UINT width,
	_In_ UINT height,
	_In_ REFERENCE_TIME AvgTimePerFrame, 
	_In_ BOOLEAN bInput)
{
	IMFMediaType* pType;
	HRESULT hr;

	if (0 <= (hr = MFCreateMediaType(&pType)))
	{
		pType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
		pType->SetUINT32(MF_MT_AVG_BITRATE, width * height * 4);
		pType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_MixedInterlaceOrProgressive);
		MFSetAttributeSize(pType, MF_MT_FRAME_SIZE, width, height);

		UINT32 numerator, denominator;

		MFAverageTimePerFrameToFrameRate(AvgTimePerFrame, &numerator, &denominator);
		MFSetAttributeRatio(pType, MF_MT_FRAME_RATE, numerator, denominator);

		if (bInput)
		{
			pType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
			hr = encoder->SetInputType(0, pType, 0);
		}
		else
		{
			pType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
			hr = encoder->SetOutputType(0, pType, 0);
		}
		pType->Release();
	}

	return hr;
}

HRESULT GetEncoder(
	_In_ UINT width,
	_In_ UINT height,
	_In_ REFERENCE_TIME AvgTimePerFrame, 
	_In_ IMFActivate** ppActivate,
	_In_ UINT32 count,
	_Out_ IMFTransform** pencoder, 
	_Out_ IMFMediaEventGenerator** ppeg)
{
	if (count)
	{
		do
		{
			IMFTransform* encoder;
			IMFActivate* pActivate = *ppActivate++;
			if (0 <= pActivate->ActivateObject(IID_PPV_ARGS(&encoder)))
			{
				IMFAttributes* pAttributes;
				if (0 <= encoder->GetAttributes(&pAttributes))
				{
					HRESULT hr = pAttributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
					
					pAttributes->Release();

					if (0 <= hr)
					{
						MFT_OUTPUT_STREAM_INFO StreamInfo = {};
						if (0 <= encoder->GetOutputStreamInfo(0, &StreamInfo) &&
							(StreamInfo.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) &&
							0 <= SetMFType(encoder, width, height, AvgTimePerFrame, FALSE) &&
							0 <= SetMFType(encoder, width, height, AvgTimePerFrame, TRUE))
						{
							IMFMediaEventGenerator* peg;
							if (0 <= encoder->QueryInterface(IID_PPV_ARGS(&peg)))
							{
								if (0 <= encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0))
								{
									*ppeg = peg, * pencoder = encoder;
									ICodecAPI* pCodecApi;
									if (0 <= encoder->QueryInterface(IID_PPV_ARGS(&pCodecApi)))
									{
										VARIANT v = { VT_UI4 };
										pCodecApi->SetValue(&CODECAPI_AVEncMPVDefaultBPictureCount, &v);
										pCodecApi->SetValue(&CODECAPI_AVEncCommonBufferSize, &v);

										v.ulVal = eAVEncCommonRateControlMode_LowDelayVBR;
										pCodecApi->SetValue(&CODECAPI_AVEncCommonRateControlMode, &v);

										v.vt = VT_BOOL;
										v.boolVal = VARIANT_TRUE;
										pCodecApi->SetValue(&CODECAPI_AVEncCommonLowLatency, &v);
										pCodecApi->Release();
									}
									return S_OK;
								}

								peg->Release();
							}
						}
					}
				}
				encoder->Release();
			}
		} while (--count);
	}

	return MF_E_TOPO_CODEC_NOT_FOUND;
}

HRESULT GetEncoder(
	_In_ UINT width,
	_In_ UINT height,
	_In_ REFERENCE_TIME AvgTimePerFrame, 
	_Out_ IMFTransform** pencoder,
	_Out_ IMFMediaEventGenerator** ppeg)
{
	HRESULT hr;
	MFT_REGISTER_TYPE_INFO inputType{ MFMediaType_Video, MFVideoFormat_NV12 };
	MFT_REGISTER_TYPE_INFO outputType{ MFMediaType_Video, MFVideoFormat_H264 };
	IMFActivate** ppActivate;
	UINT32 count;

	if (0 <= (hr = MFTEnumEx(
		MFT_CATEGORY_VIDEO_ENCODER,
		MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER | MFT_ENUM_FLAG_ASYNCMFT,
		&inputType,
		&outputType,
		&ppActivate,
		&count)))
	{
		hr = GetEncoder(width, height, AvgTimePerFrame, ppActivate, count, pencoder, ppeg);
		CoTaskMemFree(ppActivate);
	}

	return hr;
}
_NT_END