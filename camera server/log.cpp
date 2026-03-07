#include "stdafx.h"

_NT_BEGIN

#include "log.h"

HRESULT GetLastHresult(ULONG dwError /*= GetLastError()*/)
{
	NTSTATUS status = RtlGetLastNtStatus();

	return RtlNtStatusToDosErrorNoTeb(status) == dwError ? HRESULT_FROM_NT(status) : HRESULT_FROM_WIN32(dwError);
}

CLogFile CLogFile::_S_LF;

CLogFile::~CLogFile()
{
	if (_hFile)
	{
		NtClose(_hFile);
	}
	if (_hRoot)
	{
		NtClose(_hRoot);
	}
}

PCWSTR GetVarString(PCUNICODE_STRING pus)
{
	_PEB* peb = RtlGetCurrentPeb();
	PCWSTR lpsz = (PCWSTR)peb->ProcessParameters->Environment;

	while (*lpsz)
	{
		UNICODE_STRING str;
		RtlInitUnicodeString(&str, lpsz);
		if (RtlPrefixUnicodeString(pus, &str, TRUE))
		{
			return (PCWSTR)RtlOffsetToPointer(lpsz, pus->Length);
		}

		lpsz += wcslen(lpsz) + 1;
	}

	return 0;
}

NTSTATUS FormatFilePath(_Out_ PUNICODE_STRING ObjectName)
{
	STATIC_UNICODE_STRING(TMP, "TMP=");

	if (PCWSTR path = GetVarString(&TMP))
	{
		PWSTR psz = 0;
		int len = 0;

		while (0 < (len = _snwprintf(psz, len, L"%ws/rbmm camera", path)))
		{
			if (psz)
			{
				return RtlDosPathNameToNtPathName_U_WithStatus(psz, ObjectName, 0, 0);
			}

			psz = (PWSTR)alloca(++len * sizeof(WCHAR));
		}

		return STATUS_INTERNAL_ERROR;
	}

	return STATUS_VARIABLE_NOT_FOUND;
}

NTSTATUS CLogFile::Init()
{
	UNICODE_STRING ObjectName;
	OBJECT_ATTRIBUTES oa = { sizeof(oa), 0, &ObjectName, OBJ_CASE_INSENSITIVE };

	NTSTATUS status = FormatFilePath(&ObjectName);

	if (0 <= status)
	{
		HANDLE hFile;
		IO_STATUS_BLOCK iosb;

		status = NtCreateFile(&hFile, FILE_ADD_FILE, &oa, &iosb, 0, FILE_ATTRIBUTE_DIRECTORY,
			FILE_SHARE_READ|FILE_SHARE_WRITE, FILE_OPEN_IF, FILE_DIRECTORY_FILE, 0, 0);

		RtlFreeUnicodeString(&ObjectName);
		
		if (0 <= status)
		{
			_hRoot = hFile;
		}
	}

	return status;
}

NTSTATUS CLogFile::Init(ULONG wDay)
{
	if (!_hRoot)
	{
		return STATUS_INVALID_HANDLE;
	}

	WCHAR lpFileName[8];

	if (0 >= swprintf_s(lpFileName, _countof(lpFileName), L"%02u.log", wDay))
	{
		return STATUS_INTERNAL_ERROR;
	}

	HANDLE hFile;
	IO_STATUS_BLOCK iosb;
	UNICODE_STRING ObjectName;
	OBJECT_ATTRIBUTES oa = { sizeof(oa), _hRoot, &ObjectName, OBJ_CASE_INSENSITIVE };
	RtlInitUnicodeString(&ObjectName, lpFileName);

	NTSTATUS status = NtCreateFile(&hFile, SYNCHRONIZE|FILE_APPEND_DATA|FILE_WRITE_DATA|FILE_READ_ATTRIBUTES, &oa, 
		&iosb, 0, 0, FILE_SHARE_READ|FILE_SHARE_WRITE, FILE_OPEN_IF, FILE_SYNCHRONOUS_IO_NONALERT, 0, 0);

	if (0 <= status)
	{
		_hFile = hFile, _day = wDay;

		if (FILE_OPENED == iosb.Information)
		{
			FILE_NETWORK_OPEN_INFORMATION fbi;
			if (0 <= NtQueryInformationFile(hFile, &iosb, &fbi, sizeof(fbi), FileNetworkOpenInformation))
			{
				union {
					LARGE_INTEGER time;
					FILETIME ft;
				};
				
				GetSystemTimeAsFileTime(&ft);
				if (time.QuadPart - fbi.LastWriteTime.QuadPart > 10000000LL*3600*24*16)
				{
					static FILE_END_OF_FILE_INFORMATION eof{};
					NtSetInformationFile(hFile, &iosb, &eof, sizeof(eof), FileEndOfFileInformation);
				}
				else
				{
					NtSetInformationFile(hFile, &iosb, &fbi.EndOfFile, sizeof(fbi.EndOfFile), FilePositionInformation);
				}
			}
		}
	}

	return status;
}

