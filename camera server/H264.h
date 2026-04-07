#pragma once

#include "userendp.h"

class H264 : public IMFAsyncCallback
{
	LONGLONG _hnsSampleTime = 0;
	CEncTcp* _pTarget = 0;
	IMFTransform* _M_encoder = 0;
	IMFMediaEventGenerator* _M_peg = 0;
	IMFSample* _M_pSamples[4] = {};

	union {
		LONG m_packedState = 0;
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

	ULONG _cbFrame = 0, _cx = 0, _cy = 0;
	LONG _cbIn = 0, _cbOut = 0;
	LONG _dwRef = 1;

	virtual ~H264();

	HRESULT BeginGetEvent();
	HRESULT ProcessOutput();
	HRESULT ProcessInput(IMFSample* pSample);

	int AcquirePoolSlot();
	void ReleasePoolSlot(int index);
	int TryDequeue();
	BOOL TryEnqueue(int index);

	virtual HRESULT STDMETHODCALLTYPE GetParameters(
		/* [out] */ __RPC__out DWORD* pdwFlags,
		/* [out] */ __RPC__out DWORD* pdwQueue);

	virtual HRESULT STDMETHODCALLTYPE Invoke(/* [in] */ __RPC__in_opt IMFAsyncResult* pAsyncResult);

	virtual HRESULT STDMETHODCALLTYPE QueryInterface(
		/* [in] */ REFIID riid,
		/* [iid_is][out] */ _COM_Outptr_ void __RPC_FAR* __RPC_FAR* ppvObject);

public:

	virtual ULONG AddRef()
	{
		return InterlockedIncrementNoFence(&_dwRef);
	}

	virtual ULONG Release()
	{
		if (ULONG dwRef = InterlockedDecrement(&_dwRef))
		{
			return dwRef;
		}
		delete this;
		return 0;
	}

	BOOL ProcessFrame(PBYTE pb, ULONG cb);

	HRESULT Init(LONG cx, LONG cy, ULONG cbFrame, _In_ REFERENCE_TIME AvgTimePerFrame);

	void Stop(bool bDisconnect = true);

	H264(CEncTcp* pTarget) : _pTarget(pTarget)
	{
		pTarget->AddRef();

		DbgPrint("%hs<%p>\r\n", __FUNCTION__, this);
	}
};
