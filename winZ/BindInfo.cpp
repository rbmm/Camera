#include "StdAfx.h"

_NT_BEGIN

#include "dochost.h"

#pragma warning(disable : 4100)

const VS_FIXEDFILEINFO* GetFileVersion(PCWSTR name)
{
	PVOID ImageBase;
	static LPCWSTR a[3] = { RT_VERSION, MAKEINTRESOURCE(1) };
	PIMAGE_RESOURCE_DATA_ENTRY pirde;
	DWORD size, wLength;

	struct VS_VERSIONINFO_HEADER {
		WORD             wLength;
		WORD             wValueLength;
		WORD             wType;
		WCHAR            szKey[];
	} *pv;

	if (
		(ImageBase = GetModuleHandle(name)) &&
		0 <= LdrFindResource_U(ImageBase, a, 3, &pirde) && 
		0 <= LdrAccessResource(ImageBase, pirde, (void**)&pv, &size) && 
		size > sizeof(*pv) &&
		(wLength = pv->wLength) <= size &&
		pv->wValueLength >= sizeof(VS_FIXEDFILEINFO)
		)
	{
		PVOID end = RtlOffsetToPointer(pv, wLength);
		PWSTR sz = pv->szKey;
		do 
		{
			if (!*sz++)
			{
				VS_FIXEDFILEINFO* pffi = (VS_FIXEDFILEINFO*)((3 + (ULONG_PTR)sz) & ~3);
				//RtlOffsetToPointer(pv, (RtlPointerToOffset(pv, sz) + 3) & ~3);
				return RtlPointerToOffset(pffi, end) < wLength && pffi->dwSignature == VS_FFI_SIGNATURE ? pffi : 0;
			}
		} while (sz < end);
	}

	return 0;
}

DWORD GetMsHtmlVersion()
{
	if (const VS_FIXEDFILEINFO* vsfi = GetFileVersion(L"mshtml.dll"))
	{
		return HIWORD(vsfi->dwFileVersionMS); 
	}
	return 0;
}

struct __declspec(novtable) IUnknown1 : IUnknown 
{
};

struct __declspec(novtable) IUnknown2 : IUnknown 
{
};

struct __declspec(novtable) __declspec(uuid("AF0FF408-129D-4b20-91F0-02BD23D88352")) IGetBindHandle : IUnknown 
{
	virtual HRESULT STDMETHODCALLTYPE GetBindHandle(int , HANDLE *pRetHandle)
	{
		if (pRetHandle)
		{
			*pRetHandle = 0;
		}
		return S_OK;
	}
};

class __declspec(novtable) CDwnBindInfo1 : public IUnknown
{
protected:
	LONG _dwRef, _state;
};

class __declspec(novtable) CDwnBindInfo1_v10 : public IUnknown
{
protected:
	LONG _dwRef, _state;
	PVOID pv_ie10;//in ie10 only!!!
};

class __declspec(novtable) CDwnBindInfo2 : public CDwnBindInfo1, public IUnknown1, public IGetBindHandle, public IUnknown2 
{
};

class __declspec(novtable) CDwnBindInfo2_v10 : public CDwnBindInfo1_v10, public IUnknown1, public IGetBindHandle, public IUnknown2 
{
};

struct __declspec(novtable) __declspec(uuid("3050f3c3-98b5-11cf-bb82-00aa00bdce0b")) 
IDwnBindInfo : public IUnknown
{
protected:
	PVOID _pad[2], _DwnDoc, _zero;

	virtual HRESULT STDMETHODCALLTYPE BeginningTransaction( 
		LPCWSTR szURL,
		LPCWSTR szHeaders,
		DWORD dwReserved,
		LPWSTR *pszAdditionalHeaders) = 0;
};

#include "../inc/rtlframe.h"

class _CDwnBindInfo : public CDwnBindInfo2, public IDwnBindInfo
{
protected:

