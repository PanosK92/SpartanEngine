// Copyright 2014 Google Inc. All Rights Reserved.
//
// Use of this source code is governed by a BSD-style license
// that can be found in the COPYING file in the root of the source
// tree. An additional intellectual property rights grant can be found
// in the file PATENTS. All contributing project authors may
// be found in the AUTHORS file in the root of the source tree.
// -----------------------------------------------------------------------------
//
// Image transforms and color space conversion methods for lossless decoder.
//
// Author(s):  Djordje Pesut    (djordje.pesut@imgtec.com)
//             Jovan Zelincevic (jovan.zelincevic@imgtec.com)

#include "./dsp.h"

#if defined(WEBP_USE_MIPS_DSP_R2)

#include "./lossless.h"

#define MAP_COLOR_FUNCS(FUNC_NAME, TYPE, GET_INDEX, GET_VALUE)                 \
static void FUNC_NAME(const TYPE* src,                                         \
                      const uint32_t* const color_map,                         \
                      TYPE* dst, int y_start, int y_end,                       \
                      int width) {                                             \
  int y;                                                                       \
  for (y = y_start; y < y_end; ++y) {                                          \
    int x;                                                                     \
    for (x = 0; x < (width >> 2); ++x) {                                       \
      int tmp1, tmp2, tmp3, tmp4;                                              \
      __asm__ volatile (                                                       \
      ".ifc        "#TYPE",  uint8_t                    \n\t"                  \
        "lbu       %[tmp1],  0(%[src])                  \n\t"                  \
        "lbu       %[tmp2],  1(%[src])                  \n\t"                  \
        "lbu       %[tmp3],  2(%[src])                  \n\t"                  \
        "lbu       %[tmp4],  3(%[src])                  \n\t"                  \
        "addiu     %[src],   %[src],      4             \n\t"                  \
      ".endif                                           \n\t"                  \
      ".ifc        "#TYPE",  uint32_t                   \n\t"                  \
        "lw        %[tmp1],  0(%[src])                  \n\t"                  \
        "lw        %[tmp2],  4(%[src])                  \n\t"                  \
        "lw        %[tmp3],  8(%[src])                  \n\t"                  \
        "lw        %[tmp4],  12(%[src])                 \n\t"                  \
        "ext       %[tmp1],  %[tmp1],     8,        8   \n\t"                  \
        "ext       %[tmp2],  %[tmp2],     8,        8   \n\t"                  \
        "ext       %[tmp3],  %[tmp3],     8,        8   \n\t"                  \
        "ext       %[tmp4],  %[tmp4],     8,        8   \n\t"                  \
        "addiu     %[src],   %[src],      16            \n\t"                  \
      ".endif                                           \n\t"                  \
        "sll       %[tmp1],  %[tmp1],     2             \n\t"                  \
        "sll       %[tmp2],  %[tmp2],     2             \n\t"                  \
        "sll       %[tmp3],  %[tmp3],     2             \n\t"                  \
        "sll       %[tmp4],  %[tmp4],     2             \n\t"                  \
        "lwx       %[tmp1],  %[tmp1](%[color_map])      \n\t"                  \
        "lwx       %[tmp2],  %[tmp2](%[color_map])      \n\t"                  \
        "lwx       %[tmp3],  %[tmp3](%[color_map])      \n\t"                  \
        "lwx       %[tmp4],  %[tmp4](%[color_map])      \n\t"                  \
      ".ifc        "#TYPE",  uint8_t                    \n\t"                  \
        "ext       %[tmp1],  %[tmp1],     8,        8   \n\t"                  \
        "ext       %[tmp2],  %[tmp2],     8,        8   \n\t"                  \
        "ext       %[tmp3],  %[tmp3],     8,        8   \n\t"                  \
        "ext       %[tmp4],  %[tmp4],     8,        8   \n\t"                  \
        "sb        %[tmp1],  0(%[dst])                  \n\t"                  \
        "sb        %[tmp2],  1(%[dst])                  \n\t"                  \
        "sb        %[tmp3],  2(%[dst])                  \n\t"                  \
        "sb        %[tmp4],  3(%[dst])                  \n\t"                  \
        "addiu     %[dst],   %[dst],      4             \n\t"                  \
      ".endif                                           \n\t"                  \
      ".ifc        "#TYPE",  uint32_t                   \n\t"                  \
        "sw        %[tmp1],  0(%[dst])                  \n\t"                  \
        "sw        %[tmp2],  4(%[dst])                  \n\t"                  \
        "sw        %[tmp3],  8(%[dst])                  \n\t"                  \
        "sw        %[tmp4],  12(%[dst])                 \n\t"                  \
        "addiu     %[dst],   %[dst],      16            \n\t"                  \
      ".endif                                           \n\t"                  \
        : [tmp1]"=&r"(tmp1), [tmp2]"=&r"(tmp2), [tmp3]"=&r"(tmp3),             \
          [tmp4]"=&r"(tmp4), [src]"+&r"(src), [dst]"+r"(dst)                   \
        : [color_map]"r"(color_map)                                            \
        : "memory"                                                             \
      );                                                                       \
    }                                                                          \
    for (x = 0; x < (width & 3); ++x) {                                        \
      *dst++ = GET_VALUE(color_map[GET_INDEX(*src++)]);                        \
    }                                                                          \
  }                                                                            \
}

MAP_COLOR_FUNCS(MapARGB, uint32_t, VP8GetARGBIndex, VP8GetARGBValue)
MAP_COLOR_FUNCS(MapAlpha, uint8_t, VP8GetAlphaIndex, VP8GetAlphaValue)

#undef MAP_COLOR_FUNCS

static WEBP_INLINE uint32_t ClampedAddSubtractFull(uint32_t c0, uint32_t c1,
                                                   uint32_t c2) {
  int temp0, temp1, temp2, temp3, temp4, temp5;
  __asm__ volatile (
    "preceu.ph.qbr   %[temp1],   %[c0]                 \n\t"
    "preceu.ph.qbl   %[temp2],   %[c0]                 \n\t"
    "preceu.ph.qbr   %[temp3],   %[c1]                 \n\t"
    "preceu.ph.qbl   %[temp4],   %[c1]                 \n\t"
    "preceu.ph.qbr   %[temp5],   %[c2]                 \n\t"
    "preceu.ph.qbl   %[temp0],   %[c2]                 \n\t"
    "subq.ph         %[temp3],   %[temp3],   %[temp5]  \n\t"
    "subq.ph         %[temp4],   %[temp4],   %[temp0]  \n\t"
    "addq.ph         %[temp1],   %[temp1],   %[temp3]  \n\t"
    "addq.ph         %[temp2],   %[temp2],   %[temp4]  \n\t"
    "shll_s.ph       %[temp1],   %[temp1],   7         \n\t"
    "shll_s.ph       %[temp2],   %[temp2],   7         \n\t"
    "precrqu_s.qb.ph %[temp2],   %[temp2],   %[temp1]  \n\t"
    : [temp0]"=r"(temp0), [temp1]"=&r"(temp1), [temp2]"=&r"(temp2),
      [temp3]"=&r"(temp3), [temp4]"=&r"(temp4), [temp5]"=&r"(temp5)
    : [c0]"r"(c0), [c1]"r"(c1), [c2]"r"(c2)
    : "memory"
  );
  return temp2;
}

