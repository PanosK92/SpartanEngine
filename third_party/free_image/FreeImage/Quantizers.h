// =============================================================
// Quantizer objects and functions
//
// Design and implementation by:
// - Hervé Drolon <drolon@infonie.fr>
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
// =============================================================

#ifndef FREEIMAGE_QUANTIZER_H
#define FREEIMAGE_QUANTIZER_H

// 
////////////////////////////////////////////////////////////////

#include "FreeImage.h"

////////////////////////////////////////////////////////////////

/**
  Xiaolin Wu color quantization algorithm
*/
class WuQuantizer
{
public:

typedef struct tagBox {
    int r0;			 // min value, exclusive
    int r1;			 // max value, inclusive
    int g0;  
    int g1;  
    int b0;  
    int b1;
    int vol;
} Box;

protected:
    float *gm2;
	LONG *wt, *mr, *mg, *mb;
	WORD *Qadd;

	// DIB data
	unsigned width, height;
	unsigned pitch;
	FIBITMAP *m_dib;

protected:
    void Hist3D(LONG *vwt, LONG *vmr, LONG *vmg, LONG *vmb, float *m2, int ReserveSize, RGBQUAD *ReservePalette);
	void M3D(LONG *vwt, LONG *vmr, LONG *vmg, LONG *vmb, float *m2);
	LONG Vol(Box *cube, LONG *mmt);
	LONG Bottom(Box *cube, BYTE dir, LONG *mmt);
	LONG Top(Box *cube, BYTE dir, int pos, LONG *mmt);
	float Var(Box *cube);
	float Maximize(Box *cube, BYTE dir, int first, int last , int *cut,
				   LONG whole_r, LONG whole_g, LONG whole_b, LONG whole_w);
	bool Cut(Box *set1, Box *set2);
	void Mark(Box *cube, int label, BYTE *tag);

public:
	// Constructor - Input parameter: DIB 24-bit to be quantized
    WuQuantizer(FIBITMAP *dib);
	// Destructor
	~WuQuantizer();
	// Quantizer - Return value: quantized 8-bit (color palette) DIB
	FIBITMAP* Quantize(int PaletteSize, int ReserveSize, RGBQUAD *ReservePalette);
};


/**
  NEUQUANT Neural-Net quantization algorithm by Anthony Dekker
*/

// ----------------------------------------------------------------
// Constant definitions
// ----------------------------------------------------------------

/** number of colours used: 
	for 256 colours, fixed arrays need 8kb, plus space for the image
*/
//static const int netsize = 256;

/**@name network definitions */
//@{
//static const int maxnetpos = (netsize - 1);
/// bias for colour values
static const int netbiasshift = 4;
/// no. of learning cycles
static const int ncycles = 100;
//@}

/**@name defs for freq and bias */
//@{
/// bias for fractions
static const int intbiasshift = 16;
static const int intbias = (((int)1) << intbiasshift);
/// gamma = 1024
static const int gammashift = 10;
// static const int gamma = (((int)1) << gammashift);
/// beta = 1 / 1024
static const int betashift = 10;
static const int beta = (intbias >> betashift);
static const int betagamma = (intbias << (gammashift-betashift));
//@}

/**@name defs for decreasing radius factor */
//@{
/// for 256 cols, radius starts
//static const int initrad = (netsize >> 3);
/// at 32.0 biased by 6 bits
static const int radiusbiasshift = 6;
static const int radiusbias = (((int)1) << radiusbiasshift);
/// and decreases by a 
//static const int initradius	= (initrad * radiusbias);
// factor of 1/30 each cycle
static const int radiusdec = 30;
//@}

/**@name defs for decreasing alpha factor */
//@{
/// alpha starts at 1.0
static const int alphabiasshift = 10;
static const int initalpha = (((int)1) << alphabiasshift);
//@}

/**@name radbias and alpharadbias used for radpower calculation */
//@{
static const int radbiasshift = 8;
static const int radbias = (((int)1) << radbiasshift);
static const int alpharadbshift = (alphabiasshift+radbiasshift);
static const int alpharadbias = (((int)1) << alpharadbshift);	
//@}

class NNQuantizer
{
protected:
	/**@name image parameters */
	//@{
	/// pointer to input dib
	FIBITMAP *dib_ptr;
	/// image width
	int img_width;
	/// image height
	int img_height;
	/// image line length
	int img_line;
	//@}

	/**@name network parameters */
	//@{

	int netsize, maxnetpos, initrad, initradius;

	/// BGRc
	typedef int pixel[4];
	/// the network itself
	pixel *network;

	/// for network lookup - really 256
	int netindex[256];

	/// bias array for learning
	int *bias;
	/// freq array for learning
	int *freq;
	/// radpower for precomputation
	int *radpower;
	//@}

protected:
	/// Initialise network in range (0,0,0) to (255,255,255) and set parameters
	void initnet();	

	/// Unbias network to give byte values 0..255 and record position i to prepare for sort
	void unbiasnet();

	/// Insertion sort of network and building of netindex[0..255] (to do after unbias)
	void inxbuild();

	/// Search for BGR values 0..255 (after net is unbiased) and return colour index
	int inxsearch(int b, int g, int r);

