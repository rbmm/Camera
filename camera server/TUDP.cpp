#include "stdafx.h"

_NT_BEGIN
#include "log.h"
#include "TUDP.h"
#include "file.h"

NTSTATUS GetBlockLength(_In_ BCRYPT_KEY_HANDLE hKey, _Out_ PULONG BlockLength)
{
	ULONG cb;
	return BCryptGetProperty(hKey, BCRYPT_BLOCK_LENGTH, (PBYTE)BlockLength, sizeof(ULONG), &cb, 0);
}

NTSTATUS LoadKey(
				 _In_ PCWSTR pszKey, 
				 _In_ PCWSTR pszBlobType, 
				 _Out_ BCRYPT_KEY_HANDLE *phKey, 
				 _Out_ PULONG BlockLength)
{
	PBYTE pb;
	ULONG cb;
	NTSTATUS status;
	if (0 <= (status = ReadFromFile(pszKey, &pb, &cb)))
	{
		BCRYPT_ALG_HANDLE hAlgorithm;
		if (0 <= (status = BCryptOpenAlgorithmProvider(&hAlgorithm, BCRYPT_RSA_ALGORITHM, 0, 0)))
		{
			status = BCryptImportKeyPair(hAlgorithm, 0, pszBlobType, phKey, pb, cb, 0);

			BCryptCloseAlgorithmProvider(hAlgorithm, 0);

			if (0 <= status)
			{
				status = GetBlockLength(*phKey, BlockLength);
			}
		}
		LocalFree(pb);
	}

	return status;
}

NTSTATUS GenAesKey(_Out_ BCRYPT_KEY_HANDLE* phKey, BCRYPT_ALG_HANDLE hAesProv, PBYTE pbSecret, ULONG cbSecret)
{
	NTSTATUS status;
	BCRYPT_KEY_HANDLE hKey;

	if (0 <= (status = BCryptGenerateSymmetricKey(hAesProv, &hKey, 0, 0, pbSecret, cbSecret, 0)))
	{
		if (0 <= (status = BCryptSetProperty(hKey, BCRYPT_CHAINING_MODE, 
			(PBYTE)BCRYPT_CHAIN_MODE_CFB, sizeof(BCRYPT_CHAIN_MODE_CFB), 0)))
		{
			*phKey = hKey;
			return STATUS_SUCCESS;
		}

		BCryptDestroyKey(hKey);
	}

	return status;
}

bool operator==(const SOCKADDR_INET& psi1, const SOCKADDR_INET& psi2)
{
	if (psi1.si_family == psi2.si_family)
	{
		switch (psi1.si_family)
		{
		case AF_INET:
			return !memcmp(&psi1.Ipv4, &psi2.Ipv4, sizeof(SOCKADDR_IN));
		case AF_INET6:
			return !memcmp(&psi1.Ipv6, &psi2.Ipv6, sizeof(SOCKADDR_IN6));
		}
	}
	return false;
}

void PrintIP(PSOCKADDR_INET from, ULONG cb)
{
	CHAR sz[64];
	ULONG cch = _countof(sz);
	NTSTATUS status = -1;

	switch (from->si_family)
	{
	case AF_INET:
		status = RtlIpv4AddressToStringExA(&from->Ipv4.sin_addr, from->Ipv4.sin_port, sz, &cch);
		break;
	case AF_INET6:
		status = RtlIpv6AddressToStringExA(&from->Ipv6.sin6_addr, 
			from->Ipv6.sin6_scope_id ,from->Ipv6.sin6_port, sz, &cch);
		break;
	}

	if (0 <= status)
	{
		DbgPrint("%u bytes from %hs\r\n", cb, sz);
	}
}

struct C_PACKET 
{
	enum : ULONG64 { e_magic = 'AP_C' + 'TEKC'*0x100000000ULL } magic;
	ULONG64 crcKey;
	ULONG64 mcookie;
	ULONG64 rcookie;
};

