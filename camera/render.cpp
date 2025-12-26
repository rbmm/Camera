#include "stdafx.h"

_NT_BEGIN
#include "resource.h"
#include "../inc/idcres.h"
#include "../winZ/window.h"
#include "../winZ/cursors.h"

#include "utils.h"
#include "video.h"
#include "log.h"

class YCameraWnd : public ZWnd
{
	HDC _hMemDC = 0;
	VBmp* _vid = 0;
	HGDIOBJ _hso = 0;

	IWICImagingFactory *_piFactory = 0;

	HRESULT ConvertImage(IWICBitmapSource* pIBitmap);
	HRESULT ConvertImage(IWICBitmapDecoder *pIDecoder);
	HRESULT ConvertImage(ULONG cb);

	virtual LRESULT WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

	static LRESULT CALLBACK CustomStartWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

	void FreeVideo()
	{
		if (_vid)
		{
			SelectObject(_hMemDC, _hso);
			_vid->Release();
			_vid = 0;
		}
	}

	void DeleteMDC()
	{
		if (_hMemDC)
		{
			FreeVideo();
			DeleteDC(_hMemDC);
		}

		if (_piFactory) _piFactory->Release(), _piFactory = 0;
	}

	~YCameraWnd()
	{
		DbgPrint("%s<%p>\n", __FUNCTION__, this);
	}

	void Set(VBmp* vid);

public:

	static BOOL Register();

	static void Unregister();

	static inline WCHAR _S_ClassName[] = L"E746983BB3CA49f79A71322254EE5F6A";
};

LRESULT CALLBACK YCameraWnd::CustomStartWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == WM_NCCREATE)
	{
		if (YCameraWnd* p = new YCameraWnd)
		{
			lParam = p->MStartWindowProc(hwnd, uMsg, wParam, lParam);
			p->Release();
			return lParam;
		}

		return FALSE;
	}

	return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

BOOL YCameraWnd::Register()
{
	WNDCLASS cls = {
		0, CustomStartWindowProc, 0, 0, (HINSTANCE)&__ImageBase, 0, 
		CCursorCashe::GetCursor(CCursorCashe::ARROW), 
		0, 0, _S_ClassName
	};

	return RegisterClassW(&cls);
}

void YCameraWnd::Unregister()
{
	if (!UnregisterClassW(_S_ClassName, (HINSTANCE)&__ImageBase))
	{
		if (GetLastError() != ERROR_CLASS_DOES_NOT_EXIST)
		{
			__debugbreak();
		}
	}
}

void YCameraWnd::Set(VBmp* vid)
{
	FreeVideo();

	if (vid)
	{
		if (HGDIOBJ hso = vid->Select(_hMemDC))
		{
			_hso = hso;
			vid->AddRef();
			_vid = vid;
		}
	}
}

HRESULT YCameraWnd::ConvertImage(IWICBitmapSource* pIBitmap)
{
	UINT cx, cy;

	HRESULT hr = pIBitmap->GetSize(&cx, &cy);

	if (0 <= hr)
	{
		VBmp* vid = _vid;

		if (vid->width() >= cx && vid->height() >= cy)
		{
			UINT cbStride = cx << 2, cbBufferSize = cbStride * cy;

			if (cbBufferSize <= vid->ImageSize())
			{
				return pIBitmap->CopyPixels(0, cbStride, cbBufferSize, (PBYTE)vid->GetBits());
			}
		}

		return STATUS_BUFFER_TOO_SMALL;
	}

	return hr;
}

HRESULT YCameraWnd::ConvertImage(IWICBitmapDecoder *pIDecoder)
{
	HRESULT hr;
	IWICBitmapFrameDecode* pIBitmapFrame;

	if (0 <= (hr = pIDecoder->GetFrame(0, &pIBitmapFrame)))
	{
		WICPixelFormatGUID pf;
		if (0 <= (hr = pIBitmapFrame->GetPixelFormat(&pf)))
		{
			if (pf == GUID_WICPixelFormat32bppBGRA || pf == GUID_WICPixelFormat32bppBGR)
			{
				hr = ConvertImage(pIBitmapFrame);
			}
			else
			{
				IWICFormatConverter* pIFormatConverter;
				if (0 <= (hr = _piFactory->CreateFormatConverter(&pIFormatConverter)))
				{
					if (0 <= (hr = pIFormatConverter->Initialize(pIBitmapFrame, 
						GUID_WICPixelFormat32bppBGRA,
						WICBitmapDitherTypeNone, 0, 0.0, WICBitmapPaletteTypeCustom)))
					{
						hr = ConvertImage(pIFormatConverter);
					}

					pIFormatConverter->Release();
				}
			}
		}

		pIBitmapFrame->Release();
	}

	return hr;
}

HRESULT YCameraWnd::ConvertImage(ULONG cb)
{
	HRESULT hr = E_UNEXPECTED;

	if (IWICImagingFactory *piFactory = _piFactory)
	{
		if (WICInProcPointer pb = (WICInProcPointer)_vid->GetBuf())
		{
			IWICStream *pIWICStream;

			if (0 <= (hr = piFactory->CreateStream(&pIWICStream)))
			{
				if (0 <= (hr = pIWICStream->InitializeFromMemory(pb, cb)))
				{
					IWICBitmapDecoder *pIDecoder;
					if (0 <= (hr = piFactory->CreateDecoderFromStream(pIWICStream, 0, 
						WICDecodeMetadataCacheOnDemand, &pIDecoder)))
					{
						hr = ConvertImage(pIDecoder);

						pIDecoder->Release();
					}
				}
				pIWICStream->Release();
			}
		}
	}

	return hr;
}

LRESULT YCameraWnd::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	PAINTSTRUCT ps;

	switch (uMsg)
	{
	case WM_ERASEBKGND:
		GetClientRect(hwnd, &ps.rcPaint);
		FillRect((HDC)wParam, &ps.rcPaint, (HBRUSH)(1+CTLCOLOR_DLG));
		return TRUE;

	case WM_PAINT:
		if (BeginPaint(hwnd, &ps))
		{
			if (_vid)
			{
				BitBlt(ps.hdc, 0, 0, _vid->width(), _vid->height(), _hMemDC, 0, 0, SRCCOPY);
			}
			EndPaint(hwnd, &ps);
		}
		break;

	case VBmp::e_set:
		Set(reinterpret_cast<VBmp*>(lParam));
		if (!lParam)
		{
			InvalidateRect(hwnd, 0, TRUE);
		}
		break;

	case VBmp::e_update:
		if (wParam)
		{
			if (HRESULT hr = ConvertImage((ULONG)wParam))
			{
				DbgPrint("ConvertImage=%x\r\n", hr);
				break;
			}
		}
		InvalidateRect(hwnd, 0, FALSE);
		break;

	case WM_DESTROY:
		DeleteMDC();
		break;

	case WM_CREATE:
		if (HDC hdc = CreateCompatibleDC(0))
		{
			_hMemDC = hdc;

			CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, 
				__uuidof(IWICImagingFactory), (void**)&_piFactory);
			break;
		}
		return -1;

	}
	return __super::WindowProc(hwnd, uMsg, wParam, lParam);
}

_NT_END