// Copyright 2012 Google Inc. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the COPYING file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS. All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.
// -----------------------------------------------------------------------------
//
// main entry for the lossless encoder.
//
// Author: Vikas Arora (vikaas.arora@gmail.com)
//

#include <assert.h>
#include <stdlib.h>

#include "./backward_references.h"
#include "./vp8enci.h"
#include "./vp8li.h"
#include "../dsp/lossless.h"
#include "../utils/bit_writer.h"
#include "../utils/huffman_encode.h"
#include "../utils/utils.h"
#include "../webp/format_constants.h"

#define PALETTE_KEY_RIGHT_SHIFT   22  // Key for 1K buffer.
// Maximum number of histogram images (sub-blocks).
#define MAX_HUFF_IMAGE_SIZE       2600

#define OPTIMIZE_MIN_NUM_COLORS 8

// -----------------------------------------------------------------------------
// Palette optimization

static int CompareColors(const void* p1, const void* p2) {
  const uint32_t a = *(const uint32_t*)p1;
  const uint32_t b = *(const uint32_t*)p2;
  assert(a != b);
  return (a < b) ? -1 : 1;
}
static WEBP_INLINE int Distance(int a, int b) {
  return abs(a - b);
}

static int ColorDistance(uint32_t col1, uint32_t col2) {
  int score = 0;
  // we favor grouping green channel in the palette
  score += Distance((col1 >>  0) & 0xff, (col2 >>  0) & 0xff) * 5;
  score += Distance((col1 >>  8) & 0xff, (col2 >>  8) & 0xff) * 8;
  score += Distance((col1 >> 16) & 0xff, (col2 >> 16) & 0xff) * 5;
  score += Distance((col1 >> 24) & 0xff, (col2 >> 24) & 0xff) * 1;
  return score;
}

static void SwapColor(uint32_t* const col1, uint32_t* const col2) {
  if (col1 != col2) {
    const uint32_t tmp = *col1;
    *col1 = *col2;
    *col2 = tmp;
  }
}

static int ShouldRestoreSortedPalette(int score_new, int score_orig) {
  if ((score_orig > 200) && (score_new + 100 > score_orig)) {
    return 1;  // improvement not big enough
  }
  // if drop is less 20%, it's not enough
  if ((score_new + 100) > (score_orig + 100) * 80 / 100) {
    return 1;
  }
  if (score_orig > 500) {  // if original palette was dispersed and...
                           // improvement is not clear?
    if (score_new > 300) return 1;
  }
  return 0;  // keep the new one
}

static void OptimizePalette(uint32_t palette[], int num_colors) {
  uint32_t palette_orig[MAX_PALETTE_SIZE];
  int score_orig = 0, score_new = 0;
  int i;

  // Compute original dispersion.
  assert(num_colors > 1 && num_colors <= MAX_PALETTE_SIZE);
  for (i = 1; i < num_colors; ++i) {
    score_orig += ColorDistance(palette[i], palette[i - 1]);
  }
  score_orig /= (num_colors - 1);
  // if score is already quite good, bail out at once.
  if (score_orig < 100) return;

  memcpy(palette_orig, palette, num_colors * sizeof(palette_orig[0]));

  // palette[0] contains the lowest ordered color already. Keep it.
  // Reorder subsequent palette colors by shortest distance to previous.
  for (i = 1; i < num_colors; ++i) {
    int j;
    int best_col = -1;
    int best_score = 0;
    const uint32_t prev_color = palette[i - 1];
    for (j = i; j < num_colors; ++j) {
      const int score = ColorDistance(palette[j], prev_color);
      if (best_col < 0 || score < best_score) {
        best_col = j;
        best_score = score;
      }
    }
    score_new += best_score;
    SwapColor(&palette[best_col], &palette[i]);
  }
  // dispersion is typically in range ~[100-1000]
  score_new /= (num_colors - 1);

  if (ShouldRestoreSortedPalette(score_new, score_orig)) {
    memcpy(palette, palette_orig, num_colors * sizeof(palette[0]));
  }
}

// -----------------------------------------------------------------------------
// Palette

// If number of colors in the image is less than or equal to MAX_PALETTE_SIZE,
// creates a palette and returns true, else returns false.
static int AnalyzeAndCreatePalette(const WebPPicture* const pic,
                                   uint32_t palette[MAX_PALETTE_SIZE],
                                   int* const palette_size) {
  int i, x, y, key;
  int num_colors = 0;
  uint8_t in_use[MAX_PALETTE_SIZE * 4] = { 0 };
  uint32_t colors[MAX_PALETTE_SIZE * 4];
  static const uint32_t kHashMul = 0x1e35a7bd;
  const uint32_t* argb = pic->argb;
  const int width = pic->width;
  const int height = pic->height;
  uint32_t all_color_bits;
  uint32_t last_pix = ~argb[0];   // so we're sure that last_pix != argb[0]

  for (y = 0; y < height; ++y) {
    for (x = 0; x < width; ++x) {
      if (argb[x] == last_pix) {
        continue;
      }
      last_pix = argb[x];
      key = (kHashMul * last_pix) >> PALETTE_KEY_RIGHT_SHIFT;
      while (1) {
        if (!in_use[key]) {
          colors[key] = last_pix;
          in_use[key] = 1;
          ++num_colors;
          if (num_colors > MAX_PALETTE_SIZE) {
            return 0;
          }
          break;
        } else if (colors[key] == last_pix) {
          // The color is already there.
          break;
        } else {
          // Some other color sits there.
          // Do linear conflict resolution.
          ++key;
          key &= (MAX_PALETTE_SIZE * 4 - 1);  // key mask for 1K buffer.
        }
      }
    }
    argb += pic->argb_stride;
  }

  // TODO(skal): could we reuse in_use[] to speed up EncodePalette()?
  num_colors = 0;
  all_color_bits = 0x00000000;
  for (i = 0; i < (int)(sizeof(in_use) / sizeof(in_use[0])); ++i) {
    if (in_use[i]) {
      palette[num_colors] = colors[i];
      all_color_bits |= colors[i];
      ++num_colors;
    }
  }

  *palette_size = num_colors;
  qsort(palette, num_colors, sizeof(*palette), CompareColors);
  // OptimizePalette() is not useful for single-channel (like alpha, e.g.).
  if (num_colors > OPTIMIZE_MIN_NUM_COLORS &&
      (all_color_bits & ~0x000000ffu) != 0 &&   // all red?
      (all_color_bits & ~0x0000ff00u) != 0 &&   // all green/alpha?
      (all_color_bits & ~0x00ff0000u) != 0) {   // all blue?
    OptimizePalette(palette, num_colors);
  }
  return 1;
}

