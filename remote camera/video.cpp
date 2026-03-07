#include "stdafx.h"

_NT_BEGIN

#include "video.h"

PVOID VBmp::Create(LONG cx, LONG cy)
{
	ULONG size = cx * cy << 2;

	if (HANDLE hSection = CreateFileMappingW(INVALID_HANDLE_VALUE, 0, PAGE_READWRITE, 0, size + sizeof(BMEX), 0))
	{
		BITMAPINFO bi = { 
			{ sizeof(BITMAPINFOHEADER), cx, -cy, 1, 32, BI_RGB, size } 
		};

		PVOID Bits = 0;

		if (HBITMAP hbmp = CreateDIBSection(0, &bi, DIB_RGB_COLORS, &Bits, hSection, __builtin_offsetof(BMEX, Bits)))
		{
			_hBmp = hbmp, _Bits = Bits, _cx = cx, _cy = cy, _biSizeImage = size, _hSection = hSection;

			BMEX* p = CONTAINING_RECORD(Bits, BMEX, Bits);
			p->bfh.bfType = 'MB';
			p->bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
			p->bfh.bfSize = size + sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
			p->bfh.bfReserved1 = 0;
			p->bfh.bfReserved2 = 0;

			memcpy(&p->bih, &bi.bmiHeader, sizeof(BITMAPINFOHEADER));

			if (_BufSize)
			{
				if (!(_buf = new UCHAR[_BufSize]))
				{
					_BufSize = 0;
				}
			}

			return Bits;
		}

		NtClose(hSection);
	}

	return 0;
}

BYTE Clamp(int val) {
	if (val > 255) return 255;
	if (val < 0) return 0;
	return (BYTE)val;
}

void NV12toRGBA(PBYTE yPlane, PULONG prgba, ULONG cx, ULONG cy)
{
	PBYTE uvPlane = yPlane + cx * cy; // UV начинается после Y

	for (ULONG y = 0; y < cy; y++)
	{
		for (ULONG x = 0; x < cx; x++)
		{
			int Y = yPlane[y * cx + x];

			// UV блок 2x2
			int uvIndex = (y / 2) * cx + (x & ~1); // (x & ~1) выравнивает по 2
			int U = uvPlane[uvIndex];
			int V = uvPlane[uvIndex + 1];

			// Конвертация YUV -> RGB (BT.601)
			int C = Y - 16;
			int D = U - 128;
			int E = V - 128;

			union {
				ULONG rgba = 0;
				struct {
					UCHAR B, G, R, A;
				};
			};

			R = Clamp((298 * C + 409 * E + 128) >> 8);
			G = Clamp((298 * C - 100 * D - 208 * E + 128) >> 8);
			B = Clamp((298 * C + 516 * D + 128) >> 8);

			*prgba++ = rgba;
		}
	}
}

_NT_END