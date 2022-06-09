// ==========================================================
// DDS Loader
//
// Design and implementation by
// - Volker Gärtner (volkerg@gmx.at)
// - Sherman Wilcox
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
//   Definitions for the RGB 444 format
// ----------------------------------------------------------
#define FI16_444_RED_MASK	0x0F00
#define FI16_444_GREEN_MASK	0x00F0
#define FI16_444_BLUE_MASK	0x000F
#define FI16_444_RED_SHIFT		8
#define FI16_444_GREEN_SHIFT	4
#define FI16_444_BLUE_SHIFT		0

// ----------------------------------------------------------
//   Definitions for RGB16 handling
// ----------------------------------------------------------

/**
The list of possible 16-bit formats
*/
typedef enum {
	RGB_UNKNOWN = -1,
	RGB444 = 1,
	RGB555 = 2,
	RGB565 = 3
} DDSFormat16;

/**
Get the 16-bit format of an image
@param dwRBitMask Red mask
@param dwGBitMask Green mask
@param dwBBitMask Blue mask
@return Returns the 16-bit format or RGB_UNKNOWN
*/
static inline DDSFormat16
GetRGB16Format(DWORD dwRBitMask, DWORD dwGBitMask, DWORD dwBBitMask) {
	if ((dwRBitMask == FI16_444_RED_MASK) && (dwGBitMask == FI16_444_GREEN_MASK) && (dwBBitMask == FI16_444_BLUE_MASK)) {
		return RGB444;
	}
	if ((dwRBitMask == FI16_555_RED_MASK) && (dwGBitMask == FI16_555_GREEN_MASK) && (dwBBitMask == FI16_555_BLUE_MASK)) {
		return RGB555;
	}
	if ((dwRBitMask == FI16_565_RED_MASK) && (dwGBitMask == FI16_565_GREEN_MASK) && (dwBBitMask == FI16_565_BLUE_MASK)) {
		return RGB565;
	}

	return RGB_UNKNOWN;
}

/**
Convert a 16-bit RGB line to a 24-bit RGB line
@param target 24-bit Destination line
@param source 16-bit Source line
@param format 16-bit format
@param width_in_pixels Size of the line in pixels
*/
static void 
ConvertLine16To24(BYTE *target, const WORD *source, DDSFormat16 format, int width_in_pixels) {

	// convert from RGB 16-bit to RGB 24-bit
	switch (format) {
		case RGB444:
			for (int cols = 0; cols < width_in_pixels; cols++) {
				// extract source RGB444 pixel, set to 24-bit target
				target[FI_RGBA_BLUE] = (BYTE)((((source[cols] & FI16_444_BLUE_MASK) >> FI16_444_BLUE_SHIFT) * 0xFF) / 0x0F);
				target[FI_RGBA_GREEN] = (BYTE)((((source[cols] & FI16_444_GREEN_MASK) >> FI16_444_GREEN_SHIFT) * 0xFF) / 0x0F);
				target[FI_RGBA_RED] = (BYTE)((((source[cols] & FI16_444_RED_MASK) >> FI16_444_RED_SHIFT) * 0xFF) / 0x0F);
				target += 3;
			}
			break;

		case RGB555:
			for (int cols = 0; cols < width_in_pixels; cols++) {
				target[FI_RGBA_RED] = (BYTE)((((source[cols] & FI16_555_RED_MASK) >> FI16_555_RED_SHIFT) * 0xFF) / 0x1F);
				target[FI_RGBA_GREEN] = (BYTE)((((source[cols] & FI16_555_GREEN_MASK) >> FI16_555_GREEN_SHIFT) * 0xFF) / 0x1F);
				target[FI_RGBA_BLUE] = (BYTE)((((source[cols] & FI16_555_BLUE_MASK) >> FI16_555_BLUE_SHIFT) * 0xFF) / 0x1F);
				target += 3;
			}
			break;

		case RGB565:
			for (int cols = 0; cols < width_in_pixels; cols++) {
				target[FI_RGBA_RED] = (BYTE)((((source[cols] & FI16_565_RED_MASK) >> FI16_565_RED_SHIFT) * 0xFF) / 0x1F);
				target[FI_RGBA_GREEN] = (BYTE)((((source[cols] & FI16_565_GREEN_MASK) >> FI16_565_GREEN_SHIFT) * 0xFF) / 0x3F);
				target[FI_RGBA_BLUE] = (BYTE)((((source[cols] & FI16_565_BLUE_MASK) >> FI16_565_BLUE_SHIFT) * 0xFF) / 0x1F);
				target += 3;
			}
			break;

		default:
			break;
	}
}