static int AnalyzeEntropy(const uint32_t* argb,
                          int width, int height, int argb_stride,
                          double* const nonpredicted_bits,
                          double* const predicted_bits) {
  // Allocate histogram set with cache_bits = 0.
  VP8LHistogramSet* const histo_set = VP8LAllocateHistogramSet(2, 0);
  assert(nonpredicted_bits != NULL);
  assert(predicted_bits != NULL);

  if (histo_set != NULL) {
    int x, y;
    const uint32_t* prev_row = argb;
    const uint32_t* curr_row = argb + argb_stride;
    VP8LHistogram* const histo_non_pred = histo_set->histograms[0];
    VP8LHistogram* const histo_pred = histo_set->histograms[1];
    for (y = 1; y < height; ++y) {
      uint32_t prev_pix = curr_row[0];
      for (x = 1; x < width; ++x) {
        const uint32_t pix = curr_row[x];
        const uint32_t pix_diff = VP8LSubPixels(pix, prev_pix);
        if ((pix_diff == 0) || (pix == prev_row[x])) continue;
        prev_pix = pix;
        {
          const PixOrCopy pix_token = PixOrCopyCreateLiteral(pix);
          const PixOrCopy pix_diff_token = PixOrCopyCreateLiteral(pix_diff);
          VP8LHistogramAddSinglePixOrCopy(histo_non_pred, &pix_token);
          VP8LHistogramAddSinglePixOrCopy(histo_pred, &pix_diff_token);
        }
      }
      prev_row = curr_row;
      curr_row += argb_stride;
    }
    *nonpredicted_bits = VP8LHistogramEstimateBitsBulk(histo_non_pred);
    *predicted_bits = VP8LHistogramEstimateBitsBulk(histo_pred);
    VP8LFreeHistogramSet(histo_set);
    return 1;
  } else {
    return 0;
  }
}

// Check if it would be a good idea to subtract green from red and blue. We
// only evaluate entropy in red/blue components, don't bother to look at others.
static int AnalyzeSubtractGreen(const uint32_t* const argb,
                                int width, int height,
                                double* const entropy_change_ratio) {
  // Allocate histogram set with cache_bits = 1.
  VP8LHistogramSet* const histo_set = VP8LAllocateHistogramSet(2, 1);
  assert(entropy_change_ratio != NULL);

  if (histo_set != NULL) {
    int i;
    double bit_cost, bit_cost_subgreen;
    VP8LHistogram* const histo = histo_set->histograms[0];
    VP8LHistogram* const histo_subgreen = histo_set->histograms[1];
    for (i = 0; i < width * height; ++i) {
      const uint32_t c = argb[i];
      const int green = (c >> 8) & 0xff;
      const int red = (c >> 16) & 0xff;
      const int blue = (c >> 0) & 0xff;
      ++histo->red_[red];
      ++histo->blue_[blue];
      ++histo_subgreen->red_[(red - green) & 0xff];
      ++histo_subgreen->blue_[(blue - green) & 0xff];
    }
    bit_cost= VP8LHistogramEstimateBits(histo);
    bit_cost_subgreen = VP8LHistogramEstimateBits(histo_subgreen);
    VP8LFreeHistogramSet(histo_set);
    *entropy_change_ratio = bit_cost_subgreen / (bit_cost + 1e-6);
    return 1;
  } else {
    return 0;
  }
}

static int GetHistoBits(int method, int use_palette, int width, int height) {
  // Make tile size a function of encoding method (Range: 0 to 6).
  int histo_bits = (use_palette ? 9 : 7) - method;
  while (1) {
    const int huff_image_size = VP8LSubSampleSize(width, histo_bits) *
                                VP8LSubSampleSize(height, histo_bits);
    if (huff_image_size <= MAX_HUFF_IMAGE_SIZE) break;
    ++histo_bits;
  }
  return (histo_bits < MIN_HUFFMAN_BITS) ? MIN_HUFFMAN_BITS :
         (histo_bits > MAX_HUFFMAN_BITS) ? MAX_HUFFMAN_BITS : histo_bits;
}

static int GetTransformBits(int method, int histo_bits) {
  const int max_transform_bits = (method < 4) ? 6 : (method > 4) ? 4 : 5;
  return (histo_bits > max_transform_bits) ? max_transform_bits : histo_bits;
}

static int EvalSubtractGreenForPalette(int palette_size, float quality) {
  // Evaluate non-palette encoding (subtract green, prediction transforms etc)
  // for palette size in the mid-range (17-96) as for larger number of colors,
  // the benefit from switching to non-palette is not much.
  // Non-palette transforms are little CPU intensive, hence don't evaluate them
  // for lower (<= 25) quality.
  const int min_colors_non_palette = 17;
  const int max_colors_non_palette = 96;
  const float min_quality_non_palette = 26.f;
  return (palette_size >= min_colors_non_palette) &&
         (palette_size <= max_colors_non_palette) &&
         (quality >= min_quality_non_palette);
}

static int AnalyzeAndInit(VP8LEncoder* const enc, WebPImageHint image_hint) {
  const WebPPicture* const pic = enc->pic_;
  const int width = pic->width;
  const int height = pic->height;
  const int pix_cnt = width * height;
  const WebPConfig* const config = enc->config_;
  const int method = config->method;
  const int low_effort = (config->method == 0);
  const float quality = config->quality;
  double subtract_green_score = 10.0;
  const double subtract_green_threshold_palette = 0.80;
  const double subtract_green_threshold_non_palette = 1.0;
  // we round the block size up, so we're guaranteed to have
  // at max MAX_REFS_BLOCK_PER_IMAGE blocks used:
  int refs_block_size = (pix_cnt - 1) / MAX_REFS_BLOCK_PER_IMAGE + 1;
  assert(pic != NULL && pic->argb != NULL);

  enc->use_palette_ =
      AnalyzeAndCreatePalette(pic, enc->palette_, &enc->palette_size_);

  if (!enc->use_palette_ ||
      EvalSubtractGreenForPalette(enc->palette_size_, quality)) {
    if (low_effort) {
      // For low effort compression, avoid calling (costly) method
      // AnalyzeSubtractGreen and enable the subtract-green transform
      // for non-palette images.
      subtract_green_score = subtract_green_threshold_non_palette * 0.99;
    } else {
      if (!AnalyzeSubtractGreen(pic->argb, width, height,
                                &subtract_green_score)) {
        return 0;
      }
    }
  }

  // Evaluate histogram bits based on the original value of use_palette flag.
  enc->histo_bits_ = GetHistoBits(method, enc->use_palette_, pic->width,
                                  pic->height);
  enc->transform_bits_ = GetTransformBits(method, enc->histo_bits_);

  enc->use_subtract_green_ = 0;
  if (enc->use_palette_) {
    // Check if other transforms (subtract green etc) are potentially better.
    if (subtract_green_score < subtract_green_threshold_palette) {
      enc->use_subtract_green_ = 1;
      enc->use_palette_ = 0;
    }
  } else {
    // Non-palette case, check if subtract-green optimizes the entropy.
    if (subtract_green_score < subtract_green_threshold_non_palette) {
      enc->use_subtract_green_ = 1;
    }
  }

  if (!enc->use_palette_) {
    if (image_hint == WEBP_HINT_PHOTO) {
      enc->use_predict_ = 1;
      enc->use_cross_color_ = !low_effort;
    } else {
      double non_pred_entropy, pred_entropy;
      if (!AnalyzeEntropy(pic->argb, width, height, pic->argb_stride,
                          &non_pred_entropy, &pred_entropy)) {
        return 0;
      }
      if (pred_entropy < 0.95 * non_pred_entropy) {
        enc->use_predict_ = 1;
        enc->use_cross_color_ = !low_effort;
      }
    }
  }
  if (!VP8LHashChainInit(&enc->hash_chain_, pix_cnt)) return 0;

  // palette-friendly input typically uses less literals
  //  -> reduce block size a bit
  if (enc->use_palette_) refs_block_size /= 2;
  VP8LBackwardRefsInit(&enc->refs_[0], refs_block_size);
  VP8LBackwardRefsInit(&enc->refs_[1], refs_block_size);

  return 1;
}

