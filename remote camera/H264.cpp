#include "stdafx.h"

_NT_BEGIN

#include "log.h"
#include "H264.h"

HRESULT SetInputType(_In_ IMFTransform* decoder)
{
	IMFMediaType* pType;
	HRESULT hr;

	if (0 <= (hr = MFCreateMediaType(&pType)))
	{
		pType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
		pType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_MixedInterlaceOrProgressive);
		MFSetAttributeRatio(pType, MF_MT_FRAME_RATE, 30, 1);
		MFSetAttributeRatio(pType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
		pType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
		hr = decoder->SetInputType(0, pType, 0);
		pType->Release();
	}

	return hr;
}

HRESULT SetOutputType(IMFTransform* transform, GUID format = MFVideoFormat_NV12)
{
	HRESULT hr;
	ULONG i = 0;
	do
	{
		IMFMediaType* pType;
		if (0 > (hr = transform->GetOutputAvailableType(0, i++, &pType)))
		{
			break;
		}
		hr = MF_E_TOPO_CODEC_NOT_FOUND;
		GUID guid;
		if (0 <= pType->GetGUID(MF_MT_SUBTYPE, &guid) && guid == format)
		{
			hr = transform->SetOutputType(0, pType, 0);
		}
		pType->Release();
	} while (0 > hr);

	return hr;
}

class DECLSPEC_UUID("62CE7E72-4C71-4d20-B15D-452831A87D9D") CMSH264DecoderMFT;

HRESULT GetDecoder(_Out_ IMFTransform** pdecoder)
{
	HRESULT hr;
	IMFTransform* decoder;
	if (0 <= (hr = CoCreateInstance(__uuidof(CMSH264DecoderMFT), 0, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&decoder))))
	{
		if (0 <= (hr = SetInputType(decoder)) && 0 <= (hr = SetOutputType(decoder)))
		{
			*pdecoder = decoder;
			return S_OK;
		}
		decoder->Release();
	}

	return hr;
}

H264::~H264()
{
	if (m_out.pSample)
	{
		m_out.pSample->Release();
	}

	if (m_decoder)
	{
		m_decoder->Release();
	}
}

HRESULT H264::Init(ULONG cx, ULONG cy)
{
	DbgPrint("H264::Init(%u, %u)\r\n", cx, cy);
	HRESULT hr;
	if (0 <= (hr = GetDecoder(&m_decoder)))
	{
		if (0 <= MFCreateSample(&m_out.pSample))
		{
			IMFMediaBuffer* pBuffer;
			if (0 <= (hr = MFCreateAlignedMemoryBuffer(3 * cx * cy >> 1, MF_16_BYTE_ALIGNMENT, &pBuffer)))
			{
				if (0 <= (hr = m_out.pSample->AddBuffer(pBuffer)))
				{
					m_pBuffer = pBuffer;
					_cx = cx, _cy = cy;
				}

				pBuffer->Release();
			}
		}
	}
	return hr;
}

void NV12toRGBA(PBYTE yPlane, PULONG prgba, ULONG cx, ULONG cy);

HRESULT H264::GetOutput(_Out_ PULONG rgba)
{
	ULONG dwStatus;

	for (;;)
	{
		switch (HRESULT hr = m_decoder->ProcessOutput(0, 1, &m_out, &dwStatus))
		{
		default:
			DbgPrint("!! hr=%x, %x, %x\r\n", hr, dwStatus, m_out.dwStatus);
			//__debugbreak();
		case MF_E_TRANSFORM_NEED_MORE_INPUT:
			//DbgPrint("[[MF_E_TRANSFORM_NEED_MORE_INPUT]]\r\n");
			return hr;

		case MF_E_TRANSFORM_STREAM_CHANGE:
			DbgPrint("[[MF_E_TRANSFORM_STREAM_CHANGE]]\r\n");
			if (0 > (hr = SetOutputType(m_decoder, MFVideoFormat_NV12)))
			{
				//__debugbreak();
			}
			return hr;

		case S_OK:
			BYTE* pb;
			DWORD cbMaxLength, cbCurrentLength;
			if (m_out.pEvents)
			{
				m_out.pEvents->Release();
				m_out.pEvents = nullptr; // Обязательно обнулить для следующего вызова
			}
			if (0 <= (hr = m_pBuffer->Lock(&pb, &cbMaxLength, &cbCurrentLength)))
			{
				//DbgPrint("FRAME<%x>: %p\r\n", n++, pb);
				if (3*_cx*_cy == cbCurrentLength << 1)
				{
					NV12toRGBA(pb, rgba, _cx, _cy);
				}
				m_pBuffer->Unlock();
				m_pBuffer->SetCurrentLength(0);
				continue;
			}
			return hr;
		}
	}
}

HRESULT H264::OnFrame(_Out_ PULONG rgba, _In_ const void* pv, _In_ ULONG cb)
{
	//DbgPrint("OnFrame: %x\r\n", cb);

	HRESULT hr;
	IMFSample* pSample;
	if (0 <= (hr = MFCreateSample(&pSample)))
	{
		union {
			FILETIME ft;
			LONGLONG hnsSampleTime;
		};
		GetSystemTimeAsFileTime(&ft);
		if (!m_hnsSampleTime)
		{
			m_hnsSampleTime = hnsSampleTime;
		}
		pSample->SetSampleTime(hnsSampleTime - m_hnsSampleTime);

		IMFMediaBuffer* pBuffer;

		if (0 <= (hr = MFCreateAlignedMemoryBuffer(cb, MF_16_BYTE_ALIGNMENT, &pBuffer)))
		{
			PBYTE pb;
			ULONG cbMaxLength, cbCurrentLength;
			if (0 <= (hr = pBuffer->Lock(&pb, &cbMaxLength, &cbCurrentLength)))
			{
				if (cb <= cbMaxLength)
				{
					memcpy(pb, pv, cb);
				}
				else
				{
					hr = STATUS_BUFFER_TOO_SMALL;
				}
				pBuffer->Unlock();

				if (0 <= hr && 0 <= (hr = pBuffer->SetCurrentLength(cb)) && 0 <= (hr = pSample->AddBuffer(pBuffer)))
				{
					int n = 0;
					while (MF_E_NOTACCEPTING == (hr = m_decoder->ProcessInput(0, pSample, 0)))
					{
						DbgPrint("[[MF_E_NOTACCEPTING]] %x\r\n", n++);
						switch (hr = GetOutput(rgba))
						{
						case S_OK:
						case MF_E_TRANSFORM_NEED_MORE_INPUT:
							continue;
						//default:
							//__debugbreak();
						}
					}
				}
			}

			pBuffer->Release();
		}
		pSample->Release();
	}

	switch (hr)
	{
	default:
		DbgPrint("[[ProcessInput]]=%x\r\n", hr);
		//__debugbreak();
		break;

	case S_OK:
		switch (hr = GetOutput(rgba))
		{
		case S_OK:
		case MF_E_TRANSFORM_NEED_MORE_INPUT:
			return S_OK;
		}
		DbgPrint("[[GetOutput]]=%x\r\n", hr);
		//__debugbreak();
		break;
	}

	return hr;
}

_NT_END