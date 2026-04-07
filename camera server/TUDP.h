#pragma once

#include "../asio/socket.h"
#include "../inc/rundown.h"

class CClientServerR;

struct C_PACKET;
struct P_PACKET;
struct E_PACKET;

void PrintIP(PSOCKADDR_INET from, ULONG cb);

class __declspec(novtable) Endpoint : LIST_ENTRY 
{
	friend CClientServerR;

	enum { e_inactive_time = 15000, max_packet_size = 0x580, e_nb = 4, e_disc_called = 0 };

	struct MSGB 
	{
		LONG64 m_bits;
		ULONG m_M;
		ULONG m_cbMsg;
		LONG m_cbNeed;
		ULONG m_tick;
		UCHAR m_buf[0x40 * Endpoint::max_packet_size];
	};

	ULONG64 m_cookie, m_rcookie = 0;
	ULONG64 m_tick = GetTickCount64() + e_inactive_time;
	LONG64 m_bits = 0;
	BCRYPT_KEY_HANDLE m_hPriv = 0, m_hPub = 0;
	CClientServerR* m_parent;
	SRWLOCK m_lock = {};
	MSGB* m_msgb = 0;
	RundownProtection m_rp;
	Endpoint* m_next = 0;
	ULONG m_PubBlockLength = 0, m_PrivBlockLength = 0;
	LONG m_dwRefCount = 1;
	LONG m_flags = 0;
	SOCKADDR_INET m_sa {};
	ULONG m_t = 0;
	SHORT m_M = 0;

	NTSTATUS LoadPubKey(ULONG64 crc);
	NTSTATUS LoadPrivKey(ULONG64 crc2, ULONG64 crc1);

	NTSTATUS SendMsg(ULONG MsgLength, ULONG type, PBYTE pb, ULONG cb, USHORT M, USHORT N);

	NTSTATUS OnData(E_PACKET* packet, ULONG cb);
	NTSTATUS OnConnect(ULONG64 crc, ULONG64 cookie);
	NTSTATUS Accept(PSOCKADDR_INET psi, ULONG64 crc2, ULONG64 crc1, ULONG64 crc, ULONG64 cookie);
	NTSTATUS Init(PSOCKADDR_INET psi, ULONG64 crc2, ULONG64 crc1, ULONG64 cookie = 0);

	virtual NTSTATUS Accept(PSOCKADDR_INET psi) = 0;
	virtual NTSTATUS OnConnect() = 0;
	virtual void OnDisconnect() = 0;
	virtual NTSTATUS OnUserData(ULONG type, PBYTE pb, ULONG cb) = 0;

	MSGB* get(ULONG M, ULONG cbMsg);

	void notify_disconnect();

protected:

	virtual ~Endpoint();

public:

	Endpoint(CClientServerR* parent);

	void AddRef()
	{
		InterlockedIncrementNoFence(&m_dwRefCount);
	}

	void Release()
	{
		if (!InterlockedDecrement(&m_dwRefCount))
		{
			delete this;
		}
	}

	NTSTATUS Connect(PSOCKADDR_INET psi, ULONG64 crc2, ULONG64 crc1);
	NTSTATUS SendUserData(ULONG type, PBYTE pb = 0, ULONG cb = 0);

	void Disconnect();
	void ping();

	void SetDisconnectTime(ULONG t)
	{
		m_tick = GetTickCount64() + t;
	}
};

class __declspec(novtable) CClientServerR : public CUdpEndpoint, LIST_ENTRY
{
	friend Endpoint;

	ULONG64 m_crc2, m_crc1;
	PCSTR m_name;
	BCRYPT_ALG_HANDLE m_hAesProv = 0;
	SRWLOCK m_lock = {};
	PTP_TIMER _M_Timer = 0;
	LONG m_nPackets = 0;

	static VOID NTAPI _onTimer(
		_Inout_     PTP_CALLBACK_INSTANCE /*Instance*/,
		_Inout_opt_ PVOID                 Context,
		_Inout_     PTP_TIMER             /*Timer*/
	);

	void OnTimer();
	void deleteTimer();

	void RemoveConnection(Endpoint* p);
	void AddConnection(Endpoint* p);

	Endpoint* get(ULONG64 cookie);

	void AddListen();

	virtual void OnRecv(PSTR buf, ULONG cb, CDataPacket* packet, SOCKADDR_IN_EX* from);

	void OnConnect(C_PACKET* packet, PSOCKADDR_INET psi);
	void OnDisconnect(ULONG64 cookie, PSOCKADDR_INET psi);
	void OnPing(P_PACKET* packet, PSOCKADDR_INET psi);
	void OnData(PBYTE pb, ULONG cb, PSOCKADDR_INET psi);

	virtual Endpoint* CreateEndpoint() = 0;

public:
	void Stop();

	CClientServerR(ULONG64 crc2, ULONG64 crc1, PCSTR name) : m_name(name), m_crc2(crc2), m_crc1(crc1)
	{
		DbgPrint("%hs<%p>\r\n", __FUNCTION__, this);
		InitializeListHead(this);
	}

	~CClientServerR()
	{
		deleteTimer();
		if (m_hAesProv) BCryptCloseAlgorithmProvider(m_hAesProv, 0);
		DbgPrint("%hs<%p>\r\n", __FUNCTION__, this);
	}

	BOOL Start(ULONG n, PSOCKADDR_INET psi);

	NTSTATUS SetTimer(ULONG Period);
};