// Returns false in case of memory error.
static int GetHuffBitLengthsAndCodes(
    const VP8LHistogramSet* const histogram_image,
    HuffmanTreeCode* const huffman_codes) {
  int i, k;
  int ok = 0;
  uint64_t total_length_size = 0;
  uint8_t* mem_buf = NULL;
  const int histogram_image_size = histogram_image->size;
  int max_num_symbols = 0;
  uint8_t* buf_rle = NULL;
  HuffmanTree* huff_tree = NULL;

  // Iterate over all histograms and get the aggregate number of codes used.
  for (i = 0; i < histogram_image_size; ++i) {
    const VP8LHistogram* const histo = histogram_image->histograms[i];
    HuffmanTreeCode* const codes = &huffman_codes[5 * i];
    for (k = 0; k < 5; ++k) {
      const int num_symbols =
          (k == 0) ? VP8LHistogramNumCodes(histo->palette_code_bits_) :
          (k == 4) ? NUM_DISTANCE_CODES : 256;
      codes[k].num_symbols = num_symbols;
      total_length_size += num_symbols;
    }
  }

  // Allocate and Set Huffman codes.
  {
    uint16_t* codes;
    uint8_t* lengths;
    mem_buf = (uint8_t*)WebPSafeCalloc(total_length_size,
                                       sizeof(*lengths) + sizeof(*codes));
    if (mem_buf == NULL) goto End;

    codes = (uint16_t*)mem_buf;
    lengths = (uint8_t*)&codes[total_length_size];
    for (i = 0; i < 5 * histogram_image_size; ++i) {
      const int bit_length = huffman_codes[i].num_symbols;
      huffman_codes[i].codes = codes;
      huffman_codes[i].code_lengths = lengths;
      codes += bit_length;
      lengths += bit_length;
      if (max_num_symbols < bit_length) {
        max_num_symbols = bit_length;
      }
    }
  }

  buf_rle = (uint8_t*)WebPSafeMalloc(1ULL, max_num_symbols);
  huff_tree = (HuffmanTree*)WebPSafeMalloc(3ULL * max_num_symbols,
                                           sizeof(*huff_tree));
  if (buf_rle == NULL || huff_tree == NULL) goto End;

  // Create Huffman trees.
  for (i = 0; i < histogram_image_size; ++i) {
    HuffmanTreeCode* const codes = &huffman_codes[5 * i];
    VP8LHistogram* const histo = histogram_image->histograms[i];
    VP8LCreateHuffmanTree(histo->literal_, 15, buf_rle, huff_tree, codes + 0);
    VP8LCreateHuffmanTree(histo->red_, 15, buf_rle, huff_tree, codes + 1);
    VP8LCreateHuffmanTree(histo->blue_, 15, buf_rle, huff_tree, codes + 2);
    VP8LCreateHuffmanTree(histo->alpha_, 15, buf_rle, huff_tree, codes + 3);
    VP8LCreateHuffmanTree(histo->distance_, 15, buf_rle, huff_tree, codes + 4);
  }
  ok = 1;
 End:
  WebPSafeFree(huff_tree);
  WebPSafeFree(buf_rle);
  if (!ok) {
    WebPSafeFree(mem_buf);
    memset(huffman_codes, 0, 5 * histogram_image_size * sizeof(*huffman_codes));
  }
  return ok;
}

static void StoreHuffmanTreeOfHuffmanTreeToBitMask(
    VP8LBitWriter* const bw, const uint8_t* code_length_bitdepth) {
  // RFC 1951 will calm you down if you are worried about this funny sequence.
  // This sequence is tuned from that, but more weighted for lower symbol count,
  // and more spiking histograms.
  static const uint8_t kStorageOrder[CODE_LENGTH_CODES] = {
    17, 18, 0, 1, 2, 3, 4, 5, 16, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
  };
  int i;
  // Throw away trailing zeros:
  int codes_to_store = CODE_LENGTH_CODES;
  for (; codes_to_store > 4; --codes_to_store) {
    if (code_length_bitdepth[kStorageOrder[codes_to_store - 1]] != 0) {
      break;
    }
  }
  VP8LPutBits(bw, codes_to_store - 4, 4);
  for (i = 0; i < codes_to_store; ++i) {
    VP8LPutBits(bw, code_length_bitdepth[kStorageOrder[i]], 3);
  }
}

static void ClearHuffmanTreeIfOnlyOneSymbol(
    HuffmanTreeCode* const huffman_code) {
  int k;
  int count = 0;
  for (k = 0; k < huffman_code->num_symbols; ++k) {
    if (huffman_code->code_lengths[k] != 0) {
      ++count;
      if (count > 1) return;
    }
  }
  for (k = 0; k < huffman_code->num_symbols; ++k) {
    huffman_code->code_lengths[k] = 0;
    huffman_code->codes[k] = 0;
  }
}

static void StoreHuffmanTreeToBitMask(
    VP8LBitWriter* const bw,
    const HuffmanTreeToken* const tokens, const int num_tokens,
    const HuffmanTreeCode* const huffman_code) {
  int i;
  for (i = 0; i < num_tokens; ++i) {
    const int ix = tokens[i].code;
    const int extra_bits = tokens[i].extra_bits;
    VP8LPutBits(bw, huffman_code->codes[ix], huffman_code->code_lengths[ix]);
    switch (ix) {
      case 16:
        VP8LPutBits(bw, extra_bits, 2);
        break;
      case 17:
        VP8LPutBits(bw, extra_bits, 3);
        break;
      case 18:
        VP8LPutBits(bw, extra_bits, 7);
        break;
    }
  }
}

