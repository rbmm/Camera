#define SECURITY_WIN32
#include "stdafx.h"

_NT_BEGIN

#include "log.h"

HRESULT GetLastHresult(ULONG dwError /*= GetLastError()*/)
{
	NTSTATUS status = RtlGetLastNtStatus();

	return RtlNtStatusToDosErrorNoTeb(status) == dwError ? HRESULT_FROM_NT(status) : HRESULT_FROM_WIN32(dwError);
}

BOOLEAN IsFileExist(_In_ PCWSTR FileName)
{
	PWSTR psz = 0;
	ULONG cch = 0;
	while (cch = ExpandEnvironmentStringsW(FileName, psz, cch))
	{
		if (psz)
		{
			return RtlDoesFileExists_U(psz);
		}

		psz = (PWSTR)alloca(cch * sizeof(WCHAR));
	}

	return FALSE;
}

static HANDLE _S_hRoot, _S_hLog;
static SRWLOCK _S_SRWLock;
static ULONG _S_day = 0;

void DestroyLog()
{
	AcquireSRWLockExclusive(&_S_SRWLock);
	if (_S_hLog) NtClose(_S_hLog), _S_hLog = 0;
	if (_S_hRoot) NtClose(_S_hRoot), _S_hRoot = 0;
	ReleaseSRWLockExclusive(&_S_SRWLock);
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

NTSTATUS FormatTempFilePath(_Out_ PUNICODE_STRING ObjectName, _In_ PCWSTR name)
{
	STATIC_UNICODE_STRING(TMP, "TMP=");

	if (PCWSTR path = GetVarString(&TMP))
	{
		PWSTR psz = 0;
		int len = 0;

		while (0 < (len = _snwprintf(psz, len, L"%ws/%ws", path, name)))
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

NTSTATUS InitLog(PCWSTR name, BOOL bUseFolder)
{
	UNICODE_STRING ObjectName;
	OBJECT_ATTRIBUTES oa = { sizeof(oa), 0, &ObjectName, OBJ_CASE_INSENSITIVE };

	NTSTATUS status = FormatTempFilePath(&ObjectName, name);

	if (0 <= status)
	{
		IO_STATUS_BLOCK iosb;

		status = bUseFolder ? NtCreateFile(&_S_hRoot, FILE_ADD_FILE, &oa, &iosb, 0, FILE_ATTRIBUTE_DIRECTORY,
			FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN_IF, FILE_DIRECTORY_FILE, 0, 0) :
			NtCreateFile(&_S_hLog, SYNCHRONIZE | FILE_APPEND_DATA, &oa, &iosb, 0, 0,
				FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OVERWRITE_IF, FILE_SYNCHRONOUS_IO_NONALERT, 0, 0);

		RtlFreeUnicodeString(&ObjectName);
	}

	return status;
}

static NTSTATUS Init(ULONG wDay)
{
	if (!_S_hRoot)
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
	OBJECT_ATTRIBUTES oa = { sizeof(oa), _S_hRoot, &ObjectName, OBJ_CASE_INSENSITIVE };
	RtlInitUnicodeString(&ObjectName, lpFileName);

	NTSTATUS status = NtCreateFile(&hFile, SYNCHRONIZE | FILE_WRITE_DATA | FILE_READ_ATTRIBUTES, &oa,
		&iosb, 0, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN_IF, FILE_SYNCHRONOUS_IO_NONALERT, 0, 0);

	if (0 <= status)
	{
		_S_hLog = hFile, _S_day = wDay;

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
				// if last write was 16+ days ago, overwrite
				if (time.QuadPart - fbi.LastWriteTime.QuadPart > 10000000LL * 3600 * 24 * 16)
				{
					static FILE_END_OF_FILE_INFORMATION eof{};
					NtSetInformationFile(hFile, &iosb, &eof, sizeof(eof), FileEndOfFileInformation);
				}
				else // append to the end of file
				{
					NtSetInformationFile(hFile, &iosb, &fbi.EndOfFile, sizeof(fbi.EndOfFile), FilePositionInformation);
				}
			}
		}
	}

	return status;
}

void logwrite(const void* buf, ULONG cb, ULONG wDay)
{
	HANDLE hFile;

	if (_S_hRoot && _S_day != wDay)
	{
		AcquireSRWLockExclusive(&_S_SRWLock);

		if (_S_day != wDay)
		{
			if (hFile = _S_hLog)
			{
				NtClose(hFile);
				_S_hLog = 0;
			}

			Init(wDay);
		}

		ReleaseSRWLockExclusive(&_S_SRWLock);
	}

	AcquireSRWLockShared(&_S_SRWLock);

	if (_S_hLog) WriteFile(_S_hLog, buf, cb, &cb, 0);

	ReleaseSRWLockShared(&_S_SRWLock);
}

LONG _G_logMask = ~0;

void LogOutputNT(_In_ LONG mask, _In_ PCWSTR format, ...)
{
	if (!(_G_logMask & mask))
	{
		return;
	}

	SYSTEMTIME tf;
	GetLocalTime(&tf);

	va_list ap;
	va_start(ap, format);

	union {
		PWSTR buf = 0;
		PSTR psz;
	};
	int len = 0, l = 0;
	PWSTR pwz = 0;

	while (0 < (len = _vsnwprintf(pwz, len, format, ap)))
	{
		if (buf)
		{
			ULONG cb = (l + len) * sizeof(WCHAR);
			if (0 <= RtlUnicodeToUTF8N(psz, cb, &cb, buf, cb))
			{
				PSTR pc = psz + cb;
				switch (cb)
				{
				case 0:
					return;
				case 1:
					if ('\n' == *--pc)
					{
						--cb;
					}
					goto __m;
				default:
					if ('\n' != *--pc)
					{
					__m:
						*++pc = '\r', *++pc = '\n', cb += 2;
						break;
					}
					if ('\r' != *--pc)
					{
						--cb;
						goto __m;
					}
					break;
				}

				if (IsDebuggerPresent())
				{
					ULONG_PTR params[] = { cb, (ULONG_PTR)psz };
					RaiseException(DBG_PRINTEXCEPTION_C, 0, _countof(params), params);
				}

				logwrite(psz, cb, tf.wDay);
			}

			break;
		}

		if ('\f' == *format)
		{
			pwz = buf = (PWSTR)alloca(len * sizeof(WCHAR));
			format++;
			continue;
		}

		enum { tl = _countof("[yyyy-mm-dd hh:mm:ss] ") };

		buf = (PWSTR)alloca((2 + len++ + tl) * sizeof(WCHAR));

		if (0 >= (l = swprintf_s(buf, tl, L"[%04u-%02u-%02u %02u:%02u:%02u] ",
			tf.wYear, tf.wMonth, tf.wDay, tf.wHour, tf.wMinute, tf.wSecond)))
		{
			break;
		}

		pwz = buf + l;
	}
}

HMODULE GetNTDLL()
{
	static HMODULE _S_hmod;
	if (!_S_hmod)
	{
		_S_hmod = GetModuleHandle(L"ntdll.dll");
	}
	return _S_hmod;
}

HRESULT I_LogError(WCHAR c, const void* prefix, HRESULT dwError)
{
	HRESULT hr = dwError;
	WCHAR msg[0x100], format[] = L"%*s: [0x%x at %08x] %ws";
	format[1] = c;

	LONG f = 0;
	LPCVOID lpSource = 0;
	ULONG dwFlags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;

	if ((dwError & FACILITY_NT_BIT) || (0 > dwError && HRESULT_FACILITY(dwError) == FACILITY_NULL))
	{
		dwError &= ~FACILITY_NT_BIT;
	__nt:
		dwFlags = FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS;

		lpSource = GetNTDLL();
	}
__0:
	if (FormatMessageW(dwFlags, lpSource, dwError, 0, msg, _countof(msg), 0))
	{
	__end:
		LogOutputNT(~0, format, prefix, dwError, RtlPointerToOffset(&__ImageBase, _ReturnAddress()), msg);
		return hr;
	}

	if (!_bittestandset(&f, 1))
	{
		if (dwFlags & FORMAT_MESSAGE_FROM_SYSTEM)
		{
			goto __nt;
		}

		if (FACILITY_NULL == HRESULT_FACILITY(dwError))
		{
			lpSource = 0;
			dwFlags = FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS;
			goto __0;
		}
	}

	wcscpy(msg, L"\r\n");

	goto __end;
}

_NT_END