	BSTR _url, _ref;

	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, __RPC__deref_out void **ppvObject)
	{
		if (riid == __uuidof(IUnknown) || riid == __uuidof(IDwnBindInfo))
		{
			*ppvObject = static_cast<IUnknown*>(static_cast<CDwnBindInfo1*>(this));
			InterlockedIncrement(&_dwRef);
			return S_OK;
		}
		*ppvObject=0;
		return E_NOINTERFACE;
	}

	virtual DWORD STDMETHODCALLTYPE AddRef()
	{
		return InterlockedIncrement(&_dwRef);
	}

	virtual DWORD STDMETHODCALLTYPE Release()
	{
		LONG dwRef = InterlockedDecrement(&_dwRef);
		if (!dwRef)
		{
			delete this;
		}
		return dwRef;
	}

	virtual HRESULT WINAPI BeginningTransaction(
		LPCWSTR szURL,
		LPCWSTR szHeaders,
		DWORD dwReserved,
		LPWSTR *pszAdditionalHeaders
		)
	{
		//DbgPrint("\r\nBeginningTransaction(%S)\r\n", szURL);

		LPWSTR sz = (LPWSTR)alloca(wcslen(_ref)*sizeof(WCHAR)+64), lpsz =
			(LPWSTR)CoTaskMemAlloc((swprintf(sz, L"Referer: %s\r\n", _ref)+1)<<1);

		if (lpsz)
		{
			wcscpy(lpsz, sz);
		}
		*pszAdditionalHeaders = lpsz;
		return S_OK;
	}

public:

	int OnException(PEXCEPTION_RECORD per, ::PCONTEXT cntx)
	{
		if (!this) return EXCEPTION_CONTINUE_SEARCH;
		DWORD dw;
		switch(per->ExceptionCode)
		{
		case STATUS_GUARD_PAGE_VIOLATION:
			if (1 < per->NumberParameters)
			{
				ULONG_PTR p = per->ExceptionInformation[1];

				if (RtlPointerToOffset(_DwnDoc, p) < PAGE_SIZE)
				{
					//DbgPrint("\r\n>>>>>>>>>>>> %x:%x\r\n", _state, RtlPointerToOffset(_DwnDoc, p));

					switch (_state++)
					{
					case 0:
						*(PDWORD)p = 0x400;
						cntx->ContextFlags |= CONTEXT_DEBUG_REGISTERS;
						cntx->EFlags |= TRACE_FLAG;
						break;
					case 1:
						*(BSTR*)p = _ref;
						cntx->ContextFlags |= CONTEXT_DEBUG_REGISTERS;
						cntx->EFlags |= TRACE_FLAG;
						break;
					case 2:
						*(BSTR*)p = _url;
						break;
					default: return EXCEPTION_EXECUTE_HANDLER;
					}

					return EXCEPTION_CONTINUE_EXECUTION;
				}
			}
			break;
		case STATUS_SINGLE_STEP:
			VirtualProtect(_DwnDoc, PAGE_SIZE, PAGE_READWRITE|PAGE_GUARD, &dw);
			return EXCEPTION_CONTINUE_EXECUTION;
		}
		return EXCEPTION_CONTINUE_SEARCH;
	}
};

class CDwnBindInfo : public RTL_FRAME<_CDwnBindInfo>
{
	CDwnBindInfo(BSTR ref, BSTR url)
	{
		//DbgPrint("CDwnBindInfo(%p)\r\n", this);
		_dwRef = 1;
		_state = 0;
		_ref = ref;
		_url = url;
		_zero = 0;
		_pad[0] = INVALID_HANDLE_VALUE;
		_pad[1] = INVALID_HANDLE_VALUE;
		_DwnDoc = 0;
	}

	~CDwnBindInfo()
	{
		if (_DwnDoc) VirtualFree(_DwnDoc, 0, MEM_RELEASE);
		//DbgPrint("%u:~CDwnBindInfo(%p)\r\n", GetTickCount(),this);
	}

