#include "stdafx.h"

_NT_BEGIN
#include "log.h"
#include "H264.h"

void H264::Stop(bool bDisconnect)
{
	_M_encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
	_M_encoder->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
	_pTarget->OnStop();
	if (bDisconnect) _pTarget->Disconnect();
}

H264::~H264()
{
	ULONG i = _countof(_M_pSamples);
	do
	{
		if (IMFSample* pSample = _M_pSamples[--i])
		{
			pSample->Release();
		}
	} while (i);

	if (_M_peg)
	{
		_M_peg->Release();
	}

	if (_M_encoder)
	{
		IMFShutdown* p;
		if (0 <= _M_encoder->QueryInterface(IID_PPV_ARGS(&p)))
		{
			p->Shutdown();
			p->Release();
		}
		_M_encoder->Release();
	}

	_pTarget->Release();

	DbgPrint("%hs<%p>\r\n", __FUNCTION__, this);
}

HRESULT STDMETHODCALLTYPE H264::QueryInterface(
	/* [in] */ REFIID riid,
	/* [iid_is][out] */ _COM_Outptr_ void __RPC_FAR* __RPC_FAR* ppvObject)
{
	if (__uuidof(IMFAsyncCallback) == riid || __uuidof(IUnknown) == riid)
	{
		*ppvObject = static_cast<IMFAsyncCallback*>(this);
		AddRef();
		return S_OK;
	}

	*ppvObject = 0;
	return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE H264::GetParameters(
	/* [out] */ __RPC__out DWORD* pdwFlags,
	/* [out] */ __RPC__out DWORD* pdwQueue)
{
	//DbgPrint("%hs<%p>\r\n", __FUNCTION__, this);

	*pdwFlags = MFASYNC_FAST_IO_PROCESSING_CALLBACK;
	*pdwQueue = MFASYNC_CALLBACK_QUEUE_MULTITHREADED;
	return S_OK;
}

HRESULT GetEncoder(
	_In_ UINT width,
	_In_ UINT height,
	_In_ REFERENCE_TIME AvgTimePerFrame,
	_Out_ IMFTransform** pencoder,
	_Out_ IMFMediaEventGenerator** ppeg);

HRESULT H264::Init(LONG cx, LONG cy, ULONG cbFrame, _In_ REFERENCE_TIME AvgTimePerFrame)
{
	_cbFrame = cbFrame, _cx = cx, _cy = cy;

	DbgPrint("%hs<%p>(%ux%u %x)\r\n", __FUNCTION__, this, cx, cy, cbFrame);

	if (0 <= GetEncoder(cx, cy, AvgTimePerFrame, &_M_encoder, &_M_peg))
	{
		ULONG i = _countof(_M_pSamples), n = 0;
		do
		{
			i--;
			IMFSample* pSample;
			if (0 <= MFCreateSample(&pSample))
			{
				IMFMediaBuffer* pBuffer;
				if (0 <= MFCreateAlignedMemoryBuffer(3 * cbFrame >> 2, MF_16_BYTE_ALIGNMENT, &pBuffer))
				{
					if (0 <= pSample->AddBuffer(pBuffer))
					{
						pSample->AddRef();
						_M_pSamples[i] = pSample;
						_bittestandset(&m_packedState, i);
						n++;
					}

					pBuffer->Release();
				}

				pSample->Release();
			}
		} while (i);

		if (n && 0 <= BeginGetEvent())
		{
			return S_OK;
		}
	}

	return E_FAIL;
}

void YUY2ToNV12(ULONG width, ULONG height, const BYTE* yuy2, BYTE* nv12)
{
	ULONG h = 0;
	PBYTE uv_plane = nv12 + width * height;
	do
	{
		ULONG uv_width = width / 2;
		do
		{
			union {
				ULONG u;
				struct {
					UCHAR a, b, c, d;
				};
			};

			u = *((ULONG*&)yuy2)++;

			*nv12++ = a, *nv12++ = c;

			if (!(1 & h))
			{
				*uv_plane++ = b, *uv_plane++ = d;
			}

		} while (--uv_width);

	} while (h++, --height);
}

HRESULT H264::ProcessOutput()
{
	HRESULT hr;
	ULONG dwStatus = 0;
	MFT_OUTPUT_DATA_BUFFER OutputSamples = {};

	switch (hr = _M_encoder->ProcessOutput(0, 1, &OutputSamples, &dwStatus))
	{
	case S_OK:
		if (OutputSamples.pSample)
		{
			IMFMediaBuffer* pBuffer;

			if (0 <= (hr = OutputSamples.pSample->ConvertToContiguousBuffer(&pBuffer)))
			{
				PBYTE pb;
				DWORD cb, cbm;
				if (0 <= (hr = pBuffer->Lock(&pb, &cbm, &cb)))
				{
					InterlockedExchangeAddNoFence(&_cbOut, cb);
					_pTarget->SendUserData('H264', pb, cb);
					pBuffer->Unlock();
					//DbgPrint("HaveOutput([%u] %x)\r\n",cb);
				}
				pBuffer->Release();
			}

			OutputSamples.pSample->Release();
		}
		break;

	case MF_E_TRANSFORM_STREAM_CHANGE:
		DbgPrint("MF_E_TRANSFORM_STREAM_CHANGE\r\n");

		IMFMediaType* pType;
		if (0 <= (hr = _M_encoder->GetOutputAvailableType(0, 0, &pType)))
		{
			hr = _M_encoder->SetOutputType(0, pType, 0);
			pType->Release();
		}
		break;
	default:
		DbgPrint("%x:ProcessOutput=%x\r\n", GetCurrentThreadId(), hr);
		break;
	}

	if (OutputSamples.pEvents)
	{
		OutputSamples.pEvents->Release();
	}

	if (dwStatus)
	{
		DbgPrint("MFT_PROCESS_OUTPUT_STATUS=%x\r\n", dwStatus);
	}

	if (OutputSamples.dwStatus)
	{
		DbgPrint("MFT_OUTPUT_DATA_BUFFER_FLAGS=%x\r\n", OutputSamples.dwStatus);
	}

	return hr;
}

HRESULT H264::ProcessInput(IMFSample* pSample)
{
	HRESULT hr = _M_encoder->ProcessInput(0, pSample, 0);
	//DbgPrint("%x:ProcessInput(%p)=%x\r\n", GetCurrentThreadId(), pSample, hr);
	if (0 <= hr)
	{
		hr = BeginGetEvent();
	}
	return hr;
}

int H264::AcquirePoolSlot()
{
	int i = _countof(_M_pSamples);
	do
	{
		if (_interlockedbittestandreset(&m_packedState, --i))
		{
			return i;
		}
	} while (i);

	return -1;
}

void H264::ReleasePoolSlot(int i)
{
	if (_interlockedbittestandset(&m_packedState, i)) __debugbreak();
}

int H264::TryDequeue()
{
	union {
		LONG packedState = 0;
		struct {
			ULONG m_freePoolSlots : 4;  // Битмап свободных объектов
			ULONG m_writeIdx : 2;       // Индекс записи в очередь
			ULONG m_readIdx : 2;        // Индекс чтения из очереди
			ULONG m_indexQueue : 8;     // Сама очередь (4 слота по 2 бита)
			ULONG m_reserved : 14;
			ULONG m_needInput : 1;      // Флаг: 
			ULONG m_hasData : 1;        // Флаг: в очереди есть элементы
		};
	};

	packedState = m_packedState;

	for (;;)
	{
		LONG old_packedState = packedState;

		int index = -1;

		if (m_hasData)
		{
			ULONG readIdx = m_readIdx;
			ULONG shift = readIdx << 1;
			index = (m_indexQueue & (3 << shift)) >> shift;
			if ((m_readIdx = readIdx + 1) == m_writeIdx)
			{
				m_hasData = FALSE;
			}
		}
		else
		{
			m_needInput = TRUE;
		}

		if (old_packedState == (packedState = InterlockedCompareExchange(&m_packedState, packedState, old_packedState)))
		{
			return index;
		}
	}
}

BOOL H264::TryEnqueue(int index)
{
	union {
		LONG packedState = 0;
		struct {
			ULONG m_freePoolSlots : 4;  // Битмап свободных объектов
			ULONG m_writeIdx : 2;       // Индекс записи в очередь
			ULONG m_readIdx : 2;        // Индекс чтения из очереди
			ULONG m_indexQueue : 8;     // Сама очередь (4 слота по 2 бита)
			ULONG m_reserved : 14;
			ULONG m_needInput : 1;      // Флаг: 
			ULONG m_hasData : 1;        // Флаг: в очереди есть элементы
		};
	};

	packedState = m_packedState;

	for (;;)
	{
		LONG old_packedState = packedState;

		BOOL needInput;

		if (needInput = m_needInput)
		{
			m_needInput = FALSE;
		}
		else
		{
			ULONG writeIdx = m_writeIdx;

			if (m_readIdx == writeIdx && m_hasData)
			{
				__debugbreak();// never must be
			}
			ULONG shift = writeIdx << 1;
			m_indexQueue = (m_indexQueue & ~(3 << shift)) | (index << shift);
			m_hasData = TRUE;
			m_writeIdx = writeIdx + 1;
		}

		if (old_packedState == (packedState = InterlockedCompareExchange(&m_packedState, packedState, old_packedState)))
		{
			return !needInput;
		}
	}
}

HRESULT STDMETHODCALLTYPE H264::Invoke(/* [in] */ __RPC__in_opt IMFAsyncResult* pAsyncResult)
{
	HRESULT hr;
	IMFMediaEvent* pEvent;
	if (0 <= (hr = _M_peg->EndGetEvent(pAsyncResult, &pEvent)))
	{
		MediaEventType met;
		hr = pEvent->GetType(&met);
		pEvent->Release();

		if (0 <= hr)
		{
			int index;

			switch (met)
			{
			case METransformHaveOutput:
				hr = ProcessOutput();
				BeginGetEvent();
				break;

			case METransformNeedInput:
				if (0 <= (index = TryDequeue()))
				{
					ProcessInput(_M_pSamples[index]);
					ReleasePoolSlot(index);
				}
				break;

			default:
				hr = E_UNEXPECTED;
				break;
			}
		}
	}

	if (0 > hr)
	{
		__debugbreak();
		Stop();
	}

	return hr;
}

HRESULT H264::BeginGetEvent()
{
	switch (HRESULT hr = _M_peg->BeginGetEvent(this, 0))
	{
	case S_OK:
		return S_OK;
	default:
		__debugbreak();
		Stop();
		return hr;
	}
}

BOOL H264::ProcessFrame(PBYTE pb, ULONG cb)
{
	if (cb == _cbFrame)
	{
		int index = AcquirePoolSlot();

		if (0 <= index)
		{
			IMFSample* pSample = _M_pSamples[index];

			union {
				FILETIME ft;
				LONGLONG hnsSampleTime;
			};
			GetSystemTimeAsFileTime(&ft);
			if (!_hnsSampleTime)
			{
				_hnsSampleTime = hnsSampleTime;
			}
			pSample->SetSampleTime(hnsSampleTime - _hnsSampleTime);

			BOOLEAN fOk = FALSE;

			IMFMediaBuffer* pBuffer;

			if (0 <= pSample->GetBufferByIndex(0, &pBuffer))
			{
				BYTE* pbBuffer;
				DWORD cbMaxLength, cbCurrentLength;

				if (0 <= pBuffer->Lock(&pbBuffer, &cbMaxLength, &cbCurrentLength))
				{
					if ((cb = 3 * cb >> 2) <= cbMaxLength && 0 <= pBuffer->SetCurrentLength(cb))
					{
						YUY2ToNV12(_cx, _cy, pb, pbBuffer);
						InterlockedExchangeAddNoFence(&_cbIn, cb);
						fOk = TRUE;
					}

					pBuffer->Unlock();
				}

				pBuffer->Release();
			}

			if (fOk)
			{
				if (TryEnqueue(index))
				{
					return TRUE;
				}

				ProcessInput(pSample);
			}

			ReleasePoolSlot(index);
		}
		return TRUE;
	}
	return FALSE;
}

_NT_END