struct P_PACKET
{
	enum : ULONG64 { e_magic = 'GNIP' + 'GNIP' * 0x100000000ULL } magic;
	ULONG64 crc;
};

struct E_PACKET 
{
	ULONG64 crc;
	union {
		UCHAR zero[4];
		ULONG DataLength;// _byteswap_ulong
	};
	ULONG MsgLength;
	ULONG type;
	USHORT M;
	USHORT N;
	UCHAR aes[0x20];
	UCHAR Buf[];

	static ULONG size(ULONG BlockLength, ULONG DataLength)
	{
		ULONG a = offsetof(E_PACKET, zero) + BlockLength, b = offsetof(E_PACKET, Buf) + DataLength;
		return __max(a, b);
	}

	NTSTATUS Encrypt(BCRYPT_KEY_HANDLE hKey, ULONG BlockLength)
	{
		DataLength = _byteswap_ulong(DataLength);
		return BCryptEncrypt(hKey, zero, BlockLength, 0, 0, 0, zero, BlockLength, &BlockLength, BCRYPT_PAD_NONE);
	}

	NTSTATUS Decrypt(BCRYPT_KEY_HANDLE hKey, ULONG BlockLength)
	{
		NTSTATUS status = BCryptDecrypt(hKey, zero, BlockLength, 0, 0, 0, zero, BlockLength, &BlockLength, BCRYPT_PAD_NONE);
		DataLength = _byteswap_ulong(DataLength);
		return status;
	}

	NTSTATUS Encrypt(_In_ BCRYPT_ALG_HANDLE hAesProv, _In_ PBYTE pb, _In_ ULONG cb, _Out_ PULONG pcb)
	{
		BCRYPT_KEY_HANDLE hKey;
		NTSTATUS status;

		if (0 <= (status = BCryptGenRandom(0, aes, sizeof(aes), BCRYPT_USE_SYSTEM_PREFERRED_RNG)) &&
			0 <= (status = GenAesKey(&hKey, hAesProv, aes, sizeof(aes))))
		{
			status = BCryptEncrypt(hKey, pb, cb, 0, 0, 0, Buf, cb, pcb, 0);

			BCryptDestroyKey(hKey);

			DataLength = *pcb;
		}

		return status;
	}

	NTSTATUS Decrypt(_In_ BCRYPT_ALG_HANDLE hAesProv, _In_ PBYTE pb, _In_ ULONG cb, _Out_ PULONG pcb)
	{
		if (ULONG Length = DataLength)
		{
			if (offsetof(E_PACKET, Buf) + Length > cb)
			{
				return STATUS_BAD_DATA;
			}

			NTSTATUS status;
			BCRYPT_KEY_HANDLE hKey;
			if (0 <= (status = GenAesKey(&hKey, hAesProv, aes, sizeof(aes))))
			{
				status = BCryptDecrypt(hKey, Buf, Length, 0, 0, 0, pb, Length, pcb, 0);
				BCryptDestroyKey(hKey);
			}

			return status;
		}
		
		*pcb = 0;
		return 0;
	}
};

C_ASSERT(sizeof(E_PACKET)==0x38);

//////////////////////////////////////////////////////////////////////////
// Endpoint

Endpoint::~Endpoint()
{
	if (m_hPub) BCryptDestroyKey(m_hPub);
	if (m_hPriv) BCryptDestroyKey(m_hPriv);
	if (m_msgb) delete [] m_msgb;
	m_parent->Release();
	DbgPrint("%hs<%p>(%hs)\r\n", __FUNCTION__, this, m_parent->m_name);
}