static WEBP_INLINE uint32_t ClampedAddSubtractHalf(uint32_t c0, uint32_t c1,
                                                   uint32_t c2) {
  int temp0, temp1, temp2, temp3, temp4, temp5;
  __asm__ volatile (
    "adduh.qb         %[temp5],   %[c0],      %[c1]       \n\t"
    "preceu.ph.qbr    %[temp3],   %[c2]                   \n\t"
    "preceu.ph.qbr    %[temp1],   %[temp5]                \n\t"
    "preceu.ph.qbl    %[temp2],   %[temp5]                \n\t"
    "preceu.ph.qbl    %[temp4],   %[c2]                   \n\t"
    "subq.ph          %[temp3],   %[temp1],   %[temp3]    \n\t"
    "subq.ph          %[temp4],   %[temp2],   %[temp4]    \n\t"
    "shrl.ph          %[temp5],   %[temp3],   15          \n\t"
    "shrl.ph          %[temp0],   %[temp4],   15          \n\t"
    "addq.ph          %[temp3],   %[temp3],   %[temp5]    \n\t"
    "addq.ph          %[temp4],   %[temp0],   %[temp4]    \n\t"
    "shra.ph          %[temp3],   %[temp3],   1           \n\t"
    "shra.ph          %[temp4],   %[temp4],   1           \n\t"
    "addq.ph          %[temp1],   %[temp1],   %[temp3]    \n\t"
    "addq.ph          %[temp2],   %[temp2],   %[temp4]    \n\t"
    "shll_s.ph        %[temp1],   %[temp1],   7           \n\t"
    "shll_s.ph        %[temp2],   %[temp2],   7           \n\t"
    "precrqu_s.qb.ph  %[temp1],   %[temp2],   %[temp1]    \n\t"
    : [temp0]"=r"(temp0), [temp1]"=&r"(temp1), [temp2]"=&r"(temp2),
      [temp3]"=&r"(temp3), [temp4]"=r"(temp4), [temp5]"=&r"(temp5)
    : [c0]"r"(c0), [c1]"r"(c1), [c2]"r"(c2)
    : "memory"
  );
  return temp1;
}

static void SubtractGreenFromBlueAndRed(uint32_t* argb_data,
                                        int num_pixels) {
  uint32_t temp0, temp1, temp2, temp3, temp4, temp5, temp6, temp7;
  uint32_t* const p_loop1_end = argb_data + (num_pixels & ~3);
  uint32_t* const p_loop2_end = p_loop1_end + (num_pixels & 3);
  __asm__ volatile (
    ".set       push                                          \n\t"
    ".set       noreorder                                     \n\t"
    "beq        %[argb_data],    %[p_loop1_end],     3f       \n\t"
    " nop                                                     \n\t"
  "0:                                                         \n\t"
    "lw         %[temp0],        0(%[argb_data])              \n\t"
    "lw         %[temp1],        4(%[argb_data])              \n\t"
    "lw         %[temp2],        8(%[argb_data])              \n\t"
    "lw         %[temp3],        12(%[argb_data])             \n\t"
    "ext        %[temp4],        %[temp0],           8,    8  \n\t"
    "ext        %[temp5],        %[temp1],           8,    8  \n\t"
    "ext        %[temp6],        %[temp2],           8,    8  \n\t"
    "ext        %[temp7],        %[temp3],           8,    8  \n\t"
    "addiu      %[argb_data],    %[argb_data],       16       \n\t"
    "replv.ph   %[temp4],        %[temp4]                     \n\t"
    "replv.ph   %[temp5],        %[temp5]                     \n\t"
    "replv.ph   %[temp6],        %[temp6]                     \n\t"
    "replv.ph   %[temp7],        %[temp7]                     \n\t"
    "subu.qb    %[temp0],        %[temp0],           %[temp4] \n\t"
    "subu.qb    %[temp1],        %[temp1],           %[temp5] \n\t"
    "subu.qb    %[temp2],        %[temp2],           %[temp6] \n\t"
    "subu.qb    %[temp3],        %[temp3],           %[temp7] \n\t"
    "sw         %[temp0],        -16(%[argb_data])            \n\t"
    "sw         %[temp1],        -12(%[argb_data])            \n\t"
    "sw         %[temp2],        -8(%[argb_data])             \n\t"
    "bne        %[argb_data],    %[p_loop1_end],     0b       \n\t"
    " sw        %[temp3],        -4(%[argb_data])             \n\t"
  "3:                                                         \n\t"
    "beq        %[argb_data],    %[p_loop2_end],     2f       \n\t"
    " nop                                                     \n\t"
  "1:                                                         \n\t"
    "lw         %[temp0],        0(%[argb_data])              \n\t"
    "addiu      %[argb_data],    %[argb_data],       4        \n\t"
    "ext        %[temp4],        %[temp0],           8,    8  \n\t"
    "replv.ph   %[temp4],        %[temp4]                     \n\t"
    "subu.qb    %[temp0],        %[temp0],           %[temp4] \n\t"
    "bne        %[argb_data],    %[p_loop2_end],     1b       \n\t"
    " sw        %[temp0],        -4(%[argb_data])             \n\t"
  "2:                                                         \n\t"
    ".set       pop                                           \n\t"
    : [argb_data]"+&r"(argb_data), [temp0]"=&r"(temp0),
      [temp1]"=&r"(temp1), [temp2]"=&r"(temp2), [temp3]"=&r"(temp3),
      [temp4]"=&r"(temp4), [temp5]"=&r"(temp5), [temp6]"=&r"(temp6),
      [temp7]"=&r"(temp7)
    : [p_loop1_end]"r"(p_loop1_end), [p_loop2_end]"r"(p_loop2_end)
    : "memory"
  );
}

