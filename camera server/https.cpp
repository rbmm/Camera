#include "stdafx.h"

_NT_BEGIN

#include "log.h"
#include "https.h"
#include "file.h"

CEncTcp::~CEncTcp()
{
	//DbgPrint("%hs<%p>\r\n", __FUNCTION__, this);
	SetKey(0);
	deleteTimer();
}

void CEncTcp::OnDisconnect()
{
	//DbgPrint("%hs<%p>\r\n", __FUNCTION__, this);
	SetKey(0);
	deleteTimer();
}

void CEncTcp::deleteTimer()
{
	if (PTP_TIMER Timer = (PTP_TIMER)InterlockedExchangePointer((void**)&_M_Timer, 0))
	{
		TpWaitForTimer(Timer, TRUE);
		TpReleaseTimer(Timer);
	}
}

VOID NTAPI CEncTcp::_onTimer(
							 _Inout_     PTP_CALLBACK_INSTANCE Instance,
							 _Inout_opt_ PVOID                 Context,
							 _Inout_     PTP_TIMER             Timer
							 )
{
	reinterpret_cast<CEncTcp*>(Context)->onTimer(Instance, Timer);
}

VOID NTAPI CEncTcp::_scb (
						  _Inout_     PTP_CALLBACK_INSTANCE /*Instance*/,
						  _Inout_opt_ PVOID                 Context
						  )
{
	reinterpret_cast<CEncTcp*>(Context)->Disconnect(ERROR_TIMEOUT);
	reinterpret_cast<CEncTcp*>(Context)->Release();
}

VOID CEncTcp::onTimer(
					  _Inout_     PTP_CALLBACK_INSTANCE /*Instance*/,
					  _Inout_     PTP_TIMER             /*Timer*/
					  )
{
	if (GetTickCount64() > _M_DisconnectTime)
	{
		_M_DisconnectTime = MAXULONG;
		DbgPrint("%hs<%p>: !!! timeout !!!\r\n", __FUNCTION__, this);
		AddRef();
		if (0 > TpSimpleTryPost(_scb, this, 0))
		{
			Release();
		}
	}
}

NTSTATUS CEncTcp::SetTimer(ULONG Period)
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

BOOL CEncTcp::OnRecv(PSTR Buffer, ULONG cbTransferred)
{
	union {
		PSTR buf;
		PACKET_HEADER* ph;
	};

	CDataPacket* packet = m_packet;
	buf = packet->getData();

	ULONG cbExtra = 0;

	if (buf == Buffer)
	{
__loop:
		if (cbTransferred < sizeof(PACKET_HEADER) || ph->size > packet->getFreeSize())
		{
			DbgPrint("%hs(%u): !!\r\n", __FILE__, __LINE__);
			return FALSE;
		}
	}

	if (ph->size < cbTransferred)
	{
		cbExtra = cbTransferred - ph->size;
		cbTransferred = ph->size;
	}

	ULONG size = packet->addData(cbTransferred) - sizeof(PACKET_HEADER);

	if (ph->size -= cbTransferred)
	{
		return TRUE;
	}

	packet->setDataSize(0);

	if (size)
	{
		if (0 > BCryptDecrypt(_hKey, ph->buf, size, 0, 0, 0, ph->buf, size, &size, BCRYPT_BLOCK_PADDING))
		{
			return FALSE;
		}

		ph->type_xor_crc ^= RtlCrc32(ph->buf, size, 0);
	}

	if (!OnUserData(ph->type_xor_crc, ph->buf, size))
	{
		return FALSE;
	}

	if (cbExtra)
	{
		Buffer = (PSTR)memcpy(buf, Buffer + cbTransferred, cbExtra);
		cbTransferred = cbExtra, cbExtra = 0;
		goto __loop;
	}

	return TRUE;
}

ULONG CEncTcp::SendUserData(ULONG type, const void* pbIn, ULONG cbIn)
{
	CDataPacket* packet = 0;
	PACKET_HEADER* ph = 0;
	PBYTE pb = 0;
	ULONG cb = 0;
	NTSTATUS status;
	while (0 <= (status = BCryptEncrypt(_hKey, (PBYTE)pbIn, cbIn, 0, 0, 0, pb, cb, &cb, BCRYPT_BLOCK_PADDING)))
	{
		if (pb)
		{
			ph->size = cb += sizeof(PACKET_HEADER);
			ph->type_xor_crc = type ^ RtlCrc32(pbIn, cbIn, 0);
			packet->setDataSize(cb);
			status = Send(packet);
			break;
		}

		if (packet = new(sizeof(PACKET_HEADER) + cb) CDataPacket)
		{
			ph = (PACKET_HEADER*)packet->getData();
			pb = ph->buf;
		}
		else
		{
			return ERROR_OUTOFMEMORY;
		}
	}

	if (packet)
	{
		packet->Release();
	}
	return status;
}

