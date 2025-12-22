#pragma once

class WLog
{
	PVOID _BaseAddress;
	ULONG _RegionSize, _Ptr;

	PWSTR _buf()
	{
		return (PWSTR)((PBYTE)_BaseAddress + _Ptr);
	}

	ULONG _cch()
	{
		return (_RegionSize - _Ptr) / sizeof(WCHAR);
	}
	SRWLOCK _lock = {};

public:
	void operator >> (HWND hwnd);

	ULONG Init(SIZE_T RegionSize);

	~WLog()
	{
		if (_BaseAddress)
		{
			LocalFree(_BaseAddress);
		}
	}

	WLog(WLog&&) = delete;
	WLog(WLog&) = delete;
	WLog(): _BaseAddress(0) {  }

	operator PCWSTR()
	{
		return (PCWSTR)_BaseAddress;
	}

	WLog& operator ()(PCWSTR format, ...);
};

extern WLog _G_log;

#define DbgPrint(fmt, ...) _G_log(_CRT_WIDE(fmt), __VA_ARGS__ )