static WEBP_INLINE uint32_t Select(uint32_t a, uint32_t b, uint32_t c) {
  int temp0, temp1, temp2, temp3, temp4, temp5;
  __asm__ volatile (
    "cmpgdu.lt.qb %[temp1], %[c],     %[b]             \n\t"
    "pick.qb      %[temp1], %[b],     %[c]             \n\t"
    "pick.qb      %[temp2], %[c],     %[b]             \n\t"
    "cmpgdu.lt.qb %[temp4], %[c],     %[a]             \n\t"
    "pick.qb      %[temp4], %[a],     %[c]             \n\t"
    "pick.qb      %[temp5], %[c],     %[a]             \n\t"
    "subu.qb      %[temp3], %[temp1], %[temp2]         \n\t"
    "subu.qb      %[temp0], %[temp4], %[temp5]         \n\t"
    "raddu.w.qb   %[temp3], %[temp3]                   \n\t"
    "raddu.w.qb   %[temp0], %[temp0]                   \n\t"
    "subu         %[temp3], %[temp3], %[temp0]         \n\t"
    "slti         %[temp0], %[temp3], 0x1              \n\t"
    "movz         %[a],     %[b],     %[temp0]         \n\t"
    : [temp1]"=&r"(temp1), [temp2]"=&r"(temp2), [temp3]"=&r"(temp3),
      [temp4]"=&r"(temp4), [temp5]"=&r"(temp5), [temp0]"=&r"(temp0),
      [a]"+&r"(a)
    : [b]"r"(b), [c]"r"(c)
  );
  return a;
}

static WEBP_INLINE uint32_t Average2(uint32_t a0, uint32_t a1) {
  __asm__ volatile (
    "adduh.qb    %[a0], %[a0], %[a1]       \n\t"
    : [a0]"+r"(a0)
    : [a1]"r"(a1)
  );
  return a0;
}

static WEBP_INLINE uint32_t Average3(uint32_t a0, uint32_t a1, uint32_t a2) {
  return Average2(Average2(a0, a2), a1);
}

static WEBP_INLINE uint32_t Average4(uint32_t a0, uint32_t a1,
                                     uint32_t a2, uint32_t a3) {
  return Average2(Average2(a0, a1), Average2(a2, a3));
}

static uint32_t Predictor5(uint32_t left, const uint32_t* const top) {
  return Average3(left, top[0], top[1]);
}

static uint32_t Predictor6(uint32_t left, const uint32_t* const top) {
  return Average2(left, top[-1]);
}

static uint32_t Predictor7(uint32_t left, const uint32_t* const top) {
  return Average2(left, top[0]);
}

static uint32_t Predictor8(uint32_t left, const uint32_t* const top) {
  (void)left;
  return Average2(top[-1], top[0]);
}

static uint32_t Predictor9(uint32_t left, const uint32_t* const top) {
  (void)left;
  return Average2(top[0], top[1]);
}

static uint32_t Predictor10(uint32_t left, const uint32_t* const top) {
  return Average4(left, top[-1], top[0], top[1]);
}

static uint32_t Predictor11(uint32_t left, const uint32_t* const top) {
  return Select(top[0], left, top[-1]);
}

static uint32_t Predictor12(uint32_t left, const uint32_t* const top) {
  return ClampedAddSubtractFull(left, top[0], top[-1]);
}

static uint32_t Predictor13(uint32_t left, const uint32_t* const top) {
  return ClampedAddSubtractHalf(left, top[0], top[-1]);
}

static WEBP_INLINE uint32_t ColorTransformDelta(int8_t color_pred,
                                                int8_t color) {
  return (uint32_t)((int)(color_pred) * color) >> 5;
}

static void TransformColor(const VP8LMultipliers* const m, uint32_t* data,
                           int num_pixels) {
  int temp0, temp1, temp2, temp3, temp4, temp5;
  uint32_t argb, argb1, new_red, new_red1;
  const uint32_t G_to_R = m->green_to_red_;
  const uint32_t G_to_B = m->green_to_blue_;
  const uint32_t R_to_B = m->red_to_blue_;
  uint32_t* const p_loop_end = data + (num_pixels & ~1);
  __asm__ volatile (
    ".set            push                                    \n\t"
    ".set            noreorder                               \n\t"
    "beq             %[data],      %[p_loop_end],  1f        \n\t"
    " nop                                                    \n\t"
    "replv.ph        %[temp0],     %[G_to_R]                 \n\t"
    "replv.ph        %[temp1],     %[G_to_B]                 \n\t"
    "replv.ph        %[temp2],     %[R_to_B]                 \n\t"
    "shll.ph         %[temp0],     %[temp0],       8         \n\t"
    "shll.ph         %[temp1],     %[temp1],       8         \n\t"
    "shll.ph         %[temp2],     %[temp2],       8         \n\t"
    "shra.ph         %[temp0],     %[temp0],       8         \n\t"
    "shra.ph         %[temp1],     %[temp1],       8         \n\t"
    "shra.ph         %[temp2],     %[temp2],       8         \n\t"
  "0:                                                        \n\t"
    "lw              %[argb],      0(%[data])                \n\t"
    "lw              %[argb1],     4(%[data])                \n\t"
    "lhu             %[new_red],   2(%[data])                \n\t"
    "lhu             %[new_red1],  6(%[data])                \n\t"
    "precrq.qb.ph    %[temp3],     %[argb],        %[argb1]  \n\t"
    "precr.qb.ph     %[temp4],     %[argb],        %[argb1]  \n\t"
    "preceu.ph.qbra  %[temp3],     %[temp3]                  \n\t"
    "preceu.ph.qbla  %[temp4],     %[temp4]                  \n\t"
    "shll.ph         %[temp3],     %[temp3],       8         \n\t"
    "shll.ph         %[temp4],     %[temp4],       8         \n\t"
    "shra.ph         %[temp3],     %[temp3],       8         \n\t"
    "shra.ph         %[temp4],     %[temp4],       8         \n\t"
    "mul.ph          %[temp5],     %[temp3],       %[temp0]  \n\t"
    "mul.ph          %[temp3],     %[temp3],       %[temp1]  \n\t"
    "mul.ph          %[temp4],     %[temp4],       %[temp2]  \n\t"
    "addiu           %[data],      %[data],        8         \n\t"
    "ins             %[new_red1],  %[new_red],     16,   16  \n\t"
    "ins             %[argb1],     %[argb],        16,   16  \n\t"
    "shra.ph         %[temp5],     %[temp5],       5         \n\t"
    "shra.ph         %[temp3],     %[temp3],       5         \n\t"
    "shra.ph         %[temp4],     %[temp4],       5         \n\t"
    "subu.ph         %[new_red1],  %[new_red1],    %[temp5]  \n\t"
    "subu.ph         %[argb1],     %[argb1],       %[temp3]  \n\t"
    "preceu.ph.qbra  %[temp5],     %[new_red1]               \n\t"
    "subu.ph         %[argb1],     %[argb1],       %[temp4]  \n\t"
    "preceu.ph.qbra  %[temp3],     %[argb1]                  \n\t"
    "sb              %[temp5],     -2(%[data])               \n\t"
    "sb              %[temp3],     -4(%[data])               \n\t"
    "sra             %[temp5],     %[temp5],       16        \n\t"
    "sra             %[temp3],     %[temp3],       16        \n\t"
    "sb              %[temp5],     -6(%[data])               \n\t"
    "bne             %[data],      %[p_loop_end],  0b        \n\t"
    " sb             %[temp3],     -8(%[data])               \n\t"
  "1:                                                        \n\t"
    ".set            pop                                     \n\t"
    : [temp0]"=&r"(temp0), [temp1]"=&r"(temp1), [temp2]"=&r"(temp2),
      [temp3]"=&r"(temp3), [temp4]"=&r"(temp4), [temp5]"=&r"(temp5),
      [new_red1]"=&r"(new_red1), [new_red]"=&r"(new_red),
      [argb]"=&r"(argb), [argb1]"=&r"(argb1), [data]"+&r"(data)
    : [G_to_R]"r"(G_to_R), [R_to_B]"r"(R_to_B),
      [G_to_B]"r"(G_to_B), [p_loop_end]"r"(p_loop_end)
    : "memory", "hi", "lo"
  );

  if (num_pixels & 1) {
    const uint32_t argb_ = data[0];
    const uint32_t green = argb_ >> 8;
    const uint32_t red = argb_ >> 16;
    uint32_t new_blue = argb_;
    new_red = red;
    new_red -= ColorTransformDelta(m->green_to_red_, green);
    new_red &= 0xff;
    new_blue -= ColorTransformDelta(m->green_to_blue_, green);
    new_blue -= ColorTransformDelta(m->red_to_blue_, red);
    new_blue &= 0xff;
    data[0] = (argb_ & 0xff00ff00u) | (new_red << 16) | (new_blue);
  }
}