// ----------------------------------------------------------
//   Definitions for the DDS format
// ----------------------------------------------------------

#ifdef _WIN32
#pragma pack(push, 1)
#else
#pragma pack(1)
#endif

/**
DDS_PIXELFORMAT structure
*/
typedef struct tagDDPIXELFORMAT {
	/**
	Size of this structure (must be 32)
	*/
	DWORD dwSize;
	/**
	Values which indicate what type of data is in the surface, see DDPF_*
	*/
	DWORD dwFlags;
	/**
	Four-character codes for specifying compressed or custom formats. Possible values include: DXT1, DXT2, DXT3, DXT4, or DXT5. 
	A FourCC of DX10 indicates the prescense of the DDS_HEADER_DXT10 extended header, and the dxgiFormat member of that structure 
	indicates the true format. When using a four-character code, dwFlags must include DDPF_FOURCC.
	*/
	DWORD dwFourCC;
	/**
    Number of bits in an RGB (possibly including alpha) format. Valid when dwFlags includes DDPF_RGB, DDPF_LUMINANCE, or DDPF_YUV.
	*/
	DWORD dwRGBBitCount;	//! Total number of bits for RGB formats
	/**
	Red (or luminance or Y) mask for reading color data. For instance, given the A8R8G8B8 format, the red mask would be 0x00ff0000.
	*/
	DWORD dwRBitMask;
	/**
	Green (or U) mask for reading color data. For instance, given the A8R8G8B8 format, the green mask would be 0x0000ff00.
	*/
	DWORD dwGBitMask;
	/**
	Blue (or V) mask for reading color data. For instance, given the A8R8G8B8 format, the blue mask would be 0x000000ff.
	*/
	DWORD dwBBitMask;
	/**
	Alpha mask for reading alpha data. dwFlags must include DDPF_ALPHAPIXELS or DDPF_ALPHA. 
	For instance, given the A8R8G8B8 format, the alpha mask would be 0xff000000.
	*/
	DWORD dwRGBAlphaBitMask;
} DDPIXELFORMAT;

/** DIRECTDRAW PIXELFORMAT FLAGS */
enum {
	/** Texture contains alpha data; dwRGBAlphaBitMask contains valid data. */
	DDPF_ALPHAPIXELS = 0x1,
	/** Used in some older DDS files for alpha channel only uncompressed data (dwRGBBitCount contains the alpha channel bitcount; dwABitMask contains valid data) */
	DDPF_ALPHA = 0x2,
	/** Texture contains compressed RGB data; dwFourCC contains valid data. */
	DDPF_FOURCC = 0x4,
	/** Texture contains uncompressed RGB data; dwRGBBitCount and the RGB masks (dwRBitMask, dwGBitMask, dwBBitMask) contain valid data. */
	DDPF_RGB = 0x40,
	/**
	Used in some older DDS files for YUV uncompressed data (dwRGBBitCount contains the YUV bit count; 
	dwRBitMask contains the Y mask, dwGBitMask contains the U mask, dwBBitMask contains the V mask)
	*/
	DDPF_YUV = 0x200,
	/**
	Used in some older DDS files for single channel color uncompressed data (dwRGBBitCount contains the luminance channel bit count; 
	dwRBitMask contains the channel mask). Can be combined with DDPF_ALPHAPIXELS for a two channel DDS file.
	*/
	DDPF_LUMINANCE = 0x20000
};

typedef struct tagDDCAPS2 {
	DWORD dwCaps1;	//! zero or more of the DDSCAPS_* members
	DWORD dwCaps2;	//! zero or more of the DDSCAPS2_* members
	DWORD dwReserved[2];
} DDCAPS2;

/**
DIRECTDRAWSURFACE CAPABILITY FLAGS
*/
enum {
	/** Alpha only surface */
	DDSCAPS_ALPHA = 0x00000002,
	/**
	Optional; must be used on any file that contains more than one surface 
	(a mipmap, a cubic environment map, or mipmapped volume texture).
	*/
	DDSCAPS_COMPLEX	= 0x8,
	/** Used as texture (should always be set) */
	DDSCAPS_TEXTURE	= 0x1000,
	/**
	Optional; should be used for a mipmap.
	*/
	DDSCAPS_MIPMAP	= 0x400000
};

