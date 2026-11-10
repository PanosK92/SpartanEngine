/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

// the browser build leaves out the freeimage plugins that need libtiff, openexr, libraw, libwebp, openjpeg and jxrlib
// freeimage numbers formats in registration order, so each one still registers, it just can't load or save
#include "FreeImage.h"
#include "Utilities.h"
#include "Plugin.h"

#define SPARTAN_FREEIMAGE_STUB(init_name, format_name, extension_list)            \
    static const char* DLL_CALLCONV init_name##_format()                          \
    {                                                                             \
        return format_name;                                                       \
    }                                                                             \
    static const char* DLL_CALLCONV init_name##_extension()                       \
    {                                                                             \
        return extension_list;                                                    \
    }                                                                             \
    void DLL_CALLCONV init_name(Plugin* plugin, int format_id)                    \
    {                                                                             \
        plugin->format_proc    = init_name##_format;                              \
        plugin->extension_proc = init_name##_extension;                           \
    }

SPARTAN_FREEIMAGE_STUB(InitTIFF, "TIFF", "tif,tiff")
SPARTAN_FREEIMAGE_STUB(InitG3,   "G3",   "g3")
SPARTAN_FREEIMAGE_STUB(InitEXR,  "EXR",  "exr")
SPARTAN_FREEIMAGE_STUB(InitJ2K,  "J2K",  "j2k,j2c")
SPARTAN_FREEIMAGE_STUB(InitJP2,  "JP2",  "jp2")
SPARTAN_FREEIMAGE_STUB(InitRAW,  "RAW",  "3fr,arw,bay,bmq,cap,cine,cr2,crw,cs1,dc2,dcr,drf,dsc,dng,erf,fff,ia,iiq,k25,kc2,kdc,mdc,mef,mos,mrw,nef,nrw,orf,pef,ptx,pxn,qtk,raf,raw,rdc,rw2,rwl,rwz,sr2,srf,srw,sti,x3f")
SPARTAN_FREEIMAGE_STUB(InitWEBP, "WEBP", "webp")
SPARTAN_FREEIMAGE_STUB(InitJXR,  "JPEG-XR", "jxr,wdp,hdp")