static WEBP_INLINE uint8_t TransformColorBlue(uint8_t green_to_blue,
                                              uint8_t red_to_blue,
                                              uint32_t argb) {
  const uint32_t green = argb >> 8;
  const uint32_t red = argb >> 16;
  uint8_t new_blue = argb;
  new_blue -= ColorTransformDelta(green_to_blue, green);
  new_blue -= ColorTransformDelta(red_to_blue, red);
  return (new_blue & 0xff);
}

static void CollectColorBlueTransforms(const uint32_t* argb, int stride,
                                       int tile_width, int tile_height,
                                       int green_to_blue, int red_to_blue,
                                       int histo[]) {
  const int rtb = (red_to_blue << 16) | (red_to_blue & 0xffff);
  const int gtb = (green_to_blue << 16) | (green_to_blue & 0xffff);
  const uint32_t mask = 0xff00ffu;
  while (tile_height-- > 0) {
    int x;
    const uint32_t* p_argb = argb;
    argb += stride;
    for (x = 0; x < (tile_width >> 1); ++x) {
      int temp0, temp1, temp2, temp3, temp4, temp5, temp6;
      __asm__ volatile (
        "lw           %[temp0],  0(%[p_argb])             \n\t"
        "lw           %[temp1],  4(%[p_argb])             \n\t"
        "precr.qb.ph  %[temp2],  %[temp0],  %[temp1]      \n\t"
        "ins          %[temp1],  %[temp0],  16,    16     \n\t"
        "shra.ph      %[temp2],  %[temp2],  8             \n\t"
        "shra.ph      %[temp3],  %[temp1],  8             \n\t"
        "mul.ph       %[temp5],  %[temp2],  %[rtb]        \n\t"
        "mul.ph       %[temp6],  %[temp3],  %[gtb]        \n\t"
        "and          %[temp4],  %[temp1],  %[mask]       \n\t"
        "addiu        %[p_argb], %[p_argb], 8             \n\t"
        "shra.ph      %[temp5],  %[temp5],  5             \n\t"
        "shra.ph      %[temp6],  %[temp6],  5             \n\t"
        "subu.qb      %[temp2],  %[temp4],  %[temp5]      \n\t"
        "subu.qb      %[temp2],  %[temp2],  %[temp6]      \n\t"
        : [p_argb]"+&r"(p_argb), [temp0]"=&r"(temp0), [temp1]"=&r"(temp1),
          [temp2]"=&r"(temp2), [temp3]"=&r"(temp3), [temp4]"=&r"(temp4),
          [temp5]"=&r"(temp5), [temp6]"=&r"(temp6)
        : [rtb]"r"(rtb), [gtb]"r"(gtb), [mask]"r"(mask)
        : "memory", "hi", "lo"
      );
      ++histo[(uint8_t)(temp2 >> 16)];
      ++histo[(uint8_t)temp2];
    }
    if (tile_width & 1) {
      ++histo[TransformColorBlue(green_to_blue, red_to_blue, *p_argb)];
    }
  }
}

static WEBP_INLINE uint8_t TransformColorRed(uint8_t green_to_red,
                                             uint32_t argb) {
  const uint32_t green = argb >> 8;
  uint32_t new_red = argb >> 16;
  new_red -= ColorTransformDelta(green_to_red, green);
  return (new_red & 0xff);
}

static void CollectColorRedTransforms(const uint32_t* argb, int stride,
                                      int tile_width, int tile_height,
                                      int green_to_red, int histo[]) {
  const int gtr = (green_to_red << 16) | (green_to_red & 0xffff);
  while (tile_height-- > 0) {
    int x;
    const uint32_t* p_argb = argb;
    argb += stride;
    for (x = 0; x < (tile_width >> 1); ++x) {
      int temp0, temp1, temp2, temp3, temp4;
      __asm__ volatile (
        "lw           %[temp0],  0(%[p_argb])             \n\t"
        "lw           %[temp1],  4(%[p_argb])             \n\t"
        "precrq.ph.w  %[temp4],  %[temp0],  %[temp1]      \n\t"
        "ins          %[temp1],  %[temp0],  16,    16     \n\t"
        "shra.ph      %[temp3],  %[temp1],  8             \n\t"
        "mul.ph       %[temp2],  %[temp3],  %[gtr]        \n\t"
        "addiu        %[p_argb], %[p_argb], 8             \n\t"
        "shra.ph      %[temp2],  %[temp2],  5             \n\t"
        "subu.qb      %[temp2],  %[temp4],  %[temp2]      \n\t"
        : [p_argb]"+&r"(p_argb), [temp0]"=&r"(temp0), [temp1]"=&r"(temp1),
          [temp2]"=&r"(temp2), [temp3]"=&r"(temp3), [temp4]"=&r"(temp4)
        : [gtr]"r"(gtr)
        : "memory", "hi", "lo"
      );
      ++histo[(uint8_t)(temp2 >> 16)];
      ++histo[(uint8_t)temp2];
    }
    if (tile_width & 1) {
      ++histo[TransformColorRed(green_to_red, *p_argb)];
    }
  }
}

