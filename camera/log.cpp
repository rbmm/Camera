#include "stdafx.h"

_NT_BEGIN

#include "log.h"

WLog _G_log;

void WLog::operator >> (HWND hwnd)
{
	PVOID pv = (PVOID)SendMessage(hwnd, EM_GETHANDLE, 0, 0);
	SendMessage(hwnd, EM_SETHANDLE, (WPARAM)_BaseAddress, 0);
	_BaseAddress = 0, _Ptr = 0;
	if (pv)
	{
		LocalFree(pv);
	}
}

ULONG WLog::Init(SIZE_T RegionSize)
{
	if (_BaseAddress = LocalAlloc(0, RegionSize))
	{
		*(WCHAR*)_BaseAddress = 0;
		_MaxPtr = (ULONG)RegionSize / sizeof(WCHAR), _Ptr = 0;
		return NOERROR;
	}
	return GetLastError();
}

WLog& WLog::operator ()(PCWSTR format, ...)
{
	va_list args;
	va_start(args, format);

	AcquireSRWLockExclusive(&_lock);

	int len = vswprintf_s(_buf(), _cch(), format, args);

	if (0 < len)
	{
		_Ptr += len;
	}

	ReleaseSRWLockExclusive(&_lock);

	va_end(args);

	return *this;
}

_NT_END