/**
Additional detail about the surfaces stored.
*/
enum {
	DDSCAPS2_CUBEMAP			= 0x200,	//! Required for a cube map.
	DDSCAPS2_CUBEMAP_POSITIVEX	= 0x400,	//! Required when these surfaces are stored in a cube map.
	DDSCAPS2_CUBEMAP_NEGATIVEX	= 0x800,	//! Required when these surfaces are stored in a cube map.
	DDSCAPS2_CUBEMAP_POSITIVEY	= 0x1000,	//! Required when these surfaces are stored in a cube map.
	DDSCAPS2_CUBEMAP_NEGATIVEY	= 0x2000,	//! Required when these surfaces are stored in a cube map.
	DDSCAPS2_CUBEMAP_POSITIVEZ	= 0x4000,	//! Required when these surfaces are stored in a cube map.
	DDSCAPS2_CUBEMAP_NEGATIVEZ	= 0x8000,	//! Required when these surfaces are stored in a cube map.
	DDSCAPS2_VOLUME				= 0x200000	//! Required for a volume texture.
};

/**
DDS_HEADER structure
*/
typedef struct tagDDSURFACEDESC2 {
	/**	Size of structure. This member must be set to 124 */
	DWORD dwSize;
	/** Combination of the DDSD_* flags */
	DWORD dwFlags;
	/**	Surface height (in pixels) */
	DWORD dwHeight;
	/**	Surface width (in pixels) */
	DWORD dwWidth;
	/**
	The pitch or number of bytes per scan line in an uncompressed texture; 
	the total number of bytes in the top level texture for a compressed texture. 
	For information about how to compute the pitch, see the DDS File Layout section of the Programming Guide for DDS.
	*/
	DWORD dwPitchOrLinearSize;
	/**	Depth of a volume texture (in pixels), otherwise unused */
	DWORD dwDepth;
	/**	Number of mipmap levels, otherwise unused */
	DWORD dwMipMapCount;
	/** Unused */
	DWORD dwReserved1[11];
	/** The pixel format(see DDS_PIXELFORMAT). */
	DDPIXELFORMAT ddspf;
	/** Specifies the complexity of the surfaces stored. */
	DDCAPS2 ddsCaps;
	DWORD dwReserved2;
} DDSURFACEDESC2;

/**
Flags to indicate which members contain valid data. 
*/
enum {
	DDSD_CAPS = 0x1,			//! Required in every .dds file
	DDSD_HEIGHT = 0x2,			//! Required in every .dds file
	DDSD_WIDTH = 0x4,			//! Required in every .dds file
	DDSD_PITCH = 0x8,			//! Required when pitch is provided for an uncompressed texture
	DDSD_ALPHABITDEPTH = 0x80,	//! unknown use
	DDSD_PIXELFORMAT = 0x1000,	//! Required in every .dds file
	DDSD_MIPMAPCOUNT = 0x20000,	//! Required in a mipmapped texture
	DDSD_LINEARSIZE = 0x80000,	//! Required when pitch is provided for a compressed texture
	DDSD_DEPTH = 0x800000		//! Required in a depth texture
};

typedef struct tagDDSHEADER {
	DWORD dwMagic;			//! FOURCC: "DDS "
	DDSURFACEDESC2 surfaceDesc;
} DDSHEADER;

#define MAKEFOURCC(ch0, ch1, ch2, ch3) \
	((DWORD)(BYTE)(ch0) | ((DWORD)(BYTE)(ch1) << 8) |   \
    ((DWORD)(BYTE)(ch2) << 16) | ((DWORD)(BYTE)(ch3) << 24 ))

#define FOURCC_DXT1	MAKEFOURCC('D','X','T','1')
#define FOURCC_DXT2	MAKEFOURCC('D','X','T','2')
#define FOURCC_DXT3	MAKEFOURCC('D','X','T','3')
#define FOURCC_DXT4	MAKEFOURCC('D','X','T','4')
#define FOURCC_DXT5	MAKEFOURCC('D','X','T','5')

// ----------------------------------------------------------
//   Structures used by DXT textures
// ----------------------------------------------------------

typedef struct tagColor8888 {
	BYTE b;
	BYTE g;
	BYTE r;
	BYTE a;
} Color8888;

typedef struct tagColor565 {
	WORD b : 5;
	WORD g : 6;
	WORD r : 5;
} Color565;

