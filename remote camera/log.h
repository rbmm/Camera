#pragma once

HRESULT GetLastHresult(ULONG dwError = GetLastError());

inline HRESULT GetLastHresult(BOOL fOk)
{
	return fOk ? S_OK : GetLastHresult();
}

template <typename T> T HR(HRESULT& hr, T t)
{
	hr = t ? NOERROR : GetLastHresult(GetLastError());
	return t;
}

HMODULE GetNTDLL();

BOOLEAN IsFileExist(_In_ PCWSTR FileName);

LONG InitLog(PCWSTR name, BOOL bUseFolder = FALSE);

void DestroyLog();

extern LONG _G_logMask;

void LogOutputNT(_In_ LONG mask, _In_ PCWSTR format, ...);

HRESULT I_LogError(WCHAR c, const void* prefix, HRESULT dwError = GetLastHresult());

inline HRESULT LogError(PCSTR prefix, HRESULT dwError = GetLastHresult())
{
	return I_LogError('h', prefix, dwError);
}

inline HRESULT LogError(PCWSTR prefix, HRESULT dwError = GetLastHresult())
{
	return I_LogError('w', prefix, dwError);
}

#ifdef _NTIFS_

#define DbgPrint(format, ...) LogOutputNT(~0, _CRT_WIDE(format), __VA_ARGS__ )

#else

#define DbgPrint(format, ...) NT::LogOutputNT(~0, _CRT_WIDE(format), __VA_ARGS__ )
#define LogOutput(format, ...) NT::LogOutputNT(~0, format, __VA_ARGS__ )

#define _NT_BEGIN namespace NT {
#define _NT_END }

#endif // _NTIFS_

EXTERN_C extern IMAGE_DOS_HEADER __ImageBase;

#pragma message("!!! NT::LOG >>>>>>")