// 'huff_tree' and 'tokens' are pre-alloacted buffers.
static void StoreFullHuffmanCode(VP8LBitWriter* const bw,
                                 HuffmanTree* const huff_tree,
                                 HuffmanTreeToken* const tokens,
                                 const HuffmanTreeCode* const tree) {
  uint8_t code_length_bitdepth[CODE_LENGTH_CODES] = { 0 };
  uint16_t code_length_bitdepth_symbols[CODE_LENGTH_CODES] = { 0 };
  const int max_tokens = tree->num_symbols;
  int num_tokens;
  HuffmanTreeCode huffman_code;
  huffman_code.num_symbols = CODE_LENGTH_CODES;
  huffman_code.code_lengths = code_length_bitdepth;
  huffman_code.codes = code_length_bitdepth_symbols;

  VP8LPutBits(bw, 0, 1);
  num_tokens = VP8LCreateCompressedHuffmanTree(tree, tokens, max_tokens);
  {
    uint32_t histogram[CODE_LENGTH_CODES] = { 0 };
    uint8_t buf_rle[CODE_LENGTH_CODES] = { 0 };
    int i;
    for (i = 0; i < num_tokens; ++i) {
      ++histogram[tokens[i].code];
    }

    VP8LCreateHuffmanTree(histogram, 7, buf_rle, huff_tree, &huffman_code);
  }

  StoreHuffmanTreeOfHuffmanTreeToBitMask(bw, code_length_bitdepth);
  ClearHuffmanTreeIfOnlyOneSymbol(&huffman_code);
  {
    int trailing_zero_bits = 0;
    int trimmed_length = num_tokens;
    int write_trimmed_length;
    int length;
    int i = num_tokens;
    while (i-- > 0) {
      const int ix = tokens[i].code;
      if (ix == 0 || ix == 17 || ix == 18) {
        --trimmed_length;   // discount trailing zeros
        trailing_zero_bits += code_length_bitdepth[ix];
        if (ix == 17) {
          trailing_zero_bits += 3;
        } else if (ix == 18) {
          trailing_zero_bits += 7;
        }
      } else {
        break;
      }
    }
    write_trimmed_length = (trimmed_length > 1 && trailing_zero_bits > 12);
    length = write_trimmed_length ? trimmed_length : num_tokens;
    VP8LPutBits(bw, write_trimmed_length, 1);
    if (write_trimmed_length) {
      const int nbits = VP8LBitsLog2Ceiling(trimmed_length - 1);
      const int nbitpairs = (nbits == 0) ? 1 : (nbits + 1) / 2;
      VP8LPutBits(bw, nbitpairs - 1, 3);
      assert(trimmed_length >= 2);
      VP8LPutBits(bw, trimmed_length - 2, nbitpairs * 2);
    }
    StoreHuffmanTreeToBitMask(bw, tokens, length, &huffman_code);
  }
}

// 'huff_tree' and 'tokens' are pre-alloacted buffers.
static void StoreHuffmanCode(VP8LBitWriter* const bw,
                             HuffmanTree* const huff_tree,
                             HuffmanTreeToken* const tokens,
                             const HuffmanTreeCode* const huffman_code) {
  int i;
  int count = 0;
  int symbols[2] = { 0, 0 };
  const int kMaxBits = 8;
  const int kMaxSymbol = 1 << kMaxBits;

  // Check whether it's a small tree.
  for (i = 0; i < huffman_code->num_symbols && count < 3; ++i) {
    if (huffman_code->code_lengths[i] != 0) {
      if (count < 2) symbols[count] = i;
      ++count;
    }
  }

  if (count == 0) {   // emit minimal tree for empty cases
    // bits: small tree marker: 1, count-1: 0, large 8-bit code: 0, code: 0
    VP8LPutBits(bw, 0x01, 4);
  } else if (count <= 2 && symbols[0] < kMaxSymbol && symbols[1] < kMaxSymbol) {
    VP8LPutBits(bw, 1, 1);  // Small tree marker to encode 1 or 2 symbols.
    VP8LPutBits(bw, count - 1, 1);
    if (symbols[0] <= 1) {
      VP8LPutBits(bw, 0, 1);  // Code bit for small (1 bit) symbol value.
      VP8LPutBits(bw, symbols[0], 1);
    } else {
      VP8LPutBits(bw, 1, 1);
      VP8LPutBits(bw, symbols[0], 8);
    }
    if (count == 2) {
      VP8LPutBits(bw, symbols[1], 8);
    }
  } else {
    StoreFullHuffmanCode(bw, huff_tree, tokens, huffman_code);
  }
}

static void WriteHuffmanCode(VP8LBitWriter* const bw,
                             const HuffmanTreeCode* const code,
                             int code_index) {
  const int depth = code->code_lengths[code_index];
  const int symbol = code->codes[code_index];
  VP8LPutBits(bw, symbol, depth);
}

static WebPEncodingError StoreImageToBitMask(
    VP8LBitWriter* const bw, int width, int histo_bits,
    VP8LBackwardRefs* const refs,
    const uint16_t* histogram_symbols,
    const HuffmanTreeCode* const huffman_codes) {
  const int histo_xsize = histo_bits ? VP8LSubSampleSize(width, histo_bits) : 1;
  const int tile_mask = (histo_bits == 0) ? 0 : -(1 << histo_bits);
  // x and y trace the position in the image.
  int x = 0;
  int y = 0;
  int tile_x = x & tile_mask;
  int tile_y = y & tile_mask;
  int histogram_ix = histogram_symbols[0];
  const HuffmanTreeCode* codes = huffman_codes + 5 * histogram_ix;
  VP8LRefsCursor c = VP8LRefsCursorInit(refs);
  while (VP8LRefsCursorOk(&c)) {
    const PixOrCopy* const v = c.cur_pos;
    if ((tile_x != (x & tile_mask)) || (tile_y != (y & tile_mask))) {
      tile_x = x & tile_mask;
      tile_y = y & tile_mask;
      histogram_ix = histogram_symbols[(y >> histo_bits) * histo_xsize +
                                       (x >> histo_bits)];
      codes = huffman_codes + 5 * histogram_ix;
    }
    if (PixOrCopyIsCacheIdx(v)) {
      const int code = PixOrCopyCacheIdx(v);
      const int literal_ix = 256 + NUM_LENGTH_CODES + code;
      WriteHuffmanCode(bw, codes, literal_ix);
    } else if (PixOrCopyIsLiteral(v)) {
      static const int order[] = { 1, 2, 0, 3 };
      int k;
      for (k = 0; k < 4; ++k) {
        const int code = PixOrCopyLiteral(v, order[k]);
        WriteHuffmanCode(bw, codes + k, code);
      }
    } else {
      int bits, n_bits;
      int code, distance;

      VP8LPrefixEncode(v->len, &code, &n_bits, &bits);
      WriteHuffmanCode(bw, codes, 256 + code);
      VP8LPutBits(bw, bits, n_bits);

      distance = PixOrCopyDistance(v);
      VP8LPrefixEncode(distance, &code, &n_bits, &bits);
      WriteHuffmanCode(bw, codes + 4, code);
      VP8LPutBits(bw, bits, n_bits);
    }
    x += PixOrCopyLength(v);
    while (x >= width) {
      x -= width;
      ++y;
    }
    VP8LRefsCursorNext(&c);
  }
  return bw->error_ ? VP8_ENC_ERROR_OUT_OF_MEMORY : VP8_ENC_OK;
}

