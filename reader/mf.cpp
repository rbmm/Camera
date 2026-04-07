#include "stdafx.h"

_NT_BEGIN

#include "../log/log.h"
#include "../inc/rundown.h"
#include "reader.h"
#include "dump.h"

class SpiReaderCallback : public IMFSourceReaderCallback, public CCameraBase
{
public:
	LONGLONG _M_llTimestamp;
	IMFSourceReader* _M_reader;
	IReaderCallback* _M_pcb = 0;
	LONG _M_dwRef = 1;
	LONG _m_nReadCount = 0;
	RundownProtection _M_rp = RundownProtection::v_init;

	SpiReaderCallback()
	{
		DbgPrint("%x: %hs<%p>\r\n", GetCurrentThreadId(), __FUNCTION__, this);
		GetSystemTimeAsFileTime((PFILETIME)&_M_llTimestamp);
	}

	~SpiReaderCallback()
	{
		DbgPrint("%x: %hs<%p> [%x]\r\n", GetCurrentThreadId(), __FUNCTION__, this, _m_nReadCount);
		if (_M_pcb)
		{
			_M_pcb->OnStop();
			_M_pcb->Release();
		}
	}

	virtual HRESULT STDMETHODCALLTYPE QueryInterface(
		/* [in] */ REFIID riid,
		/* [iid_is][out] */ _COM_Outptr_ void __RPC_FAR* __RPC_FAR* ppvObject)
	{
		if (__uuidof(IUnknown) == riid || __uuidof(IMFSourceReaderCallback) == riid)
		{
			*ppvObject = static_cast<IMFSourceReaderCallback*>(this);
			AddRef();
			return S_OK;
		}

		*ppvObject = 0;

		return E_NOINTERFACE;
	}

	virtual ULONG STDMETHODCALLTYPE AddRef()
	{
		return InterlockedIncrement(&_M_dwRef);
	}

	virtual ULONG STDMETHODCALLTYPE Release()
	{
		if (ULONG n = InterlockedDecrement(&_M_dwRef))
		{
			return n;
		}

		delete this;

		return 0;
	}

	virtual HRESULT STDMETHODCALLTYPE OnReadSample(
		/* [annotation][in] */
		_In_  HRESULT hr,
		/* [annotation][in] */
		_In_  DWORD dwStreamIndex,
		/* [annotation][in] */
		_In_  DWORD /*dwStreamFlags*/,
		/* [annotation][in] */
		_In_  LONGLONG llTimestamp,
		/* [annotation][in] */
		_In_opt_  IMFSample* pSample)
	{
		if (hr)
		{
			DbgPrint("%x: %hs<%p>(%x: %x %p %I64u)\r\n", GetCurrentThreadId(), __FUNCTION__, this,
				dwStreamIndex, hr, pSample, llTimestamp - _M_llTimestamp);
		}

		_M_llTimestamp = llTimestamp;

		InterlockedIncrement(&_m_nReadCount);

		if (hr)
		{
			Stop();
			_M_pcb->OnError(hr);
			return hr;
		}

		if (pSample)
		{
			IMFMediaBuffer* pBuffer;

			if (0 <= pSample->ConvertToContiguousBuffer(&pBuffer))
			{
				PBYTE pb;
				DWORD cb, cbm;
				if (0 <= pBuffer->Lock(&pb, &cbm, &cb))
				{
					_M_pcb->OnReadSample(pb, cb);
					pBuffer->Unlock();
				}
				pBuffer->Release();
			}
		}
		Read();

		return S_OK;
	}

	virtual HRESULT STDMETHODCALLTYPE OnFlush(
		/* [annotation][in] */
		_In_  DWORD dwStreamIndex)
	{
		DbgPrint("%x: %hs<%p>(%x:)\r\n", GetCurrentThreadId(), __FUNCTION__, this, dwStreamIndex);
		return S_OK;
	}

	virtual HRESULT STDMETHODCALLTYPE OnEvent(
		/* [annotation][in] */
		_In_  DWORD dwStreamIndex,
		/* [annotation][in] */
		_In_  IMFMediaEvent* pEvent)
	{
		MediaEventType met;
		pEvent->GetType(&met);
		DbgPrint("%x: %hs<%p>(%x: %x)\r\n", GetCurrentThreadId(), __FUNCTION__, this, dwStreamIndex, met);
		return S_OK;
	}

	_NODISCARD BOOL Lock()
	{
		return _M_rp.Acquire();
	}

	void Unlock()
	{
		if (_M_rp.Release())
		{
			_M_reader->Release();
			_M_reader = 0;
		}
	}

	void Read(ULONG n = 1)
	{
		if (Lock())
		{
			do 
			{
				if (HRESULT hr = _M_reader->ReadSample((ULONG)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, 0, 0, 0, 0))
				{
					OnReadSample(hr, 0, 0, 0, 0);
				}
			} while (--n);

			Unlock();
		}
	}