	/// Search for biased BGR values
	int contest(int b, int g, int r);
	
	/// Move neuron i towards biased (b,g,r) by factor alpha
	void altersingle(int alpha, int i, int b, int g, int r);

	/// Move adjacent neurons by precomputed alpha*(1-((i-j)^2/[r]^2)) in radpower[|i-j|]
	void alterneigh(int rad, int i, int b, int g, int r);

	/** Main Learning Loop
	@param sampling_factor sampling factor in [1..30]
	*/
	void learn(int sampling_factor);

	/// Get a pixel sample at position pos. Handle 4-byte boundary alignment.
	void getSample(long pos, int *b, int *g, int *r);


public:
	/// Constructor
	NNQuantizer(int PaletteSize);

	/// Destructor
	~NNQuantizer();

	/** Quantizer
	@param dib input 24-bit dib to be quantized
	@param sampling a sampling factor in range 1..30. 
	1 => slower (but better), 30 => faster. Default value is 1
	@return returns the quantized 8-bit (color palette) DIB
	*/
	FIBITMAP* Quantize(FIBITMAP *dib, int ReserveSize, RGBQUAD *ReservePalette, int sampling = 1);

};

/**
 * LFPQUANT - Lossless Fast Pseudo-Quantization Algorithm
 *
 * The Lossless Fast Pseudo-Quantization algorithm is no real quantization
 * algorithm, since it makes no attempt to create a palette, that is suitable
 * for all colors of the 24-bit source image. However, it provides very fast
 * conversions from 24-bit to 8-bit images, if the number of distinct colors
 * in the source image is not greater than the desired palette size. If the
 * number of colors in the source image is exceeded, the Quantize method of
 * this implementation stops the process and returns NULL.
 *
 * This implementation uses a very fast hash map implementation to collect
 * the source image's colors. It turned out that a customized implementation
 * of a hash table with open addressing (using linear probing) provides the
 * best performance. The hash table has 512 entries, which prevents the load
 * factor to exceed 0.5 as we have 256 entries at most. Each entry consumes
 * 64 bits, so the whole hash table takes 4KB of memory.
 *
 * For large images, the LFPQuantizer is typically up to three times faster
 * than the WuQuantizer.
 */
class LFPQuantizer {
public:
	/** Constructor */
	LFPQuantizer(unsigned PaletteSize);

	/** Destructor */
	~LFPQuantizer();

	/**
	 * Quantizer
	 * @param dib input 24-bit or 32-bit bitmap to be quantized
	 * @return returns the pseudo-quantized 8-bit bitmap
	 */
	FIBITMAP* Quantize(FIBITMAP *dib, int ReserveSize, RGBQUAD *ReservePalette);

protected:
	/** The maximum size of a palette. */
	static const unsigned MAX_SIZE = 256;

	/**
	 * The size of the hash table. Must be a power of 2. By sizing it
	 * MAX_SIZE * 2, we ensure the load factor not to exceed 0.5 at any
	 * time, since we will have MAX_SIZE entries at most.
	 */
	static const unsigned MAP_SIZE = MAX_SIZE * 2;

	/**
	 * With open addressing we need a special value for empty buckets.
	 * Both entry.color and entry.index are 0xFFFFFFFF for an empty
	 * entry.
	 */
	static const unsigned EMPTY_BUCKET = 0xFFFFFFFF;

	/**
	 * This structure defines a single entry in the hash table. We use
	 * color as the entry's key.
	 */
	typedef struct MapEntry {
		unsigned color;
		unsigned index;
	} MapEntry;

	/** The hash table. */
	MapEntry *m_map;

	/**
	 * The current size of the newly created palette. Since the provided
	 * reserve palette could contain duplicates, this is not necessarily
	 * the number of entries in the hash table. Initialized to zero.
	 */
	unsigned m_size;

	/**
	 * The desired maximum number of entries in the newly created palette.
	 * If m_size exceeds this value, the palette is full and the
	 * quantization process is stopped. Initialized to the desired
	 * palette size.
	 */
	unsigned m_limit;

	/**
	 *  The palette index used for the next color added. Initialized to
	 *  zero (the reserve palette is put to the end of the palette).
	 */
	unsigned m_index;

	/**
	 * Ensures that hash codes that differ only by constant multiples
	 * at each bit position have a bounded number of collisions.
	 * @param h the initial (aka raw) hash code
	 * @return the modified hash code
	 */
	static inline unsigned hash(unsigned h) {
		h ^= (h >> 20) ^ (h >> 12);
		return h ^ (h >> 7) ^ (h >> 4);
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
	int GetIndexForColor(unsigned color);

	/**
	 * Adds the specified number of entries of the specified reserve
	 * palette to the newly created palette.
	 * @param *palette a pointer to the reserve palette to copy from
	 * @param size the number of entries to copy
	 */
	void AddReservePalette(const void *palette, unsigned size);

	/**
	 * Copies the newly created palette into the specified destination
	 * palettte. Although unused palette entries are not overwritten in
	 * the destination palette, it is assumed to have space for at
	 * least 256 entries.
	 * @param palette a pointer to the destination palette
	 */
	void WritePalette(void *palette);

};

#endif // FREEIMAGE_QUANTIZER_H