	PVOID CreateDoc()
	{
		return _DwnDoc = VirtualAlloc(0, PAGE_SIZE, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE|PAGE_GUARD);
	}

public:
	static BOOL CreateDwnBindInfo(BSTR ref, BSTR url, IUnknown** ppUnk)
	{
		if (CDwnBindInfo* p = new CDwnBindInfo(ref, url))
		{
			if (p->CreateDoc())
			{
				*ppUnk = static_cast<IUnknown*>(static_cast<CDwnBindInfo1*>(p));
				return TRUE;
			}
			p->Release();
		}
		return FALSE;
	}
};

class _CDwnBindInfo_v10 : public CDwnBindInfo2_v10, public IDwnBindInfo
{
protected:
	BSTR _url, _ref;

	virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, __RPC__deref_out void **ppvObject)
	{
		if (riid == __uuidof(IUnknown) || riid == __uuidof(IDwnBindInfo))
		{
			*ppvObject = static_cast<IUnknown*>(static_cast<CDwnBindInfo1_v10*>(this));
			InterlockedIncrement(&_dwRef);
			return S_OK;
		}
		*ppvObject=0;
		return E_NOINTERFACE;
	}

	virtual DWORD STDMETHODCALLTYPE AddRef()
	{
		return InterlockedIncrement(&_dwRef);
	}

	virtual DWORD STDMETHODCALLTYPE Release()
	{
		LONG dwRef = InterlockedDecrement(&_dwRef);
		if (!dwRef)
		{
			delete this;
		}
		return dwRef;
	}

	virtual HRESULT WINAPI BeginningTransaction(
		LPCWSTR szURL,
		LPCWSTR szHeaders,
		DWORD dwReserved,
		LPWSTR *pszAdditionalHeaders
		)
	{
		//DbgPrint("\r\nBeginningTransaction(%S)\r\n", szURL);

		LPWSTR sz = (LPWSTR)alloca(wcslen(_ref)*sizeof(WCHAR)+64), lpsz =
			(LPWSTR)CoTaskMemAlloc((swprintf(sz, L"Referer: %s\r\n", _ref)+1)<<1);

		if (lpsz)
		{
			wcscpy(lpsz, sz);
		}
		*pszAdditionalHeaders = lpsz;
		return S_OK;
	}

public:

	int OnException(PEXCEPTION_RECORD per, ::PCONTEXT cntx)
	{
		if (!this) return EXCEPTION_CONTINUE_SEARCH;
		DWORD dw;
		switch(per->ExceptionCode)
		{
		case STATUS_GUARD_PAGE_VIOLATION:
			if (1 < per->NumberParameters)
			{
				ULONG_PTR p = per->ExceptionInformation[1];

				if (RtlPointerToOffset(_DwnDoc, p) < PAGE_SIZE)
				{
					//DbgPrint("\r\n>>>>>>>>>>>> %x:%x\r\n", _state, RtlPointerToOffset(_DwnDoc, p));

					switch (_state++)
					{
					case 0:
						*(PDWORD)p = 0x400;
						cntx->ContextFlags |= CONTEXT_DEBUG_REGISTERS;
						cntx->EFlags |= TRACE_FLAG;
						break;
					case 1:
						*(BSTR*)p = _ref;
						cntx->ContextFlags |= CONTEXT_DEBUG_REGISTERS;
						cntx->EFlags |= TRACE_FLAG;
						break;
					case 2:
						*(BSTR*)p = _url;
						break;
					default: return EXCEPTION_EXECUTE_HANDLER;
					}

					return EXCEPTION_CONTINUE_EXECUTION;
				}
			}
			break;
		case STATUS_SINGLE_STEP:
			VirtualProtect(_DwnDoc, PAGE_SIZE, PAGE_READWRITE|PAGE_GUARD, &dw);
			return EXCEPTION_CONTINUE_EXECUTION;
		}
		return EXCEPTION_CONTINUE_SEARCH;
	}
};

