#pragma once

struct BMEX 
{
	USHORT ForAlign;
	BITMAPFILEHEADER bfh;
	BITMAPINFOHEADER bih;
	UCHAR Bits[];
};

class VBmp
{
	HBITMAP _hBmp = 0;
	HANDLE _hSection = 0;
	PVOID _Bits = 0, _buf = 0;
	ULONG _cx = 0, _cy = 0, _BufSize, _biSizeImage = 0;
	LONG _dwRef = 1;

	~VBmp()
	{
		if (_buf)
		{
			delete [] _buf;
		}

		if (_hBmp)
		{
			DeleteObject(_hBmp);
		}

		if (_hSection)
		{
			NtClose(_hSection);
		}
	}

public:

	enum { e_set = WM_USER, e_update, e_save };

	VBmp(ULONG BufSize) : _BufSize(BufSize)
	{
	}

	void AddRef()
	{
		InterlockedIncrementNoFence(&_dwRef);
	}

	void Release()
	{
		if (!InterlockedDecrement(&_dwRef))
		{
			delete this;
		}
	}

	PVOID Create(LONG cx, LONG cy);

	HGDIOBJ Select(HDC hdc)
	{
		return SelectObject(hdc, _hBmp);
	}

	ULONG width()
	{
		return _cx;
	}

	ULONG height()
	{
		return _cy;
	}

	PBITMAPFILEHEADER GetFileBuffer()
	{
		return &CONTAINING_RECORD(_Bits, BMEX, Bits)->bfh;
	}

	PVOID GetBits()
	{
		return _Bits;
	}

	PVOID GetBuf()
	{
		return _buf;
	}

	ULONG BufSize()
	{
		return _BufSize;
	}

	ULONG ImageSize()
	{
		return _biSizeImage;
	}
};

void YUY2toRGBA(PBYTE ptrIn, PULONG prgba, ULONG cx, ULONG cy);