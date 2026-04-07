#pragma once

#include "TUDP.h"

class KsRead;

class CEncTcp : public Endpoint
{
	using Endpoint::Endpoint;

	PWSTR _M_buf = 0;
	KsRead* _pin = 0;

	NTSTATUS Stop();

	NTSTATUS Start(PWSTR pszDeviceInterface, PKS_DATARANGE_VIDEO pDRVideo);

	NTSTATUS Refresh();

	virtual NTSTATUS Accept(PSOCKADDR_INET /*psi*/);

	virtual NTSTATUS OnConnect();

	virtual void OnDisconnect();

	virtual NTSTATUS OnUserData(ULONG type, PBYTE pb, ULONG cb);

	virtual ~CEncTcp();

public:

	void OnStop()
	{
		DbgPrint("%hs<%p>\r\n", __FUNCTION__, this);
		SendUserData('stop');
	}
};

