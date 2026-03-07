#pragma once

class H264
{
	LONGLONG m_hnsSampleTime = 0;
	IMFTransform* m_decoder = 0;
	IMFMediaBuffer* m_pBuffer = 0;
	MFT_OUTPUT_DATA_BUFFER m_out = {};

	ULONG _cx, _cy;

	HRESULT GetOutput(_Out_ PULONG rgba);

public:
	HRESULT Init(ULONG cx, ULONG cy);
	HRESULT OnFrame(_Out_ PULONG rgba, _In_ const void* pv, _In_ ULONG cb);

	~H264();
};
