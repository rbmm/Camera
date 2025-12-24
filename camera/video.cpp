#include "stdafx.h"

_NT_BEGIN

#include "video.h"

PVOID VBmp::Create(LONG cx, LONG cy)
{
	ULONG size = cx * cy << 2;

	BITMAPINFO bi = { 
		{ sizeof(BITMAPINFOHEADER), cx, -cy, 1, 32, BI_RGB, size } 
	};

	PVOID Bits = 0;

	if (HBITMAP hbmp = CreateDIBSection(0, &bi, DIB_RGB_COLORS, &Bits, 0, 0))
	{
		_hBmp = hbmp, _Bits = Bits, _cx = cx, _cy = cy, _biSizeImage = size;

		if (_BufSize)
		{
			if (!(_buf = new UCHAR[_BufSize]))
			{
				_BufSize = 0;
			}
		}

		return Bits;
	}

	return 0;
}

BYTE Clamp(int val) {
	if (val > 255) return 255;
	if (val < 0) return 0;
	return (BYTE)val;
}

void YUY2toRGBA(PBYTE ptrIn, PULONG prgba, ULONG cx, ULONG cy)
{
	cx >>= 1;

	union {
		ULONG rgba = 0;
		struct {
			UCHAR R, G, B, A;
		};
	};

	do 
	{
		ULONG s = cx;
		do 
		{
			LONG y0 = 298 * ((LONG)*ptrIn++ - 16);
			LONG u0 = (LONG)*ptrIn++ - 128;
			LONG y1 = 298 * ((LONG)*ptrIn++ - 16);
			LONG v0 = (LONG)*ptrIn++ - 128;

			LONG a = 516 * u0 + 128;
			LONG b = 128 - 100 * u0 - 208 * v0;
			LONG c = 409 * v0 + 128;

			R = Clamp(( y0 + a) >> 8);
			G = Clamp(( y0 + b) >> 8);
			B = Clamp(( y0 + c) >> 8);

			*prgba++ = rgba;

			R = Clamp(( y1 + a) >> 8);
			G = Clamp(( y1 + b) >> 8);
			B = Clamp(( y1 + c) >> 8);

			*prgba++ = rgba;

		} while (--s);

	} while (--cy);
}

_NT_END