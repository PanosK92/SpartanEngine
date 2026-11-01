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

#include "ffx_api_matrix_helpers.h"

/// Converts a matrix from any layout/convention to the FFX canonical format.
/// The FFX canonical format is row-major layout with row-vector convention.
///
/// @param srcMatrix     Pointer to 16 floats representing the source matrix
/// @param srcLayout     Memory layout of the source matrix
/// @param srcVectorConv Vector convention of the source matrix
/// @return              Matrix converted to FFX canonical format
template<FfxApiMatrixLayout SrcMatrixLayout, FfxApiMatrixVectorConvention SrcMatrixVectorConvention>
static inline FfxApiMatrix4x4 ffxConvertToMatrix4x4(const float* srcMatrix)
{
    return ffxConvertToMatrix4x4(srcMatrix, SrcMatrixLayout, SrcMatrixVectorConvention);
}