// Special case of EncodeImageInternal() for cache-bits=0, histo_bits=31
static WebPEncodingError EncodeImageNoHuffman(VP8LBitWriter* const bw,
                                              const uint32_t* const argb,
                                              VP8LHashChain* const hash_chain,
                                              VP8LBackwardRefs refs_array[2],
                                              int width, int height,
                                              int quality) {
  int i;
  int max_tokens = 0;
  WebPEncodingError err = VP8_ENC_OK;
  VP8LBackwardRefs* refs;
  HuffmanTreeToken* tokens = NULL;
  HuffmanTreeCode huffman_codes[5] = { { 0, NULL, NULL } };
  const uint16_t histogram_symbols[1] = { 0 };    // only one tree, one symbol
  int cache_bits = 0;
  VP8LHistogramSet* histogram_image = NULL;
  HuffmanTree* const huff_tree = (HuffmanTree*)WebPSafeMalloc(
        3ULL * CODE_LENGTH_CODES, sizeof(*huff_tree));
  if (huff_tree == NULL) {
    err = VP8_ENC_ERROR_OUT_OF_MEMORY;
    goto Error;
  }

  // Calculate backward references from ARGB image.
  refs = VP8LGetBackwardReferences(width, height, argb, quality, 0, &cache_bits,
                                   hash_chain, refs_array);
  if (refs == NULL) {
    err = VP8_ENC_ERROR_OUT_OF_MEMORY;
    goto Error;
  }
  histogram_image = VP8LAllocateHistogramSet(1, cache_bits);
  if (histogram_image == NULL) {
    err = VP8_ENC_ERROR_OUT_OF_MEMORY;
    goto Error;
  }

  // Build histogram image and symbols from backward references.
  VP8LHistogramStoreRefs(refs, histogram_image->histograms[0]);

  // Create Huffman bit lengths and codes for each histogram image.
  assert(histogram_image->size == 1);
  if (!GetHuffBitLengthsAndCodes(histogram_image, huffman_codes)) {
    err = VP8_ENC_ERROR_OUT_OF_MEMORY;
    goto Error;
  }

  // No color cache, no Huffman image.
  VP8LPutBits(bw, 0, 1);

  // Find maximum number of symbols for the huffman tree-set.
  for (i = 0; i < 5; ++i) {
    HuffmanTreeCode* const codes = &huffman_codes[i];
    if (max_tokens < codes->num_symbols) {
      max_tokens = codes->num_symbols;
    }
  }

  tokens = (HuffmanTreeToken*)WebPSafeMalloc(max_tokens, sizeof(*tokens));
  if (tokens == NULL) {
    err = VP8_ENC_ERROR_OUT_OF_MEMORY;
    goto Error;
  }

  // Store Huffman codes.
  for (i = 0; i < 5; ++i) {
    HuffmanTreeCode* const codes = &huffman_codes[i];
    StoreHuffmanCode(bw, huff_tree, tokens, codes);
    ClearHuffmanTreeIfOnlyOneSymbol(codes);
  }

  // Store actual literals.
  err = StoreImageToBitMask(bw, width, 0, refs, histogram_symbols,
                            huffman_codes);

 Error:
  WebPSafeFree(tokens);
  WebPSafeFree(huff_tree);
  VP8LFreeHistogramSet(histogram_image);
  WebPSafeFree(huffman_codes[0].codes);
  return err;
}

static WebPEncodingError EncodeImageInternal(VP8LBitWriter* const bw,
                                             const uint32_t* const argb,
                                             VP8LHashChain* const hash_chain,
                                             VP8LBackwardRefs refs_array[2],
                                             int width, int height, int quality,
                                             int low_effort,
                                             int use_cache, int* cache_bits,
                                             int histogram_bits,
                                             size_t init_byte_position,
                                             int* const hdr_size,
                                             int* const data_size) {
  WebPEncodingError err = VP8_ENC_OK;
  const uint32_t histogram_image_xysize =
      VP8LSubSampleSize(width, histogram_bits) *
      VP8LSubSampleSize(height, histogram_bits);
  VP8LHistogramSet* histogram_image = NULL;
  VP8LHistogramSet* tmp_histos = NULL;
  int histogram_image_size = 0;
  size_t bit_array_size = 0;
  HuffmanTree* huff_tree = NULL;
  HuffmanTreeToken* tokens = NULL;
  HuffmanTreeCode* huffman_codes = NULL;
  VP8LBackwardRefs refs;
  VP8LBackwardRefs* best_refs;
  uint16_t* const histogram_symbols =
      (uint16_t*)WebPSafeMalloc(histogram_image_xysize,
                                sizeof(*histogram_symbols));
  assert(histogram_bits >= MIN_HUFFMAN_BITS);
  assert(histogram_bits <= MAX_HUFFMAN_BITS);
  assert(hdr_size != NULL);
  assert(data_size != NULL);

  VP8LBackwardRefsInit(&refs, refs_array[0].block_size_);
  if (histogram_symbols == NULL) {
    err = VP8_ENC_ERROR_OUT_OF_MEMORY;
    goto Error;
  }

  *cache_bits = use_cache ? MAX_COLOR_CACHE_BITS : 0;
  // 'best_refs' is the reference to the best backward refs and points to one
  // of refs_array[0] or refs_array[1].
  // Calculate backward references from ARGB image.
  best_refs = VP8LGetBackwardReferences(width, height, argb, quality,
                                        low_effort, cache_bits, hash_chain,
                                        refs_array);
  if (best_refs == NULL || !VP8LBackwardRefsCopy(best_refs, &refs)) {
    err = VP8_ENC_ERROR_OUT_OF_MEMORY;
    goto Error;
  }
  histogram_image =
      VP8LAllocateHistogramSet(histogram_image_xysize, *cache_bits);
  tmp_histos = VP8LAllocateHistogramSet(2, *cache_bits);
  if (histogram_image == NULL || tmp_histos == NULL) {
    err = VP8_ENC_ERROR_OUT_OF_MEMORY;
    goto Error;
  }

  // Build histogram image and symbols from backward references.
  if (!VP8LGetHistoImageSymbols(width, height, &refs, quality, low_effort,
                                histogram_bits, *cache_bits, histogram_image,
                                tmp_histos, histogram_symbols)) {
    err = VP8_ENC_ERROR_OUT_OF_MEMORY;
    goto Error;
  }
  // Create Huffman bit lengths and codes for each histogram image.
  histogram_image_size = histogram_image->size;
  bit_array_size = 5 * histogram_image_size;
  huffman_codes = (HuffmanTreeCode*)WebPSafeCalloc(bit_array_size,
                                                   sizeof(*huffman_codes));
  // Note: some histogram_image entries may point to tmp_histos[], so the latter
  // need to outlive the following call to GetHuffBitLengthsAndCodes().
  if (huffman_codes == NULL ||
      !GetHuffBitLengthsAndCodes(histogram_image, huffman_codes)) {
    err = VP8_ENC_ERROR_OUT_OF_MEMORY;
    goto Error;
  }
  // Free combined histograms.
  VP8LFreeHistogramSet(histogram_image);
  histogram_image = NULL;

  // Free scratch histograms.
  VP8LFreeHistogramSet(tmp_histos);
  tmp_histos = NULL;

  // Color Cache parameters.
  if (*cache_bits > 0) {
    VP8LPutBits(bw, 1, 1);
    VP8LPutBits(bw, *cache_bits, 4);
  } else {
    VP8LPutBits(bw, 0, 1);
  }

  // Huffman image + meta huffman.
  {
    const int write_histogram_image = (histogram_image_size > 1);
    VP8LPutBits(bw, write_histogram_image, 1);
    if (write_histogram_image) {
      uint32_t* const histogram_argb =
          (uint32_t*)WebPSafeMalloc(histogram_image_xysize,
                                    sizeof(*histogram_argb));
      int max_index = 0;
      uint32_t i;
      if (histogram_argb == NULL) {
        err = VP8_ENC_ERROR_OUT_OF_MEMORY;
        goto Error;
      }
      for (i = 0; i < histogram_image_xysize; ++i) {
        const int symbol_index = histogram_symbols[i] & 0xffff;
        histogram_argb[i] = (symbol_index << 8);
        if (symbol_index >= max_index) {
          max_index = symbol_index + 1;
        }
      }
      histogram_image_size = max_index;

      VP8LPutBits(bw, histogram_bits - 2, 3);
      err = EncodeImageNoHuffman(bw, histogram_argb, hash_chain, refs_array,
                                 VP8LSubSampleSize(width, histogram_bits),
                                 VP8LSubSampleSize(height, histogram_bits),
                                 quality);
      WebPSafeFree(histogram_argb);
      if (err != VP8_ENC_OK) goto Error;
    }
  }

  // Store Huffman codes.
  {
    int i;
    int max_tokens = 0;
    huff_tree = (HuffmanTree*)WebPSafeMalloc(3ULL * CODE_LENGTH_CODES,
                                             sizeof(*huff_tree));
    if (huff_tree == NULL) {
      err = VP8_ENC_ERROR_OUT_OF_MEMORY;
      goto Error;
    }
    // Find maximum number of symbols for the huffman tree-set.
    for (i = 0; i < 5 * histogram_image_size; ++i) {
      HuffmanTreeCode* const codes = &huffman_codes[i];
      if (max_tokens < codes->num_symbols) {
        max_tokens = codes->num_symbols;
      }
    }
    tokens = (HuffmanTreeToken*)WebPSafeMalloc(max_tokens,
                                               sizeof(*tokens));
    if (tokens == NULL) {
      err = VP8_ENC_ERROR_OUT_OF_MEMORY;
      goto Error;
    }
    for (i = 0; i < 5 * histogram_image_size; ++i) {
      HuffmanTreeCode* const codes = &huffman_codes[i];
      StoreHuffmanCode(bw, huff_tree, tokens, codes);
      ClearHuffmanTreeIfOnlyOneSymbol(codes);
    }
  }

  *hdr_size = (int)(VP8LBitWriterNumBytes(bw) - init_byte_position);
  // Store actual literals.
  err = StoreImageToBitMask(bw, width, histogram_bits, &refs,
                            histogram_symbols, huffman_codes);
  *data_size =
        (int)(VP8LBitWriterNumBytes(bw) - init_byte_position - *hdr_size);

 Error:
  WebPSafeFree(tokens);
  WebPSafeFree(huff_tree);
  VP8LFreeHistogramSet(histogram_image);
  VP8LFreeHistogramSet(tmp_histos);
  VP8LBackwardRefsClear(&refs);
  if (huffman_codes != NULL) {
    WebPSafeFree(huffman_codes->codes);
    WebPSafeFree(huffman_codes);
  }
  WebPSafeFree(histogram_symbols);
  return err;
}