Endpoint::Endpoint(CClientServerR* parent) : m_parent(parent)
{
	DbgPrint("%hs<%p>(%hs)\r\n", __FUNCTION__, this, parent->m_name);
	parent->AddRef();
	BCryptGenRandom(0, (PBYTE)&m_cookie, sizeof(m_cookie), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
}

NTSTATUS Endpoint::Connect(PSOCKADDR_INET psi, ULONG64 crc2, ULONG64 crc1)
{
	SetDisconnectTime(e_inactive_time);

	m_parent->AddConnection(this);

	if (NTSTATUS status = Init(psi, crc2, crc1))
	{
		m_parent->RemoveConnection(this);
		return status;
	}

	return STATUS_SUCCESS;
}

void Endpoint::Disconnect()
{
	DbgPrint("%hs<%p>\r\n", __FUNCTION__, this);
	m_parent->SendTo(&m_sa, &m_rcookie, sizeof(m_rcookie));
	m_parent->RemoveConnection(this);
}

void Endpoint::ping()
{
	P_PACKET packet = { P_PACKET::e_magic, m_rcookie };
	m_parent->SendTo(&m_sa, &packet, sizeof(packet));
}

NTSTATUS Endpoint::LoadPubKey(ULONG64 crc)
{
	WCHAR name[18];
	if (0 < swprintf_s(name, _countof(name), L"%016I64x", crc))
	{
		return LoadKey(name, BCRYPT_RSAPUBLIC_BLOB, &m_hPub, &m_PubBlockLength);
	}

	return STATUS_INTERNAL_ERROR;
}

NTSTATUS Endpoint::LoadPrivKey(ULONG64 crc2, ULONG64 crc1)
{
	WCHAR name[34];
	if (0 < swprintf_s(name, _countof(name), L"%016I64x%016I64x", crc2, crc1))
	{
		return LoadKey(name, BCRYPT_RSAPRIVATE_BLOB, &m_hPriv, &m_PrivBlockLength);
	}

	return STATUS_INTERNAL_ERROR;
}

Endpoint::MSGB* Endpoint::get(ULONG M, ULONG cbMsg)
{
	ULONG i = e_nb;
	MSGB* msgb = m_msgb, *_msgb = 0, *__msgb = 0;
	ULONG tick = GetTickCount(), _dt = 0, dt;
	do 
	{
		if (msgb->m_M == M)
		{
			return cbMsg == msgb->m_cbMsg ? msgb : 0;
		}

		if (!_msgb && !msgb->m_bits)
		{
			_msgb = msgb;
		}

		if (_dt < (dt = tick - msgb->m_tick))
		{
			_dt = dt;
			if (999 < dt)
			{
				__msgb = msgb;
			}
		}
		
	} while (msgb++, --i);

	if (_msgb || (_msgb = __msgb))
	{
		_msgb->m_cbMsg = cbMsg;
		_msgb->m_cbNeed = cbMsg;
		_msgb->m_M = M;
	}

	return _msgb;
}

NTSTATUS Endpoint::OnData(E_PACKET* packet, ULONG cb)
{
	if (offsetof(E_PACKET, zero) + m_PrivBlockLength > cb)
	{
		return STATUS_BUFFER_TOO_SMALL;
	}

	NTSTATUS status;
	if (0 <= (status = packet->Decrypt(m_hPriv, m_PrivBlockLength)))
	{
		SetDisconnectTime(e_inactive_time);

		AcquireSRWLockExclusive(&m_lock);
		MSGB* msgb = get(packet->M, packet->MsgLength);
		ReleaseSRWLockExclusive(&m_lock);

		if (!msgb)
		{
			return 0;
		}

		LONG N = packet->N;

		DbgPrint("%hs: << %u.%u [%x]\r\n", m_parent->m_name, packet->M, N, cb);

		if (0x3F < N)
		{
			return STATUS_BAD_DATA;
		}

		if (!_interlockedbittestandset64(&msgb->m_bits, N))
		{
			if (0 <= (status = packet->Decrypt(m_parent->m_hAesProv, msgb->m_buf + N * max_packet_size, cb, &cb)))
			{
				msgb->m_tick = GetTickCount();

				if (InterlockedExchangeAddNoFence(&msgb->m_cbNeed, -(LONG)cb) == (LONG)cb && !((m_bits + 1) & m_bits))
				{
					DbgPrint("%u: ======== %x packet\r\n", GetTickCount() - m_t, packet->M);

					m_t = GetTickCount();

					status = OnUserData(packet->type, msgb->m_buf, packet->MsgLength);

					msgb->m_bits = 0;

					return status;
				}
			}
		}
	}

	return status;
}

NTSTATUS Endpoint::OnConnect(ULONG64 crc, ULONG64 cookie)
{
	m_rcookie = cookie;

	NTSTATUS status = LoadPubKey(crc);

	if (0 <= status)
	{
		SetDisconnectTime(e_inactive_time);

		return OnConnect();
	}

	return status;
}

NTSTATUS Endpoint::Accept(PSOCKADDR_INET psi, ULONG64 crc2, ULONG64 crc1, ULONG64 crc, ULONG64 cookie)
{
	m_rcookie = cookie;

	NTSTATUS status = LoadPubKey(crc);

	return 0 > status ? status : Init(psi, crc2, crc1, cookie);
}

NTSTATUS Endpoint::Init(PSOCKADDR_INET psi, ULONG64 crc2, ULONG64 crc1, ULONG64 cookie)
{
	if (!m_rp.Init())
	{
		return STATUS_INVALID_DEVICE_STATE;
	}

	if (MSGB* msgb = m_msgb)
	{
		__0:
		ULONG n = e_nb;
		do
		{
			msgb->m_M = MAXULONG;
			msgb++->m_bits = 0;
		} while (--n);
	}
	else
	{
		if (msgb = new MSGB[e_nb])
		{
			m_msgb = msgb;
			goto __0;
		}
		else
		{
			return STATUS_NO_MEMORY;
		}
	}

	NTSTATUS status = LoadPrivKey(crc2, crc1);

	if (0 > status)
	{
		return status;
	}

	m_sa = *psi;

	if (CDataPacket* packet = new(sizeof(C_PACKET)) CDataPacket)
	{
		C_PACKET* cpacket = (C_PACKET*)packet->getData();

		if (cookie)
		{
			status = Accept(psi);
		}

		cpacket->magic = C_PACKET::e_magic;
		cpacket->mcookie = status ? 0 : m_cookie;
		cpacket->rcookie = cookie;
		cpacket->crcKey = crc1;

		packet->setDataSize(sizeof(C_PACKET));

		NTSTATUS s = m_parent->SendTo(psi, packet);

		packet->Release();

		return status ? status : s;
	}

	return STATUS_NO_MEMORY;
}

NTSTATUS Endpoint::SendMsg(ULONG MsgLength, ULONG type, PBYTE pb, ULONG cb, USHORT M, USHORT N)
{
	ULONG BlockLength = m_PubBlockLength;

	if (CDataPacket* packet = new(E_PACKET::size(BlockLength, cb)) CDataPacket)
	{
		E_PACKET* epacket = (E_PACKET*)packet->getData();

		epacket->crc = m_rcookie;
		epacket->DataLength = 0;
		epacket->M = M, epacket->N = N;
		epacket->type = type, epacket->MsgLength = MsgLength;

		NTSTATUS status;

		if (0 <= (status = cb ? epacket->Encrypt(m_parent->m_hAesProv, pb, cb, &cb) : 0) &&
			0 <= (status = epacket->Encrypt(m_hPub, BlockLength)))
		{
			packet->setDataSize(E_PACKET::size(BlockLength, cb));
			status = m_parent->SendTo(&m_sa, packet);
		}

		packet->Release();

		return status;
	}

	return STATUS_NO_MEMORY;
}

NTSTATUS Endpoint::SendUserData(ULONG type, PBYTE pb, ULONG cb)
{
	if (max_packet_size * 0x40 < cb)
	{
		return STATUS_PORT_MESSAGE_TOO_LONG;
	}

	ULONG s, MsgLength = cb;

	USHORT M = InterlockedIncrementNoFence16(&m_M), N = 0;

	do 
	{
		if (NTSTATUS status = SendMsg(MsgLength, type, pb, s = __min(cb, max_packet_size), M, N++))
		{
			return status;
		}

	} while (pb += s, cb -= s);

	return STATUS_SUCCESS;
}

void Endpoint::notify_disconnect()
{
	if (m_rp.Acquire())
	{
		m_rp.Rundown_l();

		if (m_rp.Release())
		{
			OnDisconnect();
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// CClientServerR

void CClientServerR::AddListen()
{
	if (CDataPacket* packet = new(Endpoint::max_packet_size + sizeof(E_PACKET) + 0x80) CDataPacket)
	{
		if (RecvFrom(packet) == NOERROR)
		{
			InterlockedIncrementNoFence(&m_nPackets);
			DbgPrint("++packet<%p>\r\n", packet);
		}
		packet->Release();
	}
}

BOOL CClientServerR::Start(ULONG n, PSOCKADDR_INET psi)
{
	if (0 <= BCryptOpenAlgorithmProvider(&m_hAesProv, BCRYPT_AES_ALGORITHM, 0, 0))
	{
		if (!Create(psi))
		{
			do 
			{
				AddListen();
			} while (--n);
			return m_nPackets;
		}
	}
	return FALSE;
}

void CClientServerR::RemoveConnection(Endpoint* p)
{
	AcquireSRWLockExclusive(&m_lock);
	RemoveEntryList(p);
	InitializeListHead(p);
	ReleaseSRWLockExclusive(&m_lock);
	p->notify_disconnect();
	p->Release();
}

void CClientServerR::AddConnection(Endpoint* p)
{
	p->AddRef();
	AcquireSRWLockExclusive(&m_lock);
	InsertHeadList(this, p);
	ReleaseSRWLockExclusive(&m_lock);
}

Endpoint* CClientServerR::get(ULONG64 cookie)
{
	if (cookie)
	{
		AcquireSRWLockShared(&m_lock);
		Endpoint* p = 0;
		PLIST_ENTRY entry = this;
		while(this != (entry = entry->Flink))
		{
			if (cookie == static_cast<Endpoint*>(entry)->m_cookie)
			{
				(p = static_cast<Endpoint*>(entry))->AddRef();
				break;
			}
		}
		ReleaseSRWLockShared(&m_lock);

		return p;
	}

	return 0;
}

void CClientServerR::OnRecv(PSTR buf, ULONG cb, CDataPacket* packet, SOCKADDR_IN_EX* from)
{
	if (buf)
	{
		PrintIP(&from->addr, cb);

		OnData((PBYTE)buf, cb, &from->addr);

		cb = NOERROR;
	}

	switch (cb)
	{
	case NOERROR:
	case ERROR_PORT_UNREACHABLE:
		if (RecvFrom(packet) == NOERROR)
		{
			DbgPrint("reuse packet<%p>\r\n", packet);
			return;
		}
	}

	DbgPrint("error = %x\r\n--packet<%p>\r\n", cb, packet);

	if (!InterlockedDecrement(&m_nPackets))
	{
		Stop();
	}
}

void CClientServerR::OnConnect(C_PACKET* packet, PSOCKADDR_INET psi)
{
	if (C_PACKET::e_magic == packet->magic)
	{
		if (ULONG64 rcookie = packet->rcookie)
		{
			if (Endpoint* p = get(rcookie))
			{
				if (*psi == p->m_sa)
				{
					if (!packet->mcookie || p->OnConnect(packet->crcKey, packet->mcookie))
					{
						RemoveConnection(p);
					}
				}
				p->Release();
			}
		}
		else
		{
			if (Endpoint* p = CreateEndpoint())
			{
				AddConnection(p);

				if (p->Accept(psi, m_crc2, m_crc1, packet->crcKey, packet->mcookie))
				{
					RemoveConnection(p);
				}

				p->Release();
			}
		}
	}
}

void CClientServerR::OnDisconnect(ULONG64 cookie, PSOCKADDR_INET psi)
{
	if (Endpoint* p = get(cookie))
	{
		if (*psi == p->m_sa)
		{
			RemoveConnection(p);
		}
		p->Release();
	}
}

void CClientServerR::OnPing(P_PACKET* packet, PSOCKADDR_INET psi)
{
	if (P_PACKET::e_magic == packet->magic)
	{
		if (Endpoint* p = get(packet->crc))
		{
			if (*psi == p->m_sa)
			{
				p->SetDisconnectTime(Endpoint::e_inactive_time);
			}
			p->Release();
		}
	}
}

void CClientServerR::OnData(PBYTE pb, ULONG cb, PSOCKADDR_INET psi)
{
	switch (cb)
	{
	case sizeof(ULONG64):
		OnDisconnect(*(ULONG64*)pb, psi);
		break;

	case sizeof(P_PACKET):
		OnPing((P_PACKET*)pb, psi);
		break;

	case sizeof(C_PACKET):
		OnConnect((C_PACKET*)pb, psi);
		break;

	default:
		if (offsetof(E_PACKET, Buf) < cb)
		{
			E_PACKET* packet = (E_PACKET*)pb;
			if (Endpoint* p = get(packet->crc))
			{
				if (*psi == p->m_sa)
				{
					NTSTATUS status = STATUS_TOO_LATE;
					if (p->m_rp.Acquire())
					{
						status = p->OnData(packet, cb);

						if (p->m_rp.Release())
						{
							p->OnDisconnect();
						}
					}

					if (status)
					{
						RemoveConnection(p);
					}
				}
				p->Release();
			}
		}
	}
}

void CClientServerR::Stop()
{
	DbgPrint("%hs<%p>\r\n", __FUNCTION__, this);

	AcquireSRWLockExclusive(&m_lock);
	LIST_ENTRY Head = { Flink, Blink };
	Flink->Blink = &Head;
	Blink->Flink = &Head;
	InitializeListHead(this);
	ReleaseSRWLockExclusive(&m_lock);

	PLIST_ENTRY entry = Head.Flink;

	while (&Head != entry)
	{
		Endpoint* p = static_cast<Endpoint*>(entry);
		entry = entry->Flink;
		p->notify_disconnect();
		p->Release();
	}
}

void CClientServerR::deleteTimer()
{
	if (PTP_TIMER Timer = (PTP_TIMER)InterlockedExchangePointer((void**)&_M_Timer, 0))
	{
		TpWaitForTimer(Timer, TRUE);
		TpReleaseTimer(Timer);
	}
}

void CClientServerR::OnTimer()
{
	ULONG64 time = GetTickCount64();
	Endpoint* first = 0;
	AcquireSRWLockExclusive(&m_lock);
	PLIST_ENTRY entry = Flink;
	while (this != entry)
	{
		Endpoint* p = static_cast<Endpoint*>(entry);
		entry = entry->Flink;
		if (p->m_tick < time)
		{
			RemoveEntryList(p);
			InitializeListHead(p);
			p->m_next = first;
			first = p;
		}
	}
	ReleaseSRWLockExclusive(&m_lock);

	while (Endpoint* p = first)
	{
		first = p->m_next;
		p->Disconnect();
		p->Release();
	}
}

VOID NTAPI CClientServerR::_onTimer(
	_Inout_     PTP_CALLBACK_INSTANCE /*Instance*/,
	_Inout_opt_ PVOID                 Context,
	_Inout_     PTP_TIMER             /*Timer*/
)
{
	reinterpret_cast<CClientServerR*>(Context)->OnTimer();
}

NTSTATUS CClientServerR::SetTimer(ULONG Period)
{
	NTSTATUS status = TpAllocTimer(&_M_Timer, _onTimer, this, 0);

	if (0 > status)
	{
		return status;
	}
	LARGE_INTEGER li = {};
	TpSetTimer(_M_Timer, &li, Period, 0);
	return STATUS_SUCCESS;
}

_NT_END