typedef struct tagDXTColBlock {
	Color565 colors[2];
	BYTE row[4];
} DXTColBlock;

typedef struct tagDXTAlphaBlockExplicit {
	WORD row[4];
} DXTAlphaBlockExplicit;

typedef struct tagDXTAlphaBlock3BitLinear {
	BYTE alpha[2];
	BYTE data[6];
} DXTAlphaBlock3BitLinear;

typedef struct tagDXT1Block {
	DXTColBlock color;
} DXT1Block;

typedef struct tagDXT3Block {		// also used by dxt2
	DXTAlphaBlockExplicit alpha;
	DXTColBlock color;
} DXT3Block;

typedef struct tagDXT5Block {		// also used by dxt4
	DXTAlphaBlock3BitLinear alpha;
	DXTColBlock color;
} DXT5Block;

#ifdef _WIN32
#	pragma pack(pop)
#else
#	pragma pack()
#endif

// ----------------------------------------------------------
//   Internal functions
// ----------------------------------------------------------
#ifdef FREEIMAGE_BIGENDIAN
static void
SwapHeader(DDSHEADER *header) {
	SwapLong(&header->dwMagic);
	SwapLong(&header->surfaceDesc.dwSize);
	SwapLong(&header->surfaceDesc.dwFlags);
	SwapLong(&header->surfaceDesc.dwHeight);
	SwapLong(&header->surfaceDesc.dwWidth);
	SwapLong(&header->surfaceDesc.dwPitchOrLinearSize);
	SwapLong(&header->surfaceDesc.dwDepth);
	SwapLong(&header->surfaceDesc.dwMipMapCount);
	for(int i=0; i<11; i++) {
		SwapLong(&header->surfaceDesc.dwReserved1[i]);
	}
	SwapLong(&header->surfaceDesc.ddpfPixelFormat.dwSize);
	SwapLong(&header->surfaceDesc.ddpfPixelFormat.dwFlags);
	SwapLong(&header->surfaceDesc.ddpfPixelFormat.dwFourCC);
	SwapLong(&header->surfaceDesc.ddpfPixelFormat.dwRGBBitCount);
	SwapLong(&header->surfaceDesc.ddpfPixelFormat.dwRBitMask);
	SwapLong(&header->surfaceDesc.ddpfPixelFormat.dwGBitMask);
	SwapLong(&header->surfaceDesc.ddpfPixelFormat.dwBBitMask);
	SwapLong(&header->surfaceDesc.ddpfPixelFormat.dwRGBAlphaBitMask);
	SwapLong(&header->surfaceDesc.ddsCaps.dwCaps1);
	SwapLong(&header->surfaceDesc.ddsCaps.dwCaps2);
	SwapLong(&header->surfaceDesc.ddsCaps.dwReserved[0]);
	SwapLong(&header->surfaceDesc.ddsCaps.dwReserved[1]);
	SwapLong(&header->surfaceDesc.dwReserved2);
}
#endif

// ==========================================================

/**
Get the 4 possible colors for a block
*/
static void 
GetBlockColors(const DXTColBlock *block, Color8888 colors[4], bool isDXT1) {

	// expand from 565 to 888
	for (int i = 0; i < 2; i++)	{
		colors[i].a = 0xFF;
		/*
		colors[i].r = (BYTE)(unsigned(block->colors[i].r) * 0xFF / 0x1F);
		colors[i].g = (BYTE)(unsigned(block->colors[i].g) * 0xFF / 0x3F);
		colors[i].b = (BYTE)(unsigned(block->colors[i].b) * 0xFF / 0x1F);
		*/
		colors[i].r = (BYTE)((unsigned(block->colors[i].r) << 3U) | (unsigned(block->colors[i].r) >> 2U));
		colors[i].g = (BYTE)((unsigned(block->colors[i].g) << 2U) | (unsigned(block->colors[i].g) >> 4U));
		colors[i].b = (BYTE)((unsigned(block->colors[i].b) << 3U) | (unsigned(block->colors[i].b) >> 2U));
	}

	const WORD *wCol = (WORD *)block->colors;
	if ((wCol[0] > wCol[1]) || !isDXT1) {
		// 4 color block
		for (unsigned i = 0; i < 2; i++)	{
			colors[i + 2].a = 0xFF;
			colors[i + 2].r = (BYTE)((unsigned(colors[0].r) * (2 - i) + unsigned(colors[1].r) * (1 + i)) / 3);
			colors[i + 2].g = (BYTE)((unsigned(colors[0].g) * (2 - i) + unsigned(colors[1].g) * (1 + i)) / 3);
			colors[i + 2].b = (BYTE)((unsigned(colors[0].b) * (2 - i) + unsigned(colors[1].b) * (1 + i)) / 3);
		}
	}
	else {
		// 3 color block, number 4 is transparent
		colors[2].a = 0xFF;
		colors[2].r = (BYTE)((unsigned(colors[0].r) + unsigned(colors[1].r)) / 2);
		colors[2].g = (BYTE)((unsigned(colors[0].g) + unsigned(colors[1].g)) / 2);
		colors[2].b = (BYTE)((unsigned(colors[0].b) + unsigned(colors[1].b)) / 2);

		colors[3].a = 0x00;
		colors[3].g = 0x00;
		colors[3].b = 0x00;
		colors[3].r = 0x00;
	}
}