// -----------------------------------------------------------------------------
// Transforms

static void ApplySubtractGreen(VP8LEncoder* const enc, int width, int height,
                               VP8LBitWriter* const bw) {
  VP8LPutBits(bw, TRANSFORM_PRESENT, 1);
  VP8LPutBits(bw, SUBTRACT_GREEN, 2);
  VP8LSubtractGreenFromBlueAndRed(enc->argb_, width * height);
}

static WebPEncodingError ApplyPredictFilter(const VP8LEncoder* const enc,
                                            int width, int height,
                                            int quality, int low_effort,
                                            VP8LBitWriter* const bw) {
  const int pred_bits = enc->transform_bits_;
  const int transform_width = VP8LSubSampleSize(width, pred_bits);
  const int transform_height = VP8LSubSampleSize(height, pred_bits);

  VP8LResidualImage(width, height, pred_bits, low_effort, enc->argb_,
                    enc->argb_scratch_, enc->transform_data_);
  VP8LPutBits(bw, TRANSFORM_PRESENT, 1);
  VP8LPutBits(bw, PREDICTOR_TRANSFORM, 2);
  assert(pred_bits >= 2);
  VP8LPutBits(bw, pred_bits - 2, 3);
  return EncodeImageNoHuffman(bw, enc->transform_data_,
                              (VP8LHashChain*)&enc->hash_chain_,
                              (VP8LBackwardRefs*)enc->refs_,  // cast const away
                              transform_width, transform_height,
                              quality);
}

static WebPEncodingError ApplyCrossColorFilter(const VP8LEncoder* const enc,
                                               int width, int height,
                                               int quality,
                                               VP8LBitWriter* const bw) {
  const int ccolor_transform_bits = enc->transform_bits_;
  const int transform_width = VP8LSubSampleSize(width, ccolor_transform_bits);
  const int transform_height = VP8LSubSampleSize(height, ccolor_transform_bits);

  VP8LColorSpaceTransform(width, height, ccolor_transform_bits, quality,
                          enc->argb_, enc->transform_data_);
  VP8LPutBits(bw, TRANSFORM_PRESENT, 1);
  VP8LPutBits(bw, CROSS_COLOR_TRANSFORM, 2);
  assert(ccolor_transform_bits >= 2);
  VP8LPutBits(bw, ccolor_transform_bits - 2, 3);
  return EncodeImageNoHuffman(bw, enc->transform_data_,
                              (VP8LHashChain*)&enc->hash_chain_,
                              (VP8LBackwardRefs*)enc->refs_,  // cast const away
                              transform_width, transform_height,
                              quality);
}

// -----------------------------------------------------------------------------

static WebPEncodingError WriteRiffHeader(const WebPPicture* const pic,
                                         size_t riff_size, size_t vp8l_size) {
  uint8_t riff[RIFF_HEADER_SIZE + CHUNK_HEADER_SIZE + VP8L_SIGNATURE_SIZE] = {
    'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'E', 'B', 'P',
    'V', 'P', '8', 'L', 0, 0, 0, 0, VP8L_MAGIC_BYTE,
  };
  PutLE32(riff + TAG_SIZE, (uint32_t)riff_size);
  PutLE32(riff + RIFF_HEADER_SIZE + TAG_SIZE, (uint32_t)vp8l_size);
  if (!pic->writer(riff, sizeof(riff), pic)) {
    return VP8_ENC_ERROR_BAD_WRITE;
  }
  return VP8_ENC_OK;
}

static int WriteImageSize(const WebPPicture* const pic,
                          VP8LBitWriter* const bw) {
  const int width = pic->width - 1;
  const int height = pic->height - 1;
  assert(width < WEBP_MAX_DIMENSION && height < WEBP_MAX_DIMENSION);

  VP8LPutBits(bw, width, VP8L_IMAGE_SIZE_BITS);
  VP8LPutBits(bw, height, VP8L_IMAGE_SIZE_BITS);
  return !bw->error_;
}

static int WriteRealAlphaAndVersion(VP8LBitWriter* const bw, int has_alpha) {
  VP8LPutBits(bw, has_alpha, 1);
  VP8LPutBits(bw, VP8L_VERSION, VP8L_VERSION_BITS);
  return !bw->error_;
}

