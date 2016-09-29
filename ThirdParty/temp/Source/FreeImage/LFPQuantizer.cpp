// ==========================================================
// LFPQuantizer class implementation
//
// Design and implementation by
// - Carsten Klein (cklein05@users.sourceforge.net)
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

#include "Quantizers.h"
#include "FreeImage.h"
#include "Utilities.h"

LFPQuantizer::LFPQuantizer(unsigned PaletteSize) :
		m_size(0), m_limit(PaletteSize), m_index(0) {
	m_map = new MapEntry[MAP_SIZE];
	memset(m_map, 0xFF, MAP_SIZE * sizeof(MapEntry));
}

LFPQuantizer::~LFPQuantizer() {
    delete[] m_map;
}

FIBITMAP* LFPQuantizer::Quantize(FIBITMAP *dib, int ReserveSize, RGBQUAD *ReservePalette) {

	if (ReserveSize > 0 && ReservePalette != NULL) {
		AddReservePalette(ReservePalette, ReserveSize);
	}

	const unsigned width = FreeImage_GetWidth(dib);
	const unsigned height = FreeImage_GetHeight(dib);

	FIBITMAP *dib8 = FreeImage_Allocate(width, height, 8);
	if (dib8 == NULL) {
		return NULL;
	}

	const unsigned src_pitch = FreeImage_GetPitch(dib);
	const unsigned dst_pitch = FreeImage_GetPitch(dib8);

	const BYTE * const src_bits = FreeImage_GetBits(dib);
	BYTE * const dst_bits = FreeImage_GetBits(dib8);

	unsigned last_color = -1;
	int last_index = 0;

	if (FreeImage_GetBPP(dib) == 24) {

		// Getting the source pixel as an unsigned int is much faster than
		// working with FI_RGBA_xxx and shifting. However, this may fail
		// for the very last pixel, since its rgbReserved member (alpha)
		// may actually point to an address beyond the bitmap's memory. So,
		// we do not process the last scanline in the first loop.

		// Process all but the last scanline.
		for (unsigned y = 0; y < height - 1; ++y) {
			BYTE *dst_line = dst_bits + y * dst_pitch;
			const BYTE *src_line = src_bits + y * src_pitch;
			for (unsigned x = 0; x < width; ++x) {
				const unsigned color = *((unsigned *) src_line) & 0x00FFFFFF;
				if (color != last_color) {
					last_color = color;
					last_index = GetIndexForColor(color);
					if (last_index == -1) {
						FreeImage_Unload(dib8);
						return NULL;
					}
				}
				dst_line[x] = last_index;
				src_line += 3;
			}
		}

		// Process all but the last pixel of the last scanline.
		BYTE *dst_line = dst_bits + (height - 1) * dst_pitch;
		const BYTE *src_line = src_bits + (height - 1) * src_pitch;
		for (unsigned x = 0; x < width - 1; ++x) {
			const unsigned color = *((unsigned *) src_line) & 0x00FFFFFF;
			if (color != last_color) {
				last_color = color;
				last_index = GetIndexForColor(color);
				if (last_index == -1) {
					FreeImage_Unload(dib8);
					return NULL;
				}
			}
			dst_line[x] = last_index;
			src_line += 3;
		}

		// Process the last pixel (src_line should already point to it).
		const unsigned color = 0 | src_line[FI_RGBA_BLUE] << FI_RGBA_BLUE_SHIFT
				| src_line[FI_RGBA_GREEN] << FI_RGBA_GREEN_SHIFT
				| src_line[FI_RGBA_RED] << FI_RGBA_RED_SHIFT;
		if (color != last_color) {
			last_color = color;
			last_index = GetIndexForColor(color);
			if (last_index == -1) {
				FreeImage_Unload(dib8);
				return NULL;
			}
		}
		dst_line[width - 1] = last_index;

	} else {
		for (unsigned y = 0; y < height; ++y) {
			BYTE *dst_line = dst_bits + y * dst_pitch;
			const BYTE *src_line = src_bits + y * src_pitch;
			for (unsigned x = 0; x < width; ++x) {
				const unsigned color = *((unsigned *) src_line) & 0x00FFFFFF;
				if (color != last_color) {
					last_color = color;
					last_index = GetIndexForColor(color);
					if (last_index == -1) {
						FreeImage_Unload(dib8);
						return NULL;
					}
				}
				dst_line[x] = last_index;
				src_line += 4;
			}
		}
	}

	WritePalette(FreeImage_GetPalette(dib8));
	return dib8;
}

/**
 * Returns the palette index of the specified color. Tries to put the
 * color into the map, if it's not already present in the map. In that
 * case, a new index is used for the color. Returns -1, if adding the
 * color would exceed the desired maximum number of colors in the
 * palette.
 * @param color the color to get the index from
 * @return the palette index of the specified color or -1, if there
 * is no space left in the palette
 */
inline int LFPQuantizer::GetIndexForColor(unsigned color) {
	unsigned bucket = hash(color) & (MAP_SIZE - 1);
	while (m_map[bucket].color != color) {
		if (m_map[bucket].color == EMPTY_BUCKET) {
			if (m_size == m_limit) {
				return -1;
			}
			m_map[bucket].color = color;
			m_map[bucket].index = m_index++;
			++m_size;
			break;
		}
		bucket = (bucket + 1) % MAP_SIZE;
	}
	return m_map[bucket].index;
}

/**
 * Adds the specified number of entries of the specified reserve
 * palette to the newly created palette.
 * @param *palette a pointer to the reserve palette to copy from
 * @param size the number of entries to copy
 */
void LFPQuantizer::AddReservePalette(const void *palette, unsigned size) {
	if (size > MAX_SIZE) {
		size = MAX_SIZE;
	}
	unsigned *ppal = (unsigned *) palette;
	const unsigned offset = m_limit - size;
	for (unsigned i = 0; i < size; ++i) {
		const unsigned color = *ppal++;
		const unsigned index = i + offset;
		unsigned bucket = hash(color) & (MAP_SIZE - 1);
		while((m_map[bucket].color != EMPTY_BUCKET) && (m_map[bucket].color != color)) {
			bucket = (bucket + 1) % MAP_SIZE;
		}
		if(m_map[bucket].color != color) {
			m_map[bucket].color = color;
			m_map[bucket].index = index;
		}
	}
	m_size += size;
}

/**
 * Copies the newly created palette into the specified destination
 * palette. Although unused palette entries are not overwritten in
 * the destination palette, it is assumed to have space for at
 * least 256 entries.
 * @param palette a pointer to the destination palette
 */
void LFPQuantizer::WritePalette(void *palette) {
	for (unsigned i = 0; i < MAP_SIZE; ++i) {
		if (m_map[i].color != EMPTY_BUCKET) {
			((unsigned *) palette)[m_map[i].index] = m_map[i].color;
		}
	}
}
