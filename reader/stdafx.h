#define SECURITY_WIN32
#include "../inc/stdafx.h"

#include <wincodec.h  >


_NT_BEGIN
#include <cfgmgr32.h>

typedef IUnknown* PUNKNOWN;
#undef INTERFACE

#include <ks.h>
#include <ksmedia.h>
#include <initguid.h>
#include <devpkey.h>
#include <uuids.h>

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
_NT_END

#pragma warning(disable : 4200)