ULONG CEncTcp::SendUserData(ULONG type)
{
	if (CDataPacket* packet = new(sizeof(PACKET_HEADER)) CDataPacket)
	{
		PACKET_HEADER* ph = (PACKET_HEADER*)packet->getData();
		ph->size = sizeof(PACKET_HEADER);
		ph->type_xor_crc = type;
		packet->setDataSize(sizeof(PACKET_HEADER));
		return Send(packet);
	}
	return ERROR_OUTOFMEMORY;
}

BOOL CEncTcp::InitSession(PBYTE pb, ULONG cb)
{
	UCHAR secret[0x20];
	BCRYPT_KEY_HANDLE hKey = (BCRYPT_KEY_HANDLE)InterlockedExchangePointer(&_hKey, 0);
	NTSTATUS status = BCryptDecrypt(hKey, pb, cb, 0, 0, 0, secret, sizeof(secret), &cb, BCRYPT_PAD_PKCS1);
	BCryptDestroyKey(hKey);

	if (0 <= status && sizeof(secret) == cb)
	{
		BCRYPT_ALG_HANDLE hAlgorithm;
		if (0 <= (status = BCryptOpenAlgorithmProvider(&hAlgorithm, BCRYPT_AES_ALGORITHM, 0, 0)))
		{
			status = BCryptGenerateSymmetricKey(hAlgorithm, &hKey, 0, 0, secret, sizeof(secret), 0);
			BCryptCloseAlgorithmProvider(hAlgorithm, 0);
			if (0 <= status)
			{
				SetKey(hKey);
				return TRUE;
			}
		}
	}

	return FALSE;
}

NTSTATUS CEncTcp::InitSession(BCRYPT_KEY_HANDLE hRsaKey)
{
	NTSTATUS status;
	BCRYPT_KEY_HANDLE hAesKey = 0;
	BCRYPT_ALG_HANDLE hAlgorithm;
	if (0 <= (status = BCryptOpenAlgorithmProvider(&hAlgorithm, BCRYPT_AES_ALGORITHM, 0, 0)))
	{
		UCHAR secret[0x20];

		0 <= (status = BCryptGenRandom(0, secret, sizeof(secret), BCRYPT_USE_SYSTEM_PREFERRED_RNG)) &&
			0 <= (status = BCryptGenerateSymmetricKey(hAlgorithm, &hAesKey, 0, 0, secret, sizeof(secret), 0));

		BCryptCloseAlgorithmProvider(hAlgorithm, 0);

		if (0 <= status)
		{
			ULONG cb = 0;
			PBYTE pb = 0;

			CDataPacket* packet = 0;
			while (0 <= (status = BCryptEncrypt(hRsaKey, secret, sizeof(secret), 0, 0, 0, pb, cb, &cb, BCRYPT_PAD_PKCS1)))
			{
				if (pb)
				{
					SetKey(hAesKey), hAesKey = 0;

					packet->setDataSize(cb);
					status = Send(packet);
					break;
				}

				if (packet = new(cb) CDataPacket)
				{
					pb = (PBYTE)packet->getData();
				}
				else
				{
					status = STATUS_NO_MEMORY;
					break;
				}
			}

			if (packet)
			{
				packet->Release();
			}

			if (hAesKey) BCryptDestroyKey(hAesKey);
		}
	}

	return status;
}

NTSTATUS CEncTcp::InitSession(ULONGLONG crc)
{
	DbgPrint("%hs<%p>(%016I64x)\r\n", __FUNCTION__, this, crc);

	WCHAR name[17];
	NTSTATUS status = STATUS_INTERNAL_ERROR;

	if (0 < swprintf_s(name, _countof(name), L"%016I64x", crc))
	{
		PBYTE pb;
		ULONG cb;
		if (0 <= (status = ReadFromFile(name, &pb, &cb, 0x80, 0x800)))
		{
			BCRYPT_KEY_HANDLE hRsaKey = 0;
			BCRYPT_ALG_HANDLE hAlgorithm;
			if (0 <= (status = BCryptOpenAlgorithmProvider(&hAlgorithm, BCRYPT_RSA_ALGORITHM, 0, 0)))
			{
				status = BCryptImportKeyPair(hAlgorithm, 0, BCRYPT_RSAPUBLIC_BLOB, &hRsaKey, pb, cb, 0);
				BCryptCloseAlgorithmProvider(hAlgorithm, 0);
			}

			delete [] pb;

			if (0 <= status)
			{
				status = InitSession(hRsaKey);
				BCryptDestroyKey(hRsaKey);
			}
		}
	}

	return status;
}

_NT_END