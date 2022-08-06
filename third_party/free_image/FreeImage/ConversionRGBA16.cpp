// ==========================================================
// Bitmap conversion routines
//
// Design and implementation by
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

#include "FreeImage.h"
#include "Utilities.h"

// ----------------------------------------------------------
//   smart convert X to RGBA16
// ----------------------------------------------------------

FIBITMAP * DLL_CALLCONV
FreeImage_ConvertToRGBA16(FIBITMAP *dib) {
	FIBITMAP *src = NULL;
	FIBITMAP *dst = NULL;

	if(!FreeImage_HasPixels(dib)) return NULL;

	const FREE_IMAGE_TYPE src_type = FreeImage_GetImageType(dib);

	// check for allowed conversions 
	switch(src_type) {
		case FIT_BITMAP:
		{
			// convert to 32-bit if needed
			if(FreeImage_GetBPP(dib) == 32) {
				src = dib;
			} else {
				src = FreeImage_ConvertTo32Bits(dib);
				if(!src) return NULL;
			}
			break;
		}
		case FIT_UINT16:
			// allow conversion from unsigned 16-bit
			src = dib;
			break;
		case FIT_RGB16:
			// allow conversion from 48-bit RGB
			src = dib;
			break;
		case FIT_RGBA16:
			// RGBA16 type : clone the src
			return FreeImage_Clone(dib);
			break;
		default:
			return NULL;
	}

	// allocate dst image

	const unsigned width = FreeImage_GetWidth(src);
	const unsigned height = FreeImage_GetHeight(src);

	dst = FreeImage_AllocateT(FIT_RGBA16, width, height);
	if(!dst) {
		if(src != dib) {
			FreeImage_Unload(src);
		}
		return NULL;
	}

	// copy metadata from src to dst
	FreeImage_CloneMetadata(dst, src);

	// convert from src type to RGBA16

	switch(src_type) {
		case FIT_BITMAP:
		{
			// Calculate the number of bytes per pixel (4 for 32-bit)
			const unsigned bytespp = FreeImage_GetLine(src) / FreeImage_GetWidth(src);

			for(unsigned y = 0; y < height; y++) {
				const BYTE *src_bits = (BYTE*)FreeImage_GetScanLine(src, y);
				FIRGBA16 *dst_bits = (FIRGBA16*)FreeImage_GetScanLine(dst, y);
				for(unsigned x = 0; x < width; x++) {
					dst_bits[x].red		= src_bits[FI_RGBA_RED] << 8;
					dst_bits[x].green	= src_bits[FI_RGBA_GREEN] << 8;
					dst_bits[x].blue	= src_bits[FI_RGBA_BLUE] << 8;
					dst_bits[x].alpha	= src_bits[FI_RGBA_ALPHA] << 8;
					src_bits += bytespp;
				}
			}
		}
		break;

		case FIT_UINT16:
		{
			for(unsigned y = 0; y < height; y++) {
				const WORD *src_bits = (WORD*)FreeImage_GetScanLine(src, y);
				FIRGBA16 *dst_bits = (FIRGBA16*)FreeImage_GetScanLine(dst, y);
				for(unsigned x = 0; x < width; x++) {
					// convert by copying greyscale channel to each R, G, B channels
					dst_bits[x].red   = src_bits[x];
					dst_bits[x].green = src_bits[x];
					dst_bits[x].blue  = src_bits[x];
					dst_bits[x].alpha = 0xFFFF;
				}
			}
		}
		break;

		case FIT_RGB16:
		{
			for(unsigned y = 0; y < height; y++) {
				const FIRGB16 *src_bits = (FIRGB16*)FreeImage_GetScanLine(src, y);
				FIRGBA16 *dst_bits = (FIRGBA16*)FreeImage_GetScanLine(dst, y);
				for(unsigned x = 0; x < width; x++) {
					// convert pixels directly, while adding a "dummy" alpha of 1.0
					dst_bits[x].red   = src_bits[x].red;
					dst_bits[x].green = src_bits[x].green;
					dst_bits[x].blue  = src_bits[x].blue;
					dst_bits[x].alpha = 0xFFFF;
				}
			}
		}
		break;

		default:
			break;
	}

	if(src != dib) {
		FreeImage_Unload(src);
	}

	return dst;
}

