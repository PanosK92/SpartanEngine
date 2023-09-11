//
// Copyright (c) 2021 - 2022 Advanced Micro Devices, Inc. All rights reserved.
//
//-------------------------------------------------------------------------------------------------

#ifndef ADLX_STRUCTURES_H
#define ADLX_STRUCTURES_H
#pragma once
#include "ADLXDefines.h"

/**   @file */
//-------------------------------------------------------------------------------------------------
#pragma region ADLX_RGB
/**
 * @struct ADLX_RGB
 * @ingroup structuresVal
 * @ENG_START_DOX
 * @brief This structure contains RGB information.
 * @ENG_END_DOX
 */
typedef struct
{
    adlx_double gamutR;         /**< @ENG_START_DOX Red @ENG_END_DOX */
    adlx_double gamutG;         /**< @ENG_START_DOX Green @ENG_END_DOX */
    adlx_double gamutB;         /**< @ENG_START_DOX Blue @ENG_END_DOX */
} ADLX_RGB;
#pragma endregion ADLX_RGB

#pragma region ADLX_Point
/**
 *  @struct ADLX_Point
 *  @ingroup structuresVal
 *  @ENG_START_DOX
 *  @brief This structure contains information on driver point coordinates, and is used to store the driver-point coodinates for gamut, as well as white point.
 *  @ENG_END_DOX
 */
typedef struct
{
    adlx_int          x;           /**< @ENG_START_DOX The x coordinate. @ENG_END_DOX */
    adlx_int          y;           /**< @ENG_START_DOX The y coordinate. @ENG_END_DOX */
} ADLX_Point;
#pragma endregion ADLX_Point

#pragma region ADLX_GamutColorSpace
/**
 *  @struct ADLX_GamutColorSpace
 *  @ingroup structuresVal
 *  @ENG_START_DOX
 *  @brief This structure contains information on driver-supported gamut coordinates
 *  @ENG_END_DOX
 */
typedef struct
{
    ADLX_Point      red;                /**<  @ENG_START_DOX The red channel chromaticity coordinate. @ENG_END_DOX */
    ADLX_Point      green;              /**<  @ENG_START_DOX The green channel chromaticity coordinate. @ENG_END_DOX */
    ADLX_Point      blue;               /**<  @ENG_START_DOX The blue channel chromaticity coordinate. @ENG_END_DOX */
} ADLX_GamutColorSpace;
#pragma endregion ADLX_GamutColorSpace

#pragma region ADLX_GammaRamp
/**
 *  @struct ADLX_GammaRamp
 *  @ingroup structuresVal
 *  @ENG_START_DOX
 *  @brief This structure contains the display gamma ramp used to program the re-gamma LUT.
 *  @ENG_END_DOX
 */
typedef struct
{
    adlx_uint16  gamma[256 * 3];        /**< @ENG_START_DOX The gamma ramp is a buffer containing 256 triplets of adlx_uint16 values.
                                        Each triplet consists of red, green and blue values. @ENG_END_DOX */
} ADLX_GammaRamp;
#pragma endregion ADLX_GammaRamp

#pragma region ADLX_RegammaCoeff
/**
 *  @struct ADLX_RegammaCoeff
 *  @ingroup structuresVal
 *  @ENG_START_DOX
 *  @brief This structure contains information on driver-supported re-gamma coefficients used to build the re-gamma curve.
 *  @ENG_END_DOX
 */
typedef struct
{
    adlx_int    coefficientA0;          /**<  @ENG_START_DOX The a0 gamma coefficient. @ENG_END_DOX */
    adlx_int    coefficientA1;          /**<  @ENG_START_DOX The a1 gamma coefficient. @ENG_END_DOX */
    adlx_int    coefficientA2;          /**<  @ENG_START_DOX The a2 gamma coefficient. @ENG_END_DOX */
    adlx_int    coefficientA3;          /**<  @ENG_START_DOX The a3 gamma coefficient. @ENG_END_DOX */
    adlx_int    gamma;                  /**<  @ENG_START_DOX The regamma divider. @ENG_END_DOX */
} ADLX_RegammaCoeff;
#pragma endregion ADLX_RegammaCoeff

#pragma region ADLX_TimingInfo
/**
 *  @struct ADLX_TimingInfo
 *  @ingroup structuresVal
 *  @ENG_START_DOX
 *  @brief This structure contains display timing information.
 *  @ENG_END_DOX
 */
typedef struct
{
    adlx_int timingFlags;    /**< @ENG_START_DOX The detailed timing flag. @ENG_END_DOX */
    adlx_int hTotal;         /**< @ENG_START_DOX The total number of pixels that compose all scan lines during a horizontal sync. @ENG_END_DOX */
    adlx_int vTotal;         /**< @ENG_START_DOX The total number of vertical pixels permitted/processed per sync. @ENG_END_DOX */

    adlx_int hDisplay;       /**< @ENG_START_DOX The number of horizontal pixels within the active area. @ENG_END_DOX */
    adlx_int vDisplay;       /**< @ENG_START_DOX The number of vertical pixels within the active display area. @ENG_END_DOX */

    adlx_int hFrontPorch;    /**< @ENG_START_DOX The number of horizontal pixels between the end of the active area and the next sync. This is the distance between the right/bottom portion of the display up to the right/bottom portion of the actual image. @ENG_END_DOX */
    adlx_int vFrontPorch;    /**< @ENG_START_DOX The number of vertical pixels between the end of the active area and the next sync. This is the distance between the right/bottom portion of the display to the right/bottom portion of the actual image. @ENG_END_DOX */

    adlx_int hSyncWidth;    /**< @ENG_START_DOX The number of pixels that compose a scan line during a horizontal sync. @ENG_END_DOX */
    adlx_int vSyncWidth;    /**< @ENG_START_DOX The number of vertical pixels permitted/processed during a sync. @ENG_END_DOX */

    adlx_int hPolarity;    /**< @ENG_START_DOX The horizontal polarity of sync signals, 0 POSITIVE; 1 NEGATIVE. Positive makes the active signals high while negative makes the active signals low. @ENG_END_DOX */
    adlx_int vPolarity;    /**< @ENG_START_DOX The vertical polarity of sync signals, 0 POSITIVE; 1 NEGATIVE. Positive makes the active signals high while negative makes the active signals low. @ENG_END_DOX */
} ADLX_TimingInfo;

#pragma endregion ADLX_TimingInfo

#pragma region ADLX_CustomResolution
/** @struct ADLX_CustomResolution
 *  @ingroup structuresVal
 * @ENG_START_DOX
 *  @brief This structure contains information for custom resolution parameters on a given display.
 * @ENG_END_DOX
 *
 */
typedef struct
{
    adlx_int resWidth;                     /**< @ENG_START_DOX The resolution width. @ENG_END_DOX */
    adlx_int resHeight;                    /**< @ENG_START_DOX The resolution height. @ENG_END_DOX */
    adlx_int refreshRate;                  /**< @ENG_START_DOX The refresh rate. @ENG_END_DOX */
    ADLX_DISPLAY_SCAN_TYPE presentation;   /**< @ENG_START_DOX The presentation method, 0 PROGRESSIVE; 1 INTERLACED. @ENG_END_DOX */
    ADLX_TIMING_STANDARD timingStandard;   /**< @ENG_START_DOX The display timing standard. @ENG_END_DOX */
    adlx_long GPixelClock;                 /**< @ENG_START_DOX The speed at which pixels are transmitted within on a refresh cycle. @ENG_END_DOX */
    ADLX_TimingInfo detailedTiming;        /**< @ENG_START_DOX The detailed timing information. @ENG_END_DOX */
} ADLX_CustomResolution;

#pragma endregion ADLX_CustomResolution

#pragma region ADLX_IntRange
/** @struct ADLX_IntRange
 *  @ingroup structuresVal
 * @ENG_START_DOX
 *  @brief This structure contains information on the integer range.
 * @ENG_END_DOX
 *
 */
typedef struct
{
    adlx_int minValue;           /**< @ENG_START_DOX The minimum integer value. @ENG_END_DOX */
    adlx_int maxValue;           /**< @ENG_START_DOX The maximum integer value. @ENG_END_DOX */
    adlx_int step;               /**< @ENG_START_DOX The accepted integer range step. @ENG_END_DOX */
} ADLX_IntRange;
#pragma endregion ADLX_IntRange

#pragma region ADLX_UINT16_RGB
/** @struct ADLX_UINT16_RGB
 *  @ingroup structuresVal
 * @ENG_START_DOX
 *  @brief This structure contains UINT16 RGB information.
 * @ENG_END_DOX
 *
 */
typedef struct ADLX_UINT16_RGB
{
    adlx_uint16 red;        /**< @ENG_START_DOX Red @ENG_END_DOX */
    adlx_uint16 green;      /**< @ENG_START_DOX Green @ENG_END_DOX */
    adlx_uint16 blue;       /**< @ENG_START_DOX Blue @ENG_END_DOX */
} ADLX_UINT16_RGB;
#pragma endregion ADLX_UINT16_RGB

#pragma region ADLX_3DLUT_Data
/** @struct ADLX_3DLUT_Data
 *  @ingroup structuresVal
 * @ENG_START_DOX
 *  @brief This structure contains custom 3D LUT information.
 * @ENG_END_DOX
 *
 */

typedef struct ADLX_3DLUT_Data
{
    ADLX_UINT16_RGB data[MAX_USER_3DLUT_NUM_POINTS * MAX_USER_3DLUT_NUM_POINTS * MAX_USER_3DLUT_NUM_POINTS];     /**< @ENG_START_DOX The data is a buffer containing 17*17*17 triplets of @ref ADLX_UINT16_RGB values. Each triplet consists of red, green and blue values.
                                                 For 3D LUT data we use ushort 0 - 0xFFFF, data must be zero-padded to 16-bit. @ENG_END_DOX */
} ADLX_3DLUT_Data;
#pragma endregion ADLX_3DLUT_Data

#endif //ADLX_STRUCTURES_H
