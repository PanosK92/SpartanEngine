// ==========================================================
// GetType / Validate
//
// Design and implementation by
// - Floris van den Berg (flvdberg@wxs.nl)
// - Hervé Drolon (drolon@infonie.fr)
//
// This file is part of FreeImage 3
//
// COVERED CODE IS PROVIDED UNDER THIS LICENSE ON AN "AS IS" BASIS, WITHOUT WARRANTY
// OF ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING, WITHOUT LIMITATION, WARRANTIES
// THAT THE COVERED CODE IS FREE OF DEFECTS, MERCHANTABLE, FIT FOR A PARTICULAR PURPOSE
// OR NON-INFRINGING. THE ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE OF THE COVERED
// CODE IS WITH YOU. SHOULD ANY COVERED CODE PROVE DEFECTIVE IN ANY RESPECT, YOU (NOT
// THE INITIAL DEVELOPER OR ANY OTHER CONTRIBUTOR) ASSUME THE COST OF ANY NECESSARY
// SERVICING, REPAIR OR CORRECTION. THIS DISCLAIMER OF WARRANTY CONSTITUTES AN ESSENTIAL
// PART OF THIS LICENSE. NO USE OF ANY COVERED CODE IS AUTHORIZED HEREUNDER EXCEPT UNDER
// THIS DISCLAIMER.
//
// Use at your own risk!
// ==========================================================

#ifdef _MSC_VER 
#pragma warning (disable : 4786) // identifier was truncated to 'number' characters
#endif 

#include "FreeImage.h"
#include "Utilities.h"
#include "FreeImageIO.h"
#include "Plugin.h"

// =====================================================================
// Generic stream file type access
// =====================================================================

FREE_IMAGE_FORMAT DLL_CALLCONV
FreeImage_GetFileTypeFromHandle(FreeImageIO *io, fi_handle handle, int size) {
	if (handle != NULL) {
		int fif_count = FreeImage_GetFIFCount();

		for (int i = 0; i < fif_count; ++i) {
			FREE_IMAGE_FORMAT fif = (FREE_IMAGE_FORMAT)i;
			if (FreeImage_ValidateFIF(fif, io, handle)) {
				if(fif == FIF_TIFF) {
					// many camera raw files use a TIFF signature ...
					// ... try to revalidate against FIF_RAW (even if it breaks the code genericity)
					if (FreeImage_ValidateFIF(FIF_RAW, io, handle)) {
						return FIF_RAW;
					}
				}
				return fif;
			}
		}
	}

	return FIF_UNKNOWN;
}

// =====================================================================
// File stream file type access
// =====================================================================

FREE_IMAGE_FORMAT DLL_CALLCONV
FreeImage_GetFileType(const char *filename, int size) {
	FreeImageIO io;
	SetDefaultIO(&io);
	
	FILE *handle = fopen(filename, "rb");

	if (handle != NULL) {
		FREE_IMAGE_FORMAT format = FreeImage_GetFileTypeFromHandle(&io, (fi_handle)handle, size);

		fclose(handle);

		return format;
	}

	return FIF_UNKNOWN;
}

FREE_IMAGE_FORMAT DLL_CALLCONV 
FreeImage_GetFileTypeU(const wchar_t *filename, int size) {
#ifdef _WIN32	
	FreeImageIO io;
	SetDefaultIO(&io);
	FILE *handle = _wfopen(filename, L"rb");

	if (handle != NULL) {
		FREE_IMAGE_FORMAT format = FreeImage_GetFileTypeFromHandle(&io, (fi_handle)handle, size);

		fclose(handle);

		return format;
	}
#endif
	return FIF_UNKNOWN;
}

// =====================================================================
// Memory stream file type access
// =====================================================================

FREE_IMAGE_FORMAT DLL_CALLCONV
FreeImage_GetFileTypeFromMemory(FIMEMORY *stream, int size) {
	FreeImageIO io;
	SetMemoryIO(&io);

	if (stream != NULL) {
		return FreeImage_GetFileTypeFromHandle(&io, (fi_handle)stream, size);
	}

	return FIF_UNKNOWN;
}

// --------------------------------------------------------------------------

BOOL DLL_CALLCONV
FreeImage_ValidateFromHandle(FREE_IMAGE_FORMAT fif, FreeImageIO *io, fi_handle handle) {
	return FreeImage_ValidateFIF(fif, io, handle);
}

BOOL DLL_CALLCONV
FreeImage_Validate(FREE_IMAGE_FORMAT fif, const char *filename) {
	FreeImageIO io;
	SetDefaultIO(&io);

	FILE *handle = fopen(filename, "rb");

	if (handle != NULL) {
		BOOL bIsValidFIF = FreeImage_ValidateFromHandle(fif, &io, (fi_handle)handle);
		fclose(handle);
		return bIsValidFIF;
	}

	return FALSE;
}

BOOL DLL_CALLCONV
FreeImage_ValidateU(FREE_IMAGE_FORMAT fif, const wchar_t *filename) {
#ifdef _WIN32	
	FreeImageIO io;
	SetDefaultIO(&io);
	FILE *handle = _wfopen(filename, L"rb");

	if (handle != NULL) {
		BOOL bIsValidFIF = FreeImage_ValidateFromHandle(fif, &io, (fi_handle)handle);
		fclose(handle);
		return bIsValidFIF;
	}
#endif
	return FALSE;
}

BOOL DLL_CALLCONV
FreeImage_ValidateFromMemory(FREE_IMAGE_FORMAT fif, FIMEMORY *stream) {
	FreeImageIO io;
	SetMemoryIO(&io);

	if (stream != NULL) {
		BOOL bIsValidFIF = FreeImage_ValidateFromHandle(fif, &io, (fi_handle)stream);
		return bIsValidFIF;
	}

	return FALSE;
}
