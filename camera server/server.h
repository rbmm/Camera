#pragma once

struct VFI 
{
	ULONG biCompression;
	ULONG biWidth : 12;
	ULONG biHeight : 12;
	ULONG FPS : 6;
};

struct SELECT
{
	ULONG i, j;
};

extern bool _G_stop;