static WebPEncodingError WriteImage(const WebPPicture* const pic,
                                    VP8LBitWriter* const bw,
                                    size_t* const coded_size) {
  WebPEncodingError err = VP8_ENC_OK;
  const uint8_t* const webpll_data = VP8LBitWriterFinish(bw);
  const size_t webpll_size = VP8LBitWriterNumBytes(bw);
  const size_t vp8l_size = VP8L_SIGNATURE_SIZE + webpll_size;
  const size_t pad = vp8l_size & 1;
  const size_t riff_size = TAG_SIZE + CHUNK_HEADER_SIZE + vp8l_size + pad;

  err = WriteRiffHeader(pic, riff_size, vp8l_size);
  if (err != VP8_ENC_OK) goto Error;

  if (!pic->writer(webpll_data, webpll_size, pic)) {
    err = VP8_ENC_ERROR_BAD_WRITE;
    goto Error;
  }

  if (pad) {
    const uint8_t pad_byte[1] = { 0 };
    if (!pic->writer(pad_byte, 1, pic)) {
      err = VP8_ENC_ERROR_BAD_WRITE;
      goto Error;
    }
  }
  *coded_size = CHUNK_HEADER_SIZE + riff_size;
  return VP8_ENC_OK;

 Error:
  return err;
}

// -----------------------------------------------------------------------------

// Allocates the memory for argb (W x H) buffer, 2 rows of context for
// prediction and transform data.
static WebPEncodingError AllocateTransformBuffer(VP8LEncoder* const enc,
                                                 int width, int height) {
  WebPEncodingError err = VP8_ENC_OK;
  const int tile_size = 1 << enc->transform_bits_;
  const uint64_t image_size = width * height;
  const uint64_t argb_scratch_size = tile_size * width + width;
  const int transform_data_size =
      VP8LSubSampleSize(width, enc->transform_bits_) *
      VP8LSubSampleSize(height, enc->transform_bits_);
  const uint64_t total_size =
      image_size + argb_scratch_size + (uint64_t)transform_data_size;
  uint32_t* mem = (uint32_t*)WebPSafeMalloc(total_size, sizeof(*mem));
  if (mem == NULL) {
    err = VP8_ENC_ERROR_OUT_OF_MEMORY;
    goto Error;
  }
  enc->argb_ = mem;
  mem += image_size;
  enc->argb_scratch_ = mem;
  mem += argb_scratch_size;
  enc->transform_data_ = mem;
  enc->current_width_ = width;

 Error:
  return err;
}

static void MapToPalette(const uint32_t palette[], int num_colors,
                         uint32_t* const last_pix, int* const last_idx,
                         const uint32_t* src, uint8_t* dst, int width) {
  int x;
  int prev_idx = *last_idx;
  uint32_t prev_pix = *last_pix;
  for (x = 0; x < width; ++x) {
    const uint32_t pix = src[x];
    if (pix != prev_pix) {
      int i;
      for (i = 0; i < num_colors; ++i) {
        if (pix == palette[i]) {
          prev_idx = i;
          prev_pix = pix;
          break;
        }
      }
    }
    dst[x] = prev_idx;
  }
  *last_idx = prev_idx;
  *last_pix = prev_pix;
}

static void ApplyPalette(uint32_t* src, uint32_t* dst,
                         uint32_t src_stride, uint32_t dst_stride,
                         const uint32_t* palette, int palette_size,
                         int width, int height, int xbits, uint8_t* row) {
  int i, x, y;
  int use_LUT = 1;
  for (i = 0; i < palette_size; ++i) {
    if ((palette[i] & 0xffff00ffu) != 0) {
      use_LUT = 0;
      break;
    }
  }

  if (use_LUT) {
    uint8_t inv_palette[MAX_PALETTE_SIZE] = { 0 };
    for (i = 0; i < palette_size; ++i) {
      const int color = (palette[i] >> 8) & 0xff;
      inv_palette[color] = i;
    }
    for (y = 0; y < height; ++y) {
      for (x = 0; x < width; ++x) {
        const int color = (src[x] >> 8) & 0xff;
        row[x] = inv_palette[color];
      }
      VP8LBundleColorMap(row, width, xbits, dst);
      src += src_stride;
      dst += dst_stride;
    }
  } else {
    // Use 1 pixel cache for ARGB pixels.
    uint32_t last_pix = palette[0];
    int last_idx = 0;
    for (y = 0; y < height; ++y) {
      MapToPalette(palette, palette_size, &last_pix, &last_idx,
                   src, row, width);
      VP8LBundleColorMap(row, width, xbits, dst);
      src += src_stride;
      dst += dst_stride;
    }
  }
}

// Note: Expects "enc->palette_" to be set properly.
// Also, "enc->palette_" will be modified after this call and should not be used
// later.
static WebPEncodingError EncodePalette(VP8LBitWriter* const bw,
                                       VP8LEncoder* const enc) {
  WebPEncodingError err = VP8_ENC_OK;
  int i;
  const WebPPicture* const pic = enc->pic_;
  uint32_t* src = pic->argb;
  uint32_t* dst;
  const int width = pic->width;
  const int height = pic->height;
  uint32_t* const palette = enc->palette_;
  const int palette_size = enc->palette_size_;
  uint8_t* row = NULL;
  int xbits;

  // Replace each input pixel by corresponding palette index.
  // This is done line by line.
  if (palette_size <= 4) {
    xbits = (palette_size <= 2) ? 3 : 2;
  } else {
    xbits = (palette_size <= 16) ? 1 : 0;
  }

  err = AllocateTransformBuffer(enc, VP8LSubSampleSize(width, xbits), height);
  if (err != VP8_ENC_OK) goto Error;
  dst = enc->argb_;

  row = (uint8_t*)WebPSafeMalloc(width, sizeof(*row));
  if (row == NULL) return VP8_ENC_ERROR_OUT_OF_MEMORY;

  ApplyPalette(src, dst, pic->argb_stride, enc->current_width_,
               palette, palette_size, width, height, xbits, row);

  // Save palette to bitstream.
  VP8LPutBits(bw, TRANSFORM_PRESENT, 1);
  VP8LPutBits(bw, COLOR_INDEXING_TRANSFORM, 2);
  assert(palette_size >= 1);
  VP8LPutBits(bw, palette_size - 1, 8);
  for (i = palette_size - 1; i >= 1; --i) {
    palette[i] = VP8LSubPixels(palette[i], palette[i - 1]);
  }
  err = EncodeImageNoHuffman(bw, palette, &enc->hash_chain_, enc->refs_,
                             palette_size, 1, 20 /* quality */);
 Error:
  WebPSafeFree(row);
  return err;
}

// -----------------------------------------------------------------------------
// VP8LEncoder

static VP8LEncoder* VP8LEncoderNew(const WebPConfig* const config,
                                   const WebPPicture* const picture) {
  VP8LEncoder* const enc = (VP8LEncoder*)WebPSafeCalloc(1ULL, sizeof(*enc));
  if (enc == NULL) {
    WebPEncodingSetError(picture, VP8_ENC_ERROR_OUT_OF_MEMORY);
    return NULL;
  }
  enc->config_ = config;
  enc->pic_ = picture;

  VP8LDspInit();

  return enc;
}

static void VP8LEncoderDelete(VP8LEncoder* enc) {
  if (enc != NULL) {
    VP8LHashChainClear(&enc->hash_chain_);
    VP8LBackwardRefsClear(&enc->refs_[0]);
    VP8LBackwardRefsClear(&enc->refs_[1]);
    WebPSafeFree(enc->argb_);
    WebPSafeFree(enc);
  }
}

// -----------------------------------------------------------------------------
// Main call