typedef struct DXT_INFO_1 {
	typedef DXT1Block Block;
	enum {
		isDXT1 = 1,
		bytesPerBlock = 8
	};
} DXT_INFO_1;

typedef struct DXT_INFO_3 {
	typedef DXT3Block Block;
	enum {
		isDXT1 = 1,
		bytesPerBlock = 16
	};
} DXT_INFO_3;

typedef struct DXT_INFO_5 {
	typedef DXT5Block Block;
	enum {
		isDXT1 = 1,
		bytesPerBlock = 16
	};
} DXT_INFO_5;

/**
Base decoder
*/
template <class DXT_INFO> class DXT_BLOCKDECODER_BASE {
protected:
	Color8888 m_colors[4];
	const typename DXT_INFO::Block *m_pBlock;
	unsigned m_colorRow;

public:
	void Setup(const BYTE *pBlock) {
		// get a pointer to the block
		m_pBlock = (const typename DXT_INFO::Block *)pBlock;

		// get the 4 possible colors for a block
		GetBlockColors(&m_pBlock->color, m_colors, DXT_INFO::isDXT1);
	}

	/**
	Update y scanline
	*/
	void SetY(const int y) {
		m_colorRow = m_pBlock->color.row[y];
	}

	/**
	Get Color at (x, y) where y is set by SetY
	@see SetY
	*/
	void GetColor(const int x, Color8888 *color) {
		unsigned bits = (m_colorRow >> (x * 2)) & 3;
		memcpy(color, &m_colors[bits], sizeof(Color8888));
	}

};

class DXT_BLOCKDECODER_1 : public DXT_BLOCKDECODER_BASE<DXT_INFO_1> {
public:
	typedef DXT_INFO_1 INFO;
};

class DXT_BLOCKDECODER_3 : public DXT_BLOCKDECODER_BASE<DXT_INFO_3> {
public:
	typedef DXT_BLOCKDECODER_BASE<DXT_INFO_3> base;
	typedef DXT_INFO_3 INFO;

protected:
	unsigned m_alphaRow;

public:
	void SetY(int y) {
		base::SetY(y);
		m_alphaRow = m_pBlock->alpha.row[y];
	}

	/**
	Get the color at (x, y) where y is set by SetY
	@see SetY
	*/
	void GetColor(int x, Color8888 *color) {
		base::GetColor(x, color);
		const unsigned bits = (m_alphaRow >> (x * 4)) & 0xF;
		color->a = (BYTE)((bits * 0xFF) / 0xF);
	}
};

class DXT_BLOCKDECODER_5 : public DXT_BLOCKDECODER_BASE<DXT_INFO_5> {
public:
	typedef DXT_BLOCKDECODER_BASE<DXT_INFO_5> base;
	typedef DXT_INFO_5 INFO;

protected:
	unsigned m_alphas[8];
	unsigned m_alphaBits;
	int m_offset;

public:
	void Setup (const BYTE *pBlock) {
		base::Setup (pBlock);

		const DXTAlphaBlock3BitLinear &block = m_pBlock->alpha;
		m_alphas[0] = block.alpha[0];
		m_alphas[1] = block.alpha[1];
		if (m_alphas[0] > m_alphas[1]) {
			// 8 alpha block
			for (int i = 0; i < 6; i++) {
				m_alphas[i + 2] = ((6 - i) * m_alphas[0] + (1 + i) * m_alphas[1] + 3) / 7;
			}
		}
		else {
			// 6 alpha block
			for (int i = 0; i < 4; i++) {
				m_alphas[i + 2] = ((4 - i) * m_alphas[0] + (1 + i) * m_alphas[1] + 2) / 5;
			}
			m_alphas[6] = 0;
			m_alphas[7] = 0xFF;
		}
	}