void CLogFile::write(const void* buf, ULONG cb, ULONG wDay)
{
	HANDLE hFile;

	if (_day != wDay)
	{
		AcquireSRWLockExclusive(&_SRWLock);	

		if (_day != wDay)
		{
			if (hFile = _hFile)
			{
				NtClose(hFile);
				_hFile = 0;
			}

			Init(wDay);
		}

		ReleaseSRWLockExclusive(&_SRWLock);
	}

	AcquireSRWLockShared(&_SRWLock);

	if (hFile = _hFile) WriteFile(hFile, buf, cb, &cb, 0);

	ReleaseSRWLockShared(&_SRWLock);
}

void CLogFile::printf(_In_ PCWSTR format, ...)
{
	SYSTEMTIME tf;
	GetLocalTime(&tf);

	va_list ap;
	va_start(ap, format);

	union {
		PWSTR buf = 0;
		PSTR psz;
	};
	int len = 0;

	enum { tl = _countof("[hh:mm:ss] ") - 1};

	while (0 < (len = _vsnwprintf(buf, len, format, ap)))
	{
		if (buf && tl - 1 == swprintf_s(buf -= tl, tl, L"[%02u:%02u:%02u]", tf.wHour, tf.wMinute, tf.wSecond))
		{
			buf[tl - 1] = ' ';

			ULONG cb = (tl + len) * sizeof(WCHAR);
			if (0 <= RtlUnicodeToUTF8N(psz, cb, &cb, buf, cb))
			{
				if (IsDebuggerPresent())
				{
					ULONG_PTR params[] = { cb, (ULONG_PTR)psz };
					RaiseException(DBG_PRINTEXCEPTION_C, 0, _countof(params), params);
				}

				write(psz, cb, tf.wDay);
			}

			break;
		}

		buf = (PWSTR)alloca((++len + tl) * sizeof(WCHAR)) + tl;
	}
}

HRESULT CLogFile::PrintError(PCSTR prefix, HRESULT dwError)
{
	HRESULT hr = dwError;

	LPCVOID lpSource = 0;
	ULONG dwFlags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;

	if ((dwError & FACILITY_NT_BIT) || (0 > dwError && HRESULT_FACILITY(dwError) == FACILITY_NULL))
	{
		dwError &= ~FACILITY_NT_BIT;
__nt:
		dwFlags = FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS;

		static HMODULE _S_hmod;
		if (!_S_hmod)
		{
			_S_hmod = GetModuleHandle(L"ntdll.dll");
		}
		lpSource = _S_hmod;
	}

	static const WCHAR fmt[] = L"%hs: [0x%x at %08x] %ws";
	WCHAR msg[0x100];
	if (FormatMessageW(dwFlags, lpSource, dwError, 0, msg, _countof(msg), 0))
	{
__end:
		printf(fmt, prefix, dwError, RtlPointerToOffset(&__ImageBase, _ReturnAddress()), msg);
		return hr;
	}
	
	if (dwFlags & FORMAT_MESSAGE_FROM_SYSTEM)
	{
		goto __nt;
	}

	wcscpy(msg, L"\r\n");

	goto __end;
}

_NT_END