// Add green to blue and red channels (i.e. perform the inverse transform of
// 'subtract green').
static void AddGreenToBlueAndRed(uint32_t* data, int num_pixels) {
  uint32_t temp0, temp1, temp2, temp3, temp4, temp5, temp6, temp7;
  uint32_t* const p_loop1_end = data + (num_pixels & ~3);
  uint32_t* const p_loop2_end = data + num_pixels;
  __asm__ volatile (
    ".set       push                                          \n\t"
    ".set       noreorder                                     \n\t"
    "beq        %[data],         %[p_loop1_end],     3f       \n\t"
    " nop                                                     \n\t"
  "0:                                                         \n\t"
    "lw         %[temp0],        0(%[data])                   \n\t"
    "lw         %[temp1],        4(%[data])                   \n\t"
    "lw         %[temp2],        8(%[data])                   \n\t"
    "lw         %[temp3],        12(%[data])                  \n\t"
    "ext        %[temp4],        %[temp0],           8,    8  \n\t"
    "ext        %[temp5],        %[temp1],           8,    8  \n\t"
    "ext        %[temp6],        %[temp2],           8,    8  \n\t"
    "ext        %[temp7],        %[temp3],           8,    8  \n\t"
    "addiu      %[data],         %[data],            16       \n\t"
    "replv.ph   %[temp4],        %[temp4]                     \n\t"
    "replv.ph   %[temp5],        %[temp5]                     \n\t"
    "replv.ph   %[temp6],        %[temp6]                     \n\t"
    "replv.ph   %[temp7],        %[temp7]                     \n\t"
    "addu.qb    %[temp0],        %[temp0],           %[temp4] \n\t"
    "addu.qb    %[temp1],        %[temp1],           %[temp5] \n\t"
    "addu.qb    %[temp2],        %[temp2],           %[temp6] \n\t"
    "addu.qb    %[temp3],        %[temp3],           %[temp7] \n\t"
    "sw         %[temp0],        -16(%[data])                 \n\t"
    "sw         %[temp1],        -12(%[data])                 \n\t"
    "sw         %[temp2],        -8(%[data])                  \n\t"
    "bne        %[data],         %[p_loop1_end],     0b       \n\t"
    " sw        %[temp3],        -4(%[data])                  \n\t"
  "3:                                                         \n\t"
    "beq        %[data],         %[p_loop2_end],     2f       \n\t"
    " nop                                                     \n\t"
  "1:                                                         \n\t"
    "lw         %[temp0],        0(%[data])                   \n\t"
    "addiu      %[data],         %[data],            4        \n\t"
    "ext        %[temp4],        %[temp0],           8,    8  \n\t"
    "replv.ph   %[temp4],        %[temp4]                     \n\t"
    "addu.qb    %[temp0],        %[temp0],           %[temp4] \n\t"
    "bne        %[data],         %[p_loop2_end],     1b       \n\t"
    " sw        %[temp0],        -4(%[data])                  \n\t"
  "2:                                                         \n\t"
    ".set       pop                                           \n\t"
    : [data]"+&r"(data), [temp0]"=&r"(temp0), [temp1]"=&r"(temp1),
      [temp2]"=&r"(temp2), [temp3]"=&r"(temp3), [temp4]"=&r"(temp4),
      [temp5]"=&r"(temp5), [temp6]"=&r"(temp6), [temp7]"=&r"(temp7)
    : [p_loop1_end]"r"(p_loop1_end), [p_loop2_end]"r"(p_loop2_end)
    : "memory"
  );
}

static void TransformColorInverse(const VP8LMultipliers* const m,
                                  uint32_t* data, int num_pixels) {
  int temp0, temp1, temp2, temp3, temp4, temp5;
  uint32_t argb, argb1, new_red;
  const uint32_t G_to_R = m->green_to_red_;
  const uint32_t G_to_B = m->green_to_blue_;
  const uint32_t R_to_B = m->red_to_blue_;
  uint32_t* const p_loop_end = data + (num_pixels & ~1);
  __asm__ volatile (
    ".set            push                                    \n\t"
    ".set            noreorder                               \n\t"
    "beq             %[data],      %[p_loop_end],  1f        \n\t"
    " nop                                                    \n\t"
    "replv.ph        %[temp0],     %[G_to_R]                 \n\t"
    "replv.ph        %[temp1],     %[G_to_B]                 \n\t"
    "replv.ph        %[temp2],     %[R_to_B]                 \n\t"
    "shll.ph         %[temp0],     %[temp0],       8         \n\t"
    "shll.ph         %[temp1],     %[temp1],       8         \n\t"
    "shll.ph         %[temp2],     %[temp2],       8         \n\t"
    "shra.ph         %[temp0],     %[temp0],       8         \n\t"
    "shra.ph         %[temp1],     %[temp1],       8         \n\t"
    "shra.ph         %[temp2],     %[temp2],       8         \n\t"
  "0:                                                        \n\t"
    "lw              %[argb],      0(%[data])                \n\t"
    "lw              %[argb1],     4(%[data])                \n\t"
    "addiu           %[data],      %[data],        8         \n\t"
    "precrq.qb.ph    %[temp3],     %[argb],        %[argb1]  \n\t"
    "preceu.ph.qbra  %[temp3],     %[temp3]                  \n\t"
    "shll.ph         %[temp3],     %[temp3],       8         \n\t"
    "shra.ph         %[temp3],     %[temp3],       8         \n\t"
    "mul.ph          %[temp5],     %[temp3],       %[temp0]  \n\t"
    "mul.ph          %[temp3],     %[temp3],       %[temp1]  \n\t"
    "precrq.ph.w     %[new_red],   %[argb],        %[argb1]  \n\t"
    "ins             %[argb1],     %[argb],        16,   16  \n\t"
    "shra.ph         %[temp5],     %[temp5],       5         \n\t"
    "shra.ph         %[temp3],     %[temp3],       5         \n\t"
    "addu.ph         %[new_red],   %[new_red],     %[temp5]  \n\t"
    "addu.ph         %[argb1],     %[argb1],       %[temp3]  \n\t"
    "preceu.ph.qbra  %[temp5],     %[new_red]                \n\t"
    "shll.ph         %[temp4],     %[temp5],       8         \n\t"
    "shra.ph         %[temp4],     %[temp4],       8         \n\t"
    "mul.ph          %[temp4],     %[temp4],       %[temp2]  \n\t"
    "sb              %[temp5],     -2(%[data])               \n\t"
    "sra             %[temp5],     %[temp5],       16        \n\t"
    "shra.ph         %[temp4],     %[temp4],       5         \n\t"
    "addu.ph         %[argb1],     %[argb1],       %[temp4]  \n\t"
    "preceu.ph.qbra  %[temp3],     %[argb1]                  \n\t"
    "sb              %[temp5],     -6(%[data])               \n\t"
    "sb              %[temp3],     -4(%[data])               \n\t"
    "sra             %[temp3],     %[temp3],       16        \n\t"
    "bne             %[data],      %[p_loop_end],  0b        \n\t"
    " sb             %[temp3],     -8(%[data])               \n\t"
  "1:                                                        \n\t"
    ".set            pop                                     \n\t"
    : [temp0]"=&r"(temp0), [temp1]"=&r"(temp1), [temp2]"=&r"(temp2),
      [temp3]"=&r"(temp3), [temp4]"=&r"(temp4), [temp5]"=&r"(temp5),
      [new_red]"=&r"(new_red), [argb]"=&r"(argb),
      [argb1]"=&r"(argb1), [data]"+&r"(data)
    : [G_to_R]"r"(G_to_R), [R_to_B]"r"(R_to_B),
      [G_to_B]"r"(G_to_B), [p_loop_end]"r"(p_loop_end)
    : "memory", "hi", "lo"
  );

  // Fall-back to C-version for left-overs.
  if (num_pixels & 1) VP8LTransformColorInverse_C(m, data, 1);
}