	void SetY(const int y) {
		base::SetY(y);
		const int i = y / 2;
		const DXTAlphaBlock3BitLinear &block = m_pBlock->alpha;
		const BYTE *data = &block.data[i * 3];
		m_alphaBits = unsigned(data[0]) | (unsigned(data[1]) << 8) | (unsigned(data[2]) << 16);
		m_offset = (y & 1) * 12;
	}

	/**
	Get the color at (x, y) where y is set by SetY
	@see SetY
	*/
	void GetColor(int x, Color8888 *color) {
		base::GetColor(x, color);
		unsigned bits = (m_alphaBits >> (x * 3 + m_offset)) & 7;
		color->a = (BYTE)m_alphas[bits];
	}
};

template <class DECODER> void DecodeDXTBlock (BYTE *dstData, const BYTE *srcBlock, long dstPitch, int bw, int bh) {
	DECODER decoder;
	decoder.Setup(srcBlock);
	for (int y = 0; y < bh; y++) {
		BYTE *dst = dstData - y * dstPitch;
		// update scanline
		decoder.SetY(y);
		for (int x = 0; x < bw; x++) {
			// GetColor(x, y, dst)
			Color8888 *color = (Color8888*)dst;
			decoder.GetColor(x, color);

#if FREEIMAGE_COLORORDER == FREEIMAGE_COLORORDER_RGB 
			INPLACESWAP(dst[FI_RGBA_RED], dst[FI_RGBA_BLUE]);
#endif 
			dst += 4;
		}
	}
}

// ==========================================================
// Plugin Interface
// ==========================================================

static int s_format_id;

// ==========================================================
// Internal functions
// ==========================================================

/**
@param desc DDS_HEADER structure
@param io FreeImage IO
@param handle FreeImage handle
*/
static FIBITMAP *
LoadRGB(const DDSURFACEDESC2 *desc, FreeImageIO *io, fi_handle handle) {
	FIBITMAP *dib = NULL;
	DDSFormat16 format16 = RGB_UNKNOWN;	// for 16-bit formats

	const DDPIXELFORMAT *ddspf = &(desc->ddspf);

	// it is perfectly valid for an uncompressed DDS file to have a width or height which is not a multiple of 4
	// (only the packed image formats need to be a multiple of 4)
	const int width = (int)desc->dwWidth;
	const int height = (int)desc->dwHeight;

	// check the bitdepth, then allocate a new dib
	const int bpp = (int)ddspf->dwRGBBitCount;
	if (bpp == 16) {
		// get the 16-bit format
		format16 = GetRGB16Format(ddspf->dwRBitMask, ddspf->dwGBitMask, ddspf->dwBBitMask);
		// allocate a 24-bit dib, conversion from 16- to 24-bit will be done later
		dib = FreeImage_Allocate(width, height, 24);
	}
	else {
		dib = FreeImage_Allocate(width, height, bpp, ddspf->dwRBitMask, ddspf->dwGBitMask, ddspf->dwBBitMask);
	}
	if (dib == NULL) {
		return NULL;
	}

	// read the file
	// -------------------------------------------------------------------------

	const int line = CalculateLine(width, bpp);
	const int filePitch = ((desc->dwFlags & DDSD_PITCH) == DDSD_PITCH) ? (int)desc->dwPitchOrLinearSize : line;
	const long delta = (long)filePitch - (long)line;

	if (bpp == 16) {
		BYTE *pixels = (BYTE*)malloc(line * sizeof(BYTE));
		if (pixels) {
			for (int y = 0; y < height; y++) {
				BYTE *dst_bits = FreeImage_GetScanLine(dib, height - y - 1);
				// get the 16-bit RGB pixels
				io->read_proc(pixels, 1, line, handle);
				io->seek_proc(handle, delta, SEEK_CUR);
				// convert to 24-bit
				ConvertLine16To24(dst_bits, (const WORD*)pixels, format16, width);
			}
		}
		free(pixels);
	}
	else {
		for (int y = 0; y < height; y++) {
			BYTE *pixels = FreeImage_GetScanLine(dib, height - y - 1);
			io->read_proc(pixels, 1, line, handle);
			io->seek_proc(handle, delta, SEEK_CUR);
		}
	}

#if FREEIMAGE_COLORORDER == FREEIMAGE_COLORORDER_RGB
	// Calculate the number of bytes per pixel (3 for 24-bit or 4 for 32-bit)
	const int bytespp = FreeImage_GetLine(dib) / width;

	for (int y = 0; y < height; y++) {
		BYTE *pixels = FreeImage_GetScanLine(dib, y);
		for (int x = 0; x < width; x++) {
			INPLACESWAP(pixels[FI_RGBA_RED], pixels[FI_RGBA_BLUE]);
			pixels += bytespp;
		}
	}
#endif
	
	// enable transparency
	BOOL bIsTransparent = (bpp != 16) && ((ddspf->dwFlags & DDPF_ALPHAPIXELS) == DDPF_ALPHAPIXELS) ? TRUE : FALSE;
	FreeImage_SetTransparent(dib, bIsTransparent);

	if (!bIsTransparent && bpp == 32) {
		// no transparency: convert to 24-bit
		FIBITMAP *old = dib;
		dib = FreeImage_ConvertTo24Bits(old);
		FreeImage_Unload(old);
	}

	return dib;
}

