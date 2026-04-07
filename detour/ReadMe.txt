========================================================================
    STATIC LIBRARY : detour Project Overview
========================================================================

AppWizard has created this detour library project for you. 

This file contains a summary of what you will find in each of the files that
make up your detour application.


detour.vcproj
    This is the main project file for VC++ projects generated using an Application Wizard. 
    It contains information about the version of Visual C++ that generated the file, and 
    information about the platforms, configurations, and project features selected with the
    Application Wizard.


/////////////////////////////////////////////////////////////////////////////

StdAfx.h, StdAfx.cpp
    These files are used to build a precompiled header (PCH) file
    named detour.pch and a precompiled types file named StdAfx.obj.

/////////////////////////////////////////////////////////////////////////////
Other notes:

AppWizard uses "TODO:" comments to indicate parts of the source code you
should add to or customize.

/////////////////////////////////////////////////////////////////////////////
// These macros must exactly match those in the Windows SDK's intsafe.h.
#define INT8_MIN         (-127i8 - 1)
#define INT16_MIN        (-32767i16 - 1)
#define INT32_MIN        (-2147483647i32 - 1)
#define INT64_MIN        (-9223372036854775807i64 - 1)
#define INT8_MAX         127i8
#define INT16_MAX        32767i16
#define INT32_MAX        2147483647i32
#define INT64_MAX        9223372036854775807i64
#define UINT8_MAX        0xffui8
#define UINT16_MAX       0xffffui16
#define UINT32_MAX       0xffffffffui32
#define UINT64_MAX       0xffffffffffffffffui64

#ifdef _WIN64
#define INTPTR_MIN   INT64_MIN
#define INTPTR_MAX   INT64_MAX
#define UINTPTR_MAX  UINT64_MAX
#else
#define INTPTR_MIN   INT32_MIN
#define INTPTR_MAX   INT32_MAX
#define UINTPTR_MAX  UINT32_MAX
#endif

PVOID Allocate2GBRange(UINT_PTR address, SIZE_T dwSize)
{
	static ULONG dwAllocationGranularity;

	if (!dwAllocationGranularity)
	{
		SYSTEM_INFO si;
		GetSystemInfo(&si);
		dwAllocationGranularity = si.dwAllocationGranularity;
	}
	
	UINT_PTR min, max, addr, add = dwAllocationGranularity - 1, mask = ~add;

	min = address >= 0x80000000 ? (address - 0x80000000 + add) & mask : 0;
	max = address < (UINTPTR_MAX - 0x80000000) ? (address + 0x80000000) & mask : UINTPTR_MAX;

	::MEMORY_BASIC_INFORMATION mbi; 
	do 
	{
		if (!VirtualQuery((void*)min, &mbi, sizeof(mbi))) return NULL;

		min = (UINT_PTR)mbi.BaseAddress + mbi.RegionSize;

		if (mbi.State == MEM_FREE)
		{
			addr = ((UINT_PTR)mbi.BaseAddress + add) & mask;

			if (addr < min && dwSize <= (min - addr))
			{
				if (addr = (UINT_PTR)VirtualAlloc((PVOID)addr, dwSize, MEM_COMMIT|MEM_RESERVE, PAGE_EXECUTE_READWRITE))
					return (PVOID)addr;
			}
		}


	} while (min < max);

	return NULL;
}