static void ConvertBGRAToRGB(const uint32_t* src,
                             int num_pixels, uint8_t* dst) {
  int temp0, temp1, temp2, temp3;
  const uint32_t* const p_loop1_end = src + (num_pixels & ~3);
  const uint32_t* const p_loop2_end = src + num_pixels;
  __asm__ volatile (
    ".set       push                                       \n\t"
    ".set       noreorder                                  \n\t"
    "beq        %[src],      %[p_loop1_end],    3f         \n\t"
    " nop                                                  \n\t"
  "0:                                                      \n\t"
    "lw         %[temp3],    12(%[src])                    \n\t"
    "lw         %[temp2],    8(%[src])                     \n\t"
    "lw         %[temp1],    4(%[src])                     \n\t"
    "lw         %[temp0],    0(%[src])                     \n\t"
    "ins        %[temp3],    %[temp2],          24,   8    \n\t"
    "sll        %[temp2],    %[temp2],          8          \n\t"
    "rotr       %[temp3],    %[temp3],          16         \n\t"
    "ins        %[temp2],    %[temp1],          0,    16   \n\t"
    "sll        %[temp1],    %[temp1],          8          \n\t"
    "wsbh       %[temp3],    %[temp3]                      \n\t"
    "balign     %[temp0],    %[temp1],          1          \n\t"
    "wsbh       %[temp2],    %[temp2]                      \n\t"
    "wsbh       %[temp0],    %[temp0]                      \n\t"
    "usw        %[temp3],    8(%[dst])                     \n\t"
    "rotr       %[temp0],    %[temp0],          16         \n\t"
    "usw        %[temp2],    4(%[dst])                     \n\t"
    "addiu      %[src],      %[src],            16         \n\t"
    "usw        %[temp0],    0(%[dst])                     \n\t"
    "bne        %[src],      %[p_loop1_end],    0b         \n\t"
    " addiu     %[dst],      %[dst],            12         \n\t"
  "3:                                                      \n\t"
    "beq        %[src],      %[p_loop2_end],    2f         \n\t"
    " nop                                                  \n\t"
  "1:                                                      \n\t"
    "lw         %[temp0],    0(%[src])                     \n\t"
    "addiu      %[src],      %[src],            4          \n\t"
    "wsbh       %[temp1],    %[temp0]                      \n\t"
    "addiu      %[dst],      %[dst],            3          \n\t"
    "ush        %[temp1],    -2(%[dst])                    \n\t"
    "sra        %[temp0],    %[temp0],          16         \n\t"
    "bne        %[src],      %[p_loop2_end],    1b         \n\t"
    " sb        %[temp0],    -3(%[dst])                    \n\t"
  "2:                                                      \n\t"
    ".set       pop                                        \n\t"
    : [temp0]"=&r"(temp0), [temp1]"=&r"(temp1), [temp2]"=&r"(temp2),
      [temp3]"=&r"(temp3), [dst]"+&r"(dst), [src]"+&r"(src)
    : [p_loop1_end]"r"(p_loop1_end), [p_loop2_end]"r"(p_loop2_end)
    : "memory"
  );
}

static void ConvertBGRAToRGBA(const uint32_t* src,
                              int num_pixels, uint8_t* dst) {
  int temp0, temp1, temp2, temp3;
  const uint32_t* const p_loop1_end = src + (num_pixels & ~3);
  const uint32_t* const p_loop2_end = src + num_pixels;
  __asm__ volatile (
    ".set       push                                       \n\t"
    ".set       noreorder                                  \n\t"
    "beq        %[src],      %[p_loop1_end],    3f         \n\t"
    " nop                                                  \n\t"
  "0:                                                      \n\t"
    "lw         %[temp0],    0(%[src])                     \n\t"
    "lw         %[temp1],    4(%[src])                     \n\t"
    "lw         %[temp2],    8(%[src])                     \n\t"
    "lw         %[temp3],    12(%[src])                    \n\t"
    "wsbh       %[temp0],    %[temp0]                      \n\t"
    "wsbh       %[temp1],    %[temp1]                      \n\t"
    "wsbh       %[temp2],    %[temp2]                      \n\t"
    "wsbh       %[temp3],    %[temp3]                      \n\t"
    "addiu      %[src],      %[src],            16         \n\t"
    "balign     %[temp0],    %[temp0],          1          \n\t"
    "balign     %[temp1],    %[temp1],          1          \n\t"
    "balign     %[temp2],    %[temp2],          1          \n\t"
    "balign     %[temp3],    %[temp3],          1          \n\t"
    "usw        %[temp0],    0(%[dst])                     \n\t"
    "usw        %[temp1],    4(%[dst])                     \n\t"
    "usw        %[temp2],    8(%[dst])                     \n\t"
    "usw        %[temp3],    12(%[dst])                    \n\t"
    "bne        %[src],      %[p_loop1_end],    0b         \n\t"
    " addiu     %[dst],      %[dst],            16         \n\t"
  "3:                                                      \n\t"
    "beq        %[src],      %[p_loop2_end],    2f         \n\t"
    " nop                                                  \n\t"
  "1:                                                      \n\t"
    "lw         %[temp0],    0(%[src])                     \n\t"
    "wsbh       %[temp0],    %[temp0]                      \n\t"
    "addiu      %[src],      %[src],            4          \n\t"
    "balign     %[temp0],    %[temp0],          1          \n\t"
    "usw        %[temp0],    0(%[dst])                     \n\t"
    "bne        %[src],      %[p_loop2_end],    1b         \n\t"
    " addiu     %[dst],      %[dst],            4          \n\t"
  "2:                                                      \n\t"
    ".set       pop                                        \n\t"
    : [temp0]"=&r"(temp0), [temp1]"=&r"(temp1), [temp2]"=&r"(temp2),
      [temp3]"=&r"(temp3), [dst]"+&r"(dst), [src]"+&r"(src)
    : [p_loop1_end]"r"(p_loop1_end), [p_loop2_end]"r"(p_loop2_end)
    : "memory"
  );
}