/**
@param io FreeImage IO
@param handle FreeImage handle
@param dib Returned dib (already allocated)
@param width Image width
@param height Image height
*/
template <class DECODER> static void 
LoadDXT_Helper(FreeImageIO *io, fi_handle handle, FIBITMAP *dib, int width, int height) {
	typedef typename DECODER::INFO INFO;
	typedef typename INFO::Block Block;

	// get the size of a line in bytes
	int line = CalculateLine(width, FreeImage_GetBPP(dib));

	Block *input_buffer = new(std::nothrow) Block[(width + 3) / 4];
	if (!input_buffer) {
		return;
	}

	const int widthRest = (int) width & 3;
	const int heightRest = (int) height & 3;
	const int inputLine = (width + 3) / 4;
	int y = 0;

	if (height >= 4) {
		for (; y < height; y += 4) {
			io->read_proc (input_buffer, sizeof(typename INFO::Block), inputLine, handle);
			// TODO: probably need some endian work here
			const BYTE *pbSrc = (BYTE *)input_buffer;
			BYTE *pbDst = FreeImage_GetScanLine (dib, height - y - 1);

			if (width >= 4) {
				for (int x = 0; x < width; x += 4) {
					DecodeDXTBlock<DECODER>(pbDst, pbSrc, line, 4, 4);
					pbSrc += INFO::bytesPerBlock;
					pbDst += 16;	// 4 * 4;
				}
			}
			if (widthRest) {
				DecodeDXTBlock<DECODER>(pbDst, pbSrc, line, widthRest, 4);
			}
		}
	}
	if (heightRest)	{
		io->read_proc (input_buffer, sizeof (typename INFO::Block), inputLine, handle);
		// TODO: probably need some endian work here
		const BYTE *pbSrc = (BYTE *)input_buffer;
		BYTE *pbDst = FreeImage_GetScanLine (dib, height - y - 1);

		if (width >= 4) {
			for (int x = 0; x < width; x += 4) {
				DecodeDXTBlock<DECODER>(pbDst, pbSrc, line, 4, heightRest);
				pbSrc += INFO::bytesPerBlock;
				pbDst += 16;	// 4 * 4;
			}
		}
		if (widthRest) {
			DecodeDXTBlock<DECODER>(pbDst, pbSrc, line, widthRest, heightRest);
		}

	}

	delete [] input_buffer;
}

