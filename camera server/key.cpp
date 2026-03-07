#include "stdafx.h"

_NT_BEGIN

#include "file.h"

NTSTATUS SaveKey(BCRYPT_KEY_HANDLE hKey, PCWSTR pszBlobType, PULONGLONG pcrc, ULONGLONG _crc)
{
	NTSTATUS status;
	PBYTE pb = 0;
	ULONG cb = 0;
	while(0 <= (status = BCryptExportKey(hKey, 0, pszBlobType, pb, cb, &cb, 0)))
	{
		if (pb)
		{
			ULONGLONG crc = RtlCrc64(pb, cb, 0);
			if (pcrc) *pcrc = crc;
			WCHAR name[33];
			if (0 < _crc ? swprintf_s(name, _countof(name), L"%016I64x%016I64x", crc, _crc) : 
				swprintf_s(name, _countof(name), L"%016I64x", crc))
			{
				return SaveToFile(name, pb, cb);
			}

			return STATUS_INTERNAL_ERROR;
		}

		pb = (PBYTE)alloca(cb);
	}

	return status;
}

NTSTATUS GenKeyXY()
{
	BCRYPT_ALG_HANDLE hAlgorithm;
	NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlgorithm, BCRYPT_RSA_ALGORITHM, 0, 0);
	if (0 <= status)
	{
		BCRYPT_KEY_HANDLE hKey;
		status = BCryptGenerateKeyPair(hAlgorithm, &hKey, 2048, 0);
		BCryptCloseAlgorithmProvider(hAlgorithm, 0);
		if (0 <= status)
		{
			ULONGLONG crc;
			0 <= (status = BCryptFinalizeKeyPair(hKey, 0)) &&
				0 <= (status = SaveKey(hKey, BCRYPT_RSAPUBLIC_BLOB, &crc, 0)) &&
				0 <= (status = SaveKey(hKey, BCRYPT_RSAPRIVATE_BLOB, 0, crc));

			BCryptDestroyKey(hKey);
		}
	}

	return status;
}

_NT_END