static void ConvertBGRAToRGBA4444(const uint32_t* src,
                                  int num_pixels, uint8_t* dst) {
  int temp0, temp1, temp2, temp3, temp4, temp5;
  const uint32_t* const p_loop1_end = src + (num_pixels & ~3);
  const uint32_t* const p_loop2_end = src + num_pixels;
  __asm__ volatile (
    ".set           push                                       \n\t"
    ".set           noreorder                                  \n\t"
    "beq            %[src],      %[p_loop1_end],    3f         \n\t"
    " nop                                                      \n\t"
  "0:                                                          \n\t"
    "lw             %[temp0],    0(%[src])                     \n\t"
    "lw             %[temp1],    4(%[src])                     \n\t"
    "lw             %[temp2],    8(%[src])                     \n\t"
    "lw             %[temp3],    12(%[src])                    \n\t"
    "ext            %[temp4],    %[temp0],          28,   4    \n\t"
    "ext            %[temp5],    %[temp0],          12,   4    \n\t"
    "ins            %[temp0],    %[temp4],          0,    4    \n\t"
    "ext            %[temp4],    %[temp1],          28,   4    \n\t"
    "ins            %[temp0],    %[temp5],          16,   4    \n\t"
    "ext            %[temp5],    %[temp1],          12,   4    \n\t"
    "ins            %[temp1],    %[temp4],          0,    4    \n\t"
    "ext            %[temp4],    %[temp2],          28,   4    \n\t"
    "ins            %[temp1],    %[temp5],          16,   4    \n\t"
    "ext            %[temp5],    %[temp2],          12,   4    \n\t"
    "ins            %[temp2],    %[temp4],          0,    4    \n\t"
    "ext            %[temp4],    %[temp3],          28,   4    \n\t"
    "ins            %[temp2],    %[temp5],          16,   4    \n\t"
    "ext            %[temp5],    %[temp3],          12,   4    \n\t"
    "ins            %[temp3],    %[temp4],          0,    4    \n\t"
    "precr.qb.ph    %[temp1],    %[temp1],          %[temp0]   \n\t"
    "ins            %[temp3],    %[temp5],          16,   4    \n\t"
    "addiu          %[src],      %[src],            16         \n\t"
    "precr.qb.ph    %[temp3],    %[temp3],          %[temp2]   \n\t"
#ifdef WEBP_SWAP_16BIT_CSP
    "usw            %[temp1],    0(%[dst])                     \n\t"
    "usw            %[temp3],    4(%[dst])                     \n\t"
#else
    "wsbh           %[temp1],    %[temp1]                      \n\t"
    "wsbh           %[temp3],    %[temp3]                      \n\t"
    "usw            %[temp1],    0(%[dst])                     \n\t"
    "usw            %[temp3],    4(%[dst])                     \n\t"
#endif
    "bne            %[src],      %[p_loop1_end],    0b         \n\t"
    " addiu         %[dst],      %[dst],            8          \n\t"
  "3:                                                          \n\t"
    "beq            %[src],      %[p_loop2_end],    2f         \n\t"
    " nop                                                      \n\t"
  "1:                                                          \n\t"
    "lw             %[temp0],    0(%[src])                     \n\t"
    "ext            %[temp4],    %[temp0],          28,   4    \n\t"
    "ext            %[temp5],    %[temp0],          12,   4    \n\t"
    "ins            %[temp0],    %[temp4],          0,    4    \n\t"
    "ins            %[temp0],    %[temp5],          16,   4    \n\t"
    "addiu          %[src],      %[src],            4          \n\t"
    "precr.qb.ph    %[temp0],    %[temp0],          %[temp0]   \n\t"
#ifdef WEBP_SWAP_16BIT_CSP
    "ush            %[temp0],    0(%[dst])                     \n\t"
#else
    "wsbh           %[temp0],    %[temp0]                      \n\t"
    "ush            %[temp0],    0(%[dst])                     \n\t"
#endif
    "bne            %[src],      %[p_loop2_end],    1b         \n\t"
    " addiu         %[dst],      %[dst],            2          \n\t"
  "2:                                                          \n\t"
    ".set           pop                                        \n\t"
    : [temp0]"=&r"(temp0), [temp1]"=&r"(temp1), [temp2]"=&r"(temp2),
      [temp3]"=&r"(temp3), [temp4]"=&r"(temp4), [temp5]"=&r"(temp5),
      [dst]"+&r"(dst), [src]"+&r"(src)
    : [p_loop1_end]"r"(p_loop1_end), [p_loop2_end]"r"(p_loop2_end)
    : "memory"
  );
}

static void ConvertBGRAToRGB565(const uint32_t* src,
                                int num_pixels, uint8_t* dst) {
  int temp0, temp1, temp2, temp3, temp4, temp5;
  const uint32_t* const p_loop1_end = src + (num_pixels & ~3);
  const uint32_t* const p_loop2_end = src + num_pixels;
  __asm__ volatile (
    ".set           push                                       \n\t"
    ".set           noreorder                                  \n\t"
    "beq            %[src],      %[p_loop1_end],    3f         \n\t"
    " nop                                                      \n\t"
  "0:                                                          \n\t"
    "lw             %[temp0],    0(%[src])                     \n\t"
    "lw             %[temp1],    4(%[src])                     \n\t"
    "lw             %[temp2],    8(%[src])                     \n\t"
    "lw             %[temp3],    12(%[src])                    \n\t"
    "ext            %[temp4],    %[temp0],          8,    16   \n\t"
    "ext            %[temp5],    %[temp0],          5,    11   \n\t"
    "ext            %[temp0],    %[temp0],          3,    5    \n\t"
    "ins            %[temp4],    %[temp5],          0,    11   \n\t"
    "ext            %[temp5],    %[temp1],          5,    11   \n\t"
    "ins            %[temp4],    %[temp0],          0,    5    \n\t"
    "ext            %[temp0],    %[temp1],          8,    16   \n\t"
    "ext            %[temp1],    %[temp1],          3,    5    \n\t"
    "ins            %[temp0],    %[temp5],          0,    11   \n\t"
    "ext            %[temp5],    %[temp2],          5,    11   \n\t"
    "ins            %[temp0],    %[temp1],          0,    5    \n\t"
    "ext            %[temp1],    %[temp2],          8,    16   \n\t"
    "ext            %[temp2],    %[temp2],          3,    5    \n\t"
    "ins            %[temp1],    %[temp5],          0,    11   \n\t"
    "ext            %[temp5],    %[temp3],          5,    11   \n\t"
    "ins            %[temp1],    %[temp2],          0,    5    \n\t"
    "ext            %[temp2],    %[temp3],          8,    16   \n\t"
    "ext            %[temp3],    %[temp3],          3,    5    \n\t"
    "ins            %[temp2],    %[temp5],          0,    11   \n\t"
    "append         %[temp0],    %[temp4],          16         \n\t"
    "ins            %[temp2],    %[temp3],          0,    5    \n\t"
    "addiu          %[src],      %[src],            16         \n\t"
    "append         %[temp2],    %[temp1],          16         \n\t"
#ifdef WEBP_SWAP_16BIT_CSP
    "usw            %[temp0],    0(%[dst])                     \n\t"
    "usw            %[temp2],    4(%[dst])                     \n\t"
#else
    "wsbh           %[temp0],    %[temp0]                      \n\t"
    "wsbh           %[temp2],    %[temp2]                      \n\t"
    "usw            %[temp0],    0(%[dst])                     \n\t"
    "usw            %[temp2],    4(%[dst])                     \n\t"
#endif
    "bne            %[src],      %[p_loop1_end],    0b         \n\t"
    " addiu         %[dst],      %[dst],            8          \n\t"
  "3:                                                          \n\t"
    "beq            %[src],      %[p_loop2_end],    2f         \n\t"
    " nop                                                      \n\t"
  "1:                                                          \n\t"
    "lw             %[temp0],    0(%[src])                     \n\t"
    "ext            %[temp4],    %[temp0],          8,    16   \n\t"
    "ext            %[temp5],    %[temp0],          5,    11   \n\t"
    "ext            %[temp0],    %[temp0],          3,    5    \n\t"
    "ins            %[temp4],    %[temp5],          0,    11   \n\t"
    "addiu          %[src],      %[src],            4          \n\t"
    "ins            %[temp4],    %[temp0],          0,    5    \n\t"
#ifdef WEBP_SWAP_16BIT_CSP
    "ush            %[temp4],    0(%[dst])                     \n\t"
#else
    "wsbh           %[temp4],    %[temp4]                      \n\t"
    "ush            %[temp4],    0(%[dst])                     \n\t"
#endif
    "bne            %[src],      %[p_loop2_end],    1b         \n\t"
    " addiu         %[dst],      %[dst],            2          \n\t"
  "2:                                                          \n\t"
    ".set           pop                                        \n\t"
    : [temp0]"=&r"(temp0), [temp1]"=&r"(temp1), [temp2]"=&r"(temp2),
      [temp3]"=&r"(temp3), [temp4]"=&r"(temp4), [temp5]"=&r"(temp5),
      [dst]"+&r"(dst), [src]"+&r"(src)
    : [p_loop1_end]"r"(p_loop1_end), [p_loop2_end]"r"(p_loop2_end)
    : "memory"
  );
}