/**
@param decoder_type Decoder to be used, either 1 (DXT1), 3 (DXT3) or 5 (DXT5)
@param desc DDS_HEADER structure
@param io FreeImage IO
@param handle FreeImage handle
*/
static FIBITMAP *
LoadDXT(int decoder_type, const DDSURFACEDESC2 *desc, FreeImageIO *io, fi_handle handle) {
	// get image size, rounded to 32-bit
	int width = (int)desc->dwWidth & ~3;
	int height = (int)desc->dwHeight & ~3;

	// allocate a 32-bit dib
	FIBITMAP *dib = FreeImage_Allocate(width, height, 32, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
	if (dib == NULL) {
		return NULL;
	}

	// select the right decoder, then decode the image
	switch (decoder_type) {
		case 1:
			LoadDXT_Helper<DXT_BLOCKDECODER_1>(io, handle, dib, width, height);
			break;
		case 3:
			LoadDXT_Helper<DXT_BLOCKDECODER_3>(io, handle, dib, width, height);
			break;
		case 5:
			LoadDXT_Helper<DXT_BLOCKDECODER_5>(io, handle, dib, width, height);
			break;
		default:
			break;
	}
	
	return dib;
}
// ==========================================================
// Plugin Implementation
// ==========================================================

static const char * DLL_CALLCONV
Format() {
	return "DDS";
}

static const char * DLL_CALLCONV
Description() {
	return "DirectX Surface";
}

static const char * DLL_CALLCONV
Extension() {
	return "dds";
}

static const char * DLL_CALLCONV
RegExpr() {
	return NULL;
}

static const char * DLL_CALLCONV
MimeType() {
	return "image/x-dds";
}

static BOOL DLL_CALLCONV
Validate(FreeImageIO *io, fi_handle handle) {
	DDSHEADER header;
	memset(&header, 0, sizeof(header));
	io->read_proc(&header, 1, sizeof(header), handle);
#ifdef FREEIMAGE_BIGENDIAN
	SwapHeader(&header);
#endif
	if (header.dwMagic != MAKEFOURCC('D', 'D', 'S', ' ')) {
		return FALSE;
	}
	if (header.surfaceDesc.dwSize != sizeof(header.surfaceDesc) || header.surfaceDesc.ddspf.dwSize != sizeof(header.surfaceDesc.ddspf)) {
		return FALSE;
	}
	return TRUE;
}

static BOOL DLL_CALLCONV
SupportsExportDepth(int depth) {
	return FALSE;
}

static BOOL DLL_CALLCONV 
SupportsExportType(FREE_IMAGE_TYPE type) {
	return FALSE;
}

// ----------------------------------------------------------

static void * DLL_CALLCONV
Open(FreeImageIO *io, fi_handle handle, BOOL read) {
	return NULL;
}

static void DLL_CALLCONV
Close(FreeImageIO *io, fi_handle handle, void *data) {
}

// ----------------------------------------------------------

static FIBITMAP * DLL_CALLCONV
Load(FreeImageIO *io, fi_handle handle, int page, int flags, void *data) {
	DDSHEADER header;
	FIBITMAP *dib = NULL;

	memset(&header, 0, sizeof(header));
	io->read_proc(&header, 1, sizeof(header), handle);
#ifdef FREEIMAGE_BIGENDIAN
	SwapHeader(&header);
#endif
	
	// values which indicate what type of data is in the surface, see DDPF_*
	const DWORD dwFlags = header.surfaceDesc.ddspf.dwFlags;

	const DDSURFACEDESC2 *surfaceDesc = &(header.surfaceDesc);

	if ((dwFlags & DDPF_RGB) == DDPF_RGB) {
		// uncompressed data
		dib = LoadRGB(surfaceDesc, io, handle);
	}
	else if ((dwFlags & DDPF_FOURCC) == DDPF_FOURCC) {
		// compressed data
		switch (surfaceDesc->ddspf.dwFourCC) {
			case FOURCC_DXT1:
				dib = LoadDXT(1, surfaceDesc, io, handle);
				break;
			case FOURCC_DXT3:
				dib = LoadDXT(3, surfaceDesc, io, handle);
				break;
			case FOURCC_DXT5:
				dib = LoadDXT(5, surfaceDesc, io, handle);
				break;
		}
	}

	return dib;
}

/*
static BOOL DLL_CALLCONV
Save(FreeImageIO *io, FIBITMAP *dib, fi_handle handle, int page, int flags, void *data) {
	return FALSE;
}
*/

// ==========================================================
//   Init
// ==========================================================

void DLL_CALLCONV
InitDDS(Plugin *plugin, int format_id) {
	s_format_id = format_id;

	plugin->format_proc = Format;
	plugin->description_proc = Description;
	plugin->extension_proc = Extension;
	plugin->regexpr_proc = RegExpr;
	plugin->open_proc = Open;
	plugin->close_proc = Close;
	plugin->pagecount_proc = NULL;
	plugin->pagecapability_proc = NULL;
	plugin->load_proc = Load;
	plugin->save_proc = NULL;	//Save;	// not implemented (yet?)
	plugin->validate_proc = Validate;
	plugin->mime_proc = MimeType;
	plugin->supports_export_bpp_proc = SupportsExportDepth;
	plugin->supports_export_type_proc = SupportsExportType;
	plugin->supports_icc_profiles_proc = NULL;
}