	virtual HRESULT Start(_In_ IReaderCallback* pcb)
	{
		HRESULT hr;
		IMFMediaType* pType;
		if (0 <= (hr = _M_reader->GetCurrentMediaType((ULONG)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pType)))
		{
			GUID guid = {};
			union {
				UINT64 value = 0;
				struct { ULONG height, width; };
			};

			0 <= (hr = pType->GetGUID(MF_MT_SUBTYPE, &guid)) &&
				0 <= (hr = pType->GetUINT64(MF_MT_FRAME_SIZE, &value));

			pType->Release();

			if (0 <= hr)
			{
				DumpGuid(&guid, "MF_MT_SUBTYPE=");

				ULONG cb;

				if (MFVideoFormat_RGB32 == guid || MFVideoFormat_ARGB32 == guid)
				{
					guid.Data1 = 0;
					cb = 4 * height * width;
				}
				else if (MFVideoFormat_NV12 == guid )
				{
					cb = 12 * height * width >> 3;
				}
				else if (MFVideoFormat_YUY2 == guid)
				{
					cb = 2 * height * width;
				}
				else
				{
					return STATUS_NOT_SUPPORTED;
				}

				if (0 <= (hr = pcb->Init(guid.Data1, width, height, cb)))
				{
					_M_pcb = pcb;
					pcb->AddRef();
					Read(4);
					return S_OK;
				}
			}
		}

		return hr;
	}

public:

	virtual void Stop()
	{
		if (Lock())
		{
			_M_rp.Rundown_l();
			Unlock();
		}
	}

	void Set(IMFSourceReader* reader)
	{
		_M_reader = reader;
	}
};

HRESULT CreateMFCamera(_In_ IMFMediaSource* source, _Out_ CCameraBase** ppcb)
{
	HRESULT hr;
	if (SpiReaderCallback* pcb = new SpiReaderCallback)
	{
		IMFAttributes* attrs;

		if (0 <= (hr = MFCreateAttributes(&attrs, 4)))
		{
			IMFSourceReader* reader = 0;

			0 <= (hr = attrs->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, pcb)) &&
				0 <= (hr = attrs->SetUINT32(MF_LOW_LATENCY, TRUE)) &&
				0 <= (hr = attrs->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, TRUE)) &&
				0 <= (hr = attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, FALSE)) &&
				0 <= (hr = MFCreateSourceReaderFromMediaSource(source, attrs, &reader));

			attrs->Release();

			if (0 <= hr)
			{
				pcb->Set(reader);
				*ppcb = pcb;
				return S_OK;
			}
		}

		pcb->Release();
		return hr;
	}

	return E_OUTOFMEMORY;
}

NTSTATUS StartMFReader(_Out_ CCameraBase** ppSourceReader, _In_ IReaderCallback* pcb, _In_ IMFMediaSource* pSource)
{
	CCameraBase* camera = 0;

	HRESULT hr = CreateMFCamera(pSource, &camera);

	if (S_OK == hr)
	{
		if (0 <= (hr = camera->Start(pcb)))
		{
			*ppSourceReader = camera;
			return S_OK;
		}
		camera->Stop();
		camera->Release();
	}

	return hr;
}

RTL_RUN_ONCE _S_ro;

#pragma intrinsic(_lrotr, _lrotl)

NTSTATUS StartMFReader(_Out_ CCameraBase** ppSourceReader, _In_ IReaderCallback* pcb, _In_ PCWSTR pszDeviceInterface)
{
	union {
		PVOID pv = 0;
		HRESULT hr;
	};

	NTSTATUS status = RtlRunOnceBeginInitialize(&_S_ro, 0, &pv);

	if (STATUS_PENDING == status)
	{
		hr = _lrotl(MFStartup(MF_VERSION), 4) & ~((1 << RTL_RUN_ONCE_CTX_RESERVED_BITS) - 1);
		RtlRunOnceComplete(&_S_ro, 0, pv);
	}

	if (0 > status)
	{
		return status;
	}

	if (0 <= (hr = _lrotr(hr, 4)))
	{
		IMFAttributes* attrs;

		if (0 <= MFCreateAttributes(&attrs, 2))
		{
			IMFMediaSource* pSource = 0;

			if (0 <= (hr = attrs->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, pszDeviceInterface)) &&
				0 <= (hr = attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID)))
			{
				hr = MFCreateDeviceSource(attrs, &pSource);
			}

			attrs->Release();

			if (0 <= hr)
			{
				hr = StartMFReader(ppSourceReader, pcb, pSource);
				pSource->Release();

				if (!hr)
				{
					return S_OK;
				}
			}
		}

		// MFShutdown();
	}

	return hr;
}

_NT_END