static void ConvertBGRAToBGR(const uint32_t* src,
                             int num_pixels, uint8_t* dst) {
  int temp0, temp1, temp2, temp3;
  const uint32_t* const p_loop1_end = src + (num_pixels & ~3);
  const uint32_t* const p_loop2_end = src + num_pixels;
  __asm__ volatile (
    ".set       push                                         \n\t"
    ".set       noreorder                                    \n\t"
    "beq        %[src],      %[p_loop1_end],    3f           \n\t"
    " nop                                                    \n\t"
  "0:                                                        \n\t"
    "lw         %[temp0],    0(%[src])                       \n\t"
    "lw         %[temp1],    4(%[src])                       \n\t"
    "lw         %[temp2],    8(%[src])                       \n\t"
    "lw         %[temp3],    12(%[src])                      \n\t"
    "ins        %[temp0],    %[temp1],          24,    8     \n\t"
    "sra        %[temp1],    %[temp1],          8            \n\t"
    "ins        %[temp1],    %[temp2],          16,    16    \n\t"
    "sll        %[temp2],    %[temp2],          8            \n\t"
    "balign     %[temp3],    %[temp2],          1            \n\t"
    "addiu      %[src],      %[src],            16           \n\t"
    "usw        %[temp0],    0(%[dst])                       \n\t"
    "usw        %[temp1],    4(%[dst])                       \n\t"
    "usw        %[temp3],    8(%[dst])                       \n\t"
    "bne        %[src],      %[p_loop1_end],    0b           \n\t"
    " addiu     %[dst],      %[dst],            12           \n\t"
  "3:                                                        \n\t"
    "beq        %[src],      %[p_loop2_end],    2f           \n\t"
    " nop                                                    \n\t"
  "1:                                                        \n\t"
    "lw         %[temp0],    0(%[src])                       \n\t"
    "addiu      %[src],      %[src],            4            \n\t"
    "addiu      %[dst],      %[dst],            3            \n\t"
    "ush        %[temp0],    -3(%[dst])                      \n\t"
    "sra        %[temp0],    %[temp0],          16           \n\t"
    "bne        %[src],      %[p_loop2_end],    1b           \n\t"
    " sb        %[temp0],    -1(%[dst])                      \n\t"
  "2:                                                        \n\t"
    ".set       pop                                          \n\t"
    : [temp0]"=&r"(temp0), [temp1]"=&r"(temp1), [temp2]"=&r"(temp2),
      [temp3]"=&r"(temp3), [dst]"+&r"(dst), [src]"+&r"(src)
    : [p_loop1_end]"r"(p_loop1_end), [p_loop2_end]"r"(p_loop2_end)
    : "memory"
  );
}

#endif  // WEBP_USE_MIPS_DSP_R2

//------------------------------------------------------------------------------

extern void VP8LDspInitMIPSdspR2(void);

WEBP_TSAN_IGNORE_FUNCTION void VP8LDspInitMIPSdspR2(void) {
#if defined(WEBP_USE_MIPS_DSP_R2)
  VP8LMapColor32b = MapARGB;
  VP8LMapColor8b = MapAlpha;
  VP8LPredictors[5] = Predictor5;
  VP8LPredictors[6] = Predictor6;
  VP8LPredictors[7] = Predictor7;
  VP8LPredictors[8] = Predictor8;
  VP8LPredictors[9] = Predictor9;
  VP8LPredictors[10] = Predictor10;
  VP8LPredictors[11] = Predictor11;
  VP8LPredictors[12] = Predictor12;
  VP8LPredictors[13] = Predictor13;
  VP8LSubtractGreenFromBlueAndRed = SubtractGreenFromBlueAndRed;
  VP8LTransformColor = TransformColor;
  VP8LCollectColorBlueTransforms = CollectColorBlueTransforms;
  VP8LCollectColorRedTransforms = CollectColorRedTransforms;
  VP8LAddGreenToBlueAndRed = AddGreenToBlueAndRed;
  VP8LTransformColorInverse = TransformColorInverse;
  VP8LConvertBGRAToRGB = ConvertBGRAToRGB;
  VP8LConvertBGRAToRGBA = ConvertBGRAToRGBA;
  VP8LConvertBGRAToRGBA4444 = ConvertBGRAToRGBA4444;
  VP8LConvertBGRAToRGB565 = ConvertBGRAToRGB565;
  VP8LConvertBGRAToBGR = ConvertBGRAToBGR;
#endif  // WEBP_USE_MIPS_DSP_R2
}

//------------------------------------------------------------------------------
