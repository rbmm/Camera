#define SECURITY_WIN32
#include "../inc/stdafx.h"
#include <commctrl.h>
#include <WINDOWSX.H>

#include <wincodec.h  >
#include <ShlObj_core.h >
#include <ShObjIdl_core.h >
_NT_BEGIN
#include <cfgmgr32.h>

typedef IUnknown *PUNKNOWN;
#undef INTERFACE

#include <ks.h>
#include <ksmedia.h>
#include <initguid.h>
#include <devpkey.h>
#include <uuids.h>
_NT_END
#pragma warning(disable : 4200)