WebPEncodingError VP8LEncodeStream(const WebPConfig* const config,
                                   const WebPPicture* const picture,
                                   VP8LBitWriter* const bw, int use_cache) {
  WebPEncodingError err = VP8_ENC_OK;
  const int quality = (int)config->quality;
  const int low_effort = (config->method == 0);
  const int width = picture->width;
  const int height = picture->height;
  VP8LEncoder* const enc = VP8LEncoderNew(config, picture);
  const size_t byte_position = VP8LBitWriterNumBytes(bw);
  int use_near_lossless = 0;
  int hdr_size = 0;
  int data_size = 0;

  if (enc == NULL) {
    err = VP8_ENC_ERROR_OUT_OF_MEMORY;
    goto Error;
  }

  // ---------------------------------------------------------------------------
  // Analyze image (entropy, num_palettes etc)

  if (!AnalyzeAndInit(enc, config->image_hint)) {
    err = VP8_ENC_ERROR_OUT_OF_MEMORY;
    goto Error;
  }

  // Apply near-lossless preprocessing.
  use_near_lossless = !enc->use_palette_ && (config->near_lossless < 100);
  if (use_near_lossless) {
    if (!VP8ApplyNearLossless(width, height, picture->argb,
                              config->near_lossless)) {
      err = VP8_ENC_ERROR_OUT_OF_MEMORY;
      goto Error;
    }
  }

  if (enc->use_palette_) {
    err = EncodePalette(bw, enc);
    if (err != VP8_ENC_OK) goto Error;
  }

  // In case image is not packed.
  if (enc->argb_ == NULL) {
    int y;
    err = AllocateTransformBuffer(enc, width, height);
    if (err != VP8_ENC_OK) goto Error;
    assert(enc->argb_ != NULL);
    for (y = 0; y < height; ++y) {
      memcpy(enc->argb_ + y * width,
             picture->argb + y * picture->argb_stride,
             width * sizeof(*enc->argb_));
    }
    enc->current_width_ = width;
  }

  // ---------------------------------------------------------------------------
  // Apply transforms and write transform data.

  if (enc->use_subtract_green_) {
    ApplySubtractGreen(enc, enc->current_width_, height, bw);
  }

  if (enc->use_predict_) {
    err = ApplyPredictFilter(enc, enc->current_width_, height, quality,
                             low_effort, bw);
    if (err != VP8_ENC_OK) goto Error;
  }

  if (enc->use_cross_color_) {
    err = ApplyCrossColorFilter(enc, enc->current_width_, height, quality, bw);
    if (err != VP8_ENC_OK) goto Error;
  }

  VP8LPutBits(bw, !TRANSFORM_PRESENT, 1);  // No more transforms.

  // ---------------------------------------------------------------------------
  // Encode and write the transformed image.
  err = EncodeImageInternal(bw, enc->argb_, &enc->hash_chain_, enc->refs_,
                            enc->current_width_, height, quality, low_effort,
                            use_cache, &enc->cache_bits_, enc->histo_bits_,
                            byte_position, &hdr_size, &data_size);
  if (err != VP8_ENC_OK) goto Error;

  if (picture->stats != NULL) {
    WebPAuxStats* const stats = picture->stats;
    stats->lossless_features = 0;
    if (enc->use_predict_) stats->lossless_features |= 1;
    if (enc->use_cross_color_) stats->lossless_features |= 2;
    if (enc->use_subtract_green_) stats->lossless_features |= 4;
    if (enc->use_palette_) stats->lossless_features |= 8;
    stats->histogram_bits = enc->histo_bits_;
    stats->transform_bits = enc->transform_bits_;
    stats->cache_bits = enc->cache_bits_;
    stats->palette_size = enc->palette_size_;
    stats->lossless_size = (int)(VP8LBitWriterNumBytes(bw) - byte_position);
    stats->lossless_hdr_size = hdr_size;
    stats->lossless_data_size = data_size;
  }

 Error:
  VP8LEncoderDelete(enc);
  return err;
}

int VP8LEncodeImage(const WebPConfig* const config,
                    const WebPPicture* const picture) {
  int width, height;
  int has_alpha;
  size_t coded_size;
  int percent = 0;
  int initial_size;
  WebPEncodingError err = VP8_ENC_OK;
  VP8LBitWriter bw;

  if (picture == NULL) return 0;

  if (config == NULL || picture->argb == NULL) {
    err = VP8_ENC_ERROR_NULL_PARAMETER;
    WebPEncodingSetError(picture, err);
    return 0;
  }

  width = picture->width;
  height = picture->height;
  // Initialize BitWriter with size corresponding to 16 bpp to photo images and
  // 8 bpp for graphical images.
  initial_size = (config->image_hint == WEBP_HINT_GRAPH) ?
                 width * height : width * height * 2;
  if (!VP8LBitWriterInit(&bw, initial_size)) {
    err = VP8_ENC_ERROR_OUT_OF_MEMORY;
    goto Error;
  }

  if (!WebPReportProgress(picture, 1, &percent)) {
 UserAbort:
    err = VP8_ENC_ERROR_USER_ABORT;
    goto Error;
  }
  // Reset stats (for pure lossless coding)
  if (picture->stats != NULL) {
    WebPAuxStats* const stats = picture->stats;
    memset(stats, 0, sizeof(*stats));
    stats->PSNR[0] = 99.f;
    stats->PSNR[1] = 99.f;
    stats->PSNR[2] = 99.f;
    stats->PSNR[3] = 99.f;
    stats->PSNR[4] = 99.f;
  }

  // Write image size.
  if (!WriteImageSize(picture, &bw)) {
    err = VP8_ENC_ERROR_OUT_OF_MEMORY;
    goto Error;
  }

  has_alpha = WebPPictureHasTransparency(picture);
  // Write the non-trivial Alpha flag and lossless version.
  if (!WriteRealAlphaAndVersion(&bw, has_alpha)) {
    err = VP8_ENC_ERROR_OUT_OF_MEMORY;
    goto Error;
  }

  if (!WebPReportProgress(picture, 5, &percent)) goto UserAbort;

  // Encode main image stream.
  err = VP8LEncodeStream(config, picture, &bw, 1 /*use_cache*/);
  if (err != VP8_ENC_OK) goto Error;

  // TODO(skal): have a fine-grained progress report in VP8LEncodeStream().
  if (!WebPReportProgress(picture, 90, &percent)) goto UserAbort;

  // Finish the RIFF chunk.
  err = WriteImage(picture, &bw, &coded_size);
  if (err != VP8_ENC_OK) goto Error;

  if (!WebPReportProgress(picture, 100, &percent)) goto UserAbort;

  // Save size.
  if (picture->stats != NULL) {
    picture->stats->coded_size += (int)coded_size;
    picture->stats->lossless_size = (int)coded_size;
  }

  if (picture->extra_info != NULL) {
    const int mb_w = (width + 15) >> 4;
    const int mb_h = (height + 15) >> 4;
    memset(picture->extra_info, 0, mb_w * mb_h * sizeof(*picture->extra_info));
  }

 Error:
  if (bw.error_) err = VP8_ENC_ERROR_OUT_OF_MEMORY;
  VP8LBitWriterWipeOut(&bw);
  if (err != VP8_ENC_OK) {
    WebPEncodingSetError(picture, err);
    return 0;
  }
  return 1;
}

//------------------------------------------------------------------------------
