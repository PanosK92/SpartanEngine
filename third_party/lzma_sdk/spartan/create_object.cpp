// createobject entry for static format7zr, no dll and no dllmain
#include "StdAfx.h"

#include "../CPP/Common/MyWindows.h"
#include "../CPP/Common/MyInitGuid.h"

#include "../CPP/7zip/ICoder.h"
#include "../CPP/7zip/IPassword.h"
#include "../CPP/7zip/IDecl.h"
#include "../CPP/7zip/Common/CreateCoder.h"
#include "../CPP/7zip/Archive/IArchive.h"

#ifdef _WIN32
HINSTANCE g_hInstance = nullptr;
#endif

Z7_DEFINE_GUID(CLSID_CArchiveHandler,
    k_7zip_GUID_Data1,
    k_7zip_GUID_Data2,
    k_7zip_GUID_Data3_Common,
    0x10, 0x00, 0x00, 0x01, 0x10, 0x00, 0x00, 0x00);

#define DEFINE_GUID_ARC(name, id) Z7_DEFINE_GUID(name, \
  0x23170F69, 0x40C1, 0x278A, 0x10, 0x00, 0x00, 0x01, 0x10, id, 0x00, 0x00);

DEFINE_GUID_ARC(CLSID_Format, 7)

STDAPI CreateCoder(const GUID* clsid, const GUID* iid, void** outObject);
STDAPI CreateHasher(const GUID* clsid, IHasher** hasher);
STDAPI CreateArchiver(const GUID* clsid, const GUID* iid, void** outObject);

STDAPI CreateObject(const GUID* clsid, const GUID* iid, void** outObject)
{
    *outObject = nullptr;
    if (*iid == IID_ICompressCoder ||
        *iid == IID_ICompressCoder2 ||
        *iid == IID_ICompressFilter)
    {
        return CreateCoder(clsid, iid, outObject);
    }
    if (*iid == IID_IHasher)
    {
        return CreateHasher(clsid, (IHasher**)outObject);
    }
    return CreateArchiver(clsid, iid, outObject);
}