class CDwnBindInfo_v10 : public RTL_FRAME<_CDwnBindInfo_v10>
{
	~CDwnBindInfo_v10()
	{
		if (_DwnDoc) VirtualFree(_DwnDoc, 0, MEM_RELEASE);
		//DbgPrint("%u:~CDwnBindInfo(%p)\r\n", GetTickCount(),this);
	}

	CDwnBindInfo_v10(BSTR ref, BSTR url)
	{
		//DbgPrint("CDwnBindInfo(%p)\r\n", this);
		_dwRef = 1;
		_state = 0;
		_ref = ref;
		_url = url;
		_zero = 0;
		_pad[0] = INVALID_HANDLE_VALUE;
		_pad[1] = INVALID_HANDLE_VALUE;
		_DwnDoc = 0;
	}

	PVOID CreateDoc()
	{
		return _DwnDoc = VirtualAlloc(0, PAGE_SIZE, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE|PAGE_GUARD);
	}

public:

	static BOOL CreateDwnBindInfo(BSTR ref, BSTR url, IUnknown** ppUnk)
	{
		if (CDwnBindInfo_v10* p = new CDwnBindInfo_v10(ref, url))
		{
			if (p->CreateDoc())
			{
				*ppUnk = static_cast<IUnknown*>(static_cast<CDwnBindInfo1_v10*>(p));
				return TRUE;
			}
			p->Release();
		}
		return FALSE;
	}
};

int OnException(PEXCEPTION_POINTERS pep)
{
	return CDwnBindInfo::get()->OnException(pep->ExceptionRecord, pep->ContextRecord);
}

int OnException_v10(PEXCEPTION_POINTERS pep)
{
	return CDwnBindInfo_v10::get()->OnException(pep->ExceptionRecord, pep->ContextRecord);
}

void CDocHost::_Navigate(BSTR url, BSTR ref)
{
	ULONG v = GetMsHtmlVersion();

	switch (v)
	{
	case 6:
	case 7:
	case 8:
	case 9:
	case 10:
	case 11:
	case 12:
		break;
	default: ref = 0;
	}

	if (!ref)
	{
		m_pDoc->put_URL(url);
		return;
	}

	IMoniker * pmk;

	if (!CreateURLMonikerEx(0, url, &pmk, URL_MK_UNIFORM))
	{
		IPersistMoniker* ppm;
		if (!m_pDoc->QueryInterface(IID_PPV(ppm)))
		{
			IBindCtx* pbc;
			if (!CreateBindCtx(0, &pbc))
			{
				IUnknown* pUnk;
				if (v >= 10 ? CDwnBindInfo_v10::CreateDwnBindInfo(ref, url, &pUnk) : CDwnBindInfo::CreateDwnBindInfo(ref, url, &pUnk))
				{
					pbc->RegisterObjectParam(L"__DWNBINDINFO", pUnk);

					int (*pfnOnException)(PEXCEPTION_POINTERS pep) = 
						v >= 10 ? OnException_v10 : OnException;

					__try
					{
						ppm->Load(TRUE, pmk, pbc, 0);
					}
					__except(pfnOnException(GetExceptionInformation()))
					{
					}

					pUnk->Release();
				}
				pbc->Release();
			}
			ppm->Release();
		}
		pmk->Release();
	}
}

void CDocHost::Navigate(PCWSTR url, PCWSTR ref)
{
	BSTR bref, burl;

	if (burl = SysAllocString(url))
	{
		if (ref)
		{
			if (bref = SysAllocString(ref))
			{
				_Navigate(burl, bref);
				SysFreeString(bref);
			}
		}
		else
		{
			_Navigate(burl);
		}

		SysFreeString(burl);
	}
}

_NT_END