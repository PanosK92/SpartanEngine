// This file is part of the FidelityFX SDK.
//
// Copyright (C) 2026 Advanced Micro Devices, Inc.
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#pragma once

#include <string.h>
#include "ffx_api_types.h"

/// Memory layout enumeration for matrix data.
/// This describes how the data is organized in memory.
enum FfxApiMatrixLayout
{
    FFX_API_MATRIX_ROW_MAJOR,          ///< Row-major memory layout. Elements are stored row-by-row. Common in DirectX/C arrays.
    FFX_API_MATRIX_COLUMN_MAJOR,       ///< Column-major memory layout. Elements are stored column-by-column. Common in OpenGL/HLSL.
};

/// Vector convention enumeration for matrix multiplication.
/// This describes whether vectors are treated as rows or columns in mathematical operations.
enum FfxApiMatrixVectorConvention
{
    FFX_API_MATRIX_ROW_VECTORS,        ///< Row vector convention: v * M (vector on left). Common in DirectX.
    FFX_API_MATRIX_COLUMN_VECTORS,     ///< Column vector convention: M * v (vector on right). Common in OpenGL/HLSL.
};

/// Converts a matrix from any layout/convention to the FFX canonical format.
/// The FFX canonical format is row-major layout with row-vector convention.
///
/// @param srcMatrix     Pointer to 16 floats representing the source matrix
/// @param srcLayout     Memory layout of the source matrix
/// @param srcVectorConv Vector convention of the source matrix
/// @return              Matrix converted to FFX canonical format
static inline FfxApiMatrix4x4 ffxConvertToMatrix4x4(
    const float* srcMatrix,
    enum FfxApiMatrixLayout srcLayout,
    enum FfxApiMatrixVectorConvention srcVectorConv)
{
    FfxApiMatrix4x4 result;
    
    // Transpose needed if exactly one of layout or convention differs from canonical.
    const bool needsTranspose = (srcLayout == FFX_API_MATRIX_ROW_MAJOR) != (srcVectorConv == FFX_API_MATRIX_ROW_VECTORS);
    
    if (needsTranspose)
    {
        for (uint32_t row = 0; row < 4; ++row)
        {
            result.rows[row].x = srcMatrix[4 * 0 + row];
            result.rows[row].y = srcMatrix[4 * 1 + row];
            result.rows[row].z = srcMatrix[4 * 2 + row];
            result.rows[row].w = srcMatrix[4 * 3 + row];
        }
    }
    else
    {
        memcpy(&result, srcMatrix, sizeof(FfxApiMatrix4x4));
    }
    
    return result;
}
