// This file is part of the FidelityFX SDK.
//
// Copyright (C) 2023 Advanced Micro Devices, Inc.
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy 
// of this software and associated documentation files(the “Software”), to deal 
// in the Software without restriction, including without limitation the rights 
// to use, copy, modify, merge, publish, distribute, sublicense, and /or sell 
// copies of the Software, and to permit persons to whom the Software is 
// furnished to do so, subject to the following conditions :
// 
// The above copyright notice and this permission notice shall be included in 
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE 
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN 
// THE SOFTWARE.

FfxUInt32 FfxNumBlocksPerThreadGroup()
{
    return NumBlocksPerThreadGroup();
}

FfxUInt32 FfxNumThreadGroups()
{
    return NumThreadGroups();
}

FfxUInt32 FfxNumThreadGroupsWithAdditionalBlocks()
{
    return NumThreadGroupsWithAdditionalBlocks();
}

FfxUInt32 FfxNumReduceThreadgroupPerBin()
{
    return NumReduceThreadgroupPerBin();
}

FfxUInt32 FfxNumKeys()
{
    return NumKeys();
}

FfxUInt32 FfxLoadKey(FfxUInt32 index)
{
    return LoadSourceKey(index);
}

void FfxStoreKey(FfxUInt32 index, FfxUInt32 value)
{
    StoreDestKey(index, value);
}

FfxUInt32 FfxLoadPayload(FfxUInt32 index)
{
    return LoadSourcePayload(index);
}

void FfxStorePayload(FfxUInt32 index, FfxUInt32 value)
{
    StoreDestPayload(index, value);
}

FfxUInt32 FfxLoadSum(FfxUInt32 index)
{
    return LoadSumTable(index);
}

void FfxStoreSum(FfxUInt32 index, FfxUInt32 value)
{
    StoreSumTable(index, value);
}

void FfxStoreReduce(FfxUInt32 index, FfxUInt32 value)
{
    StoreReduceTable(index, value);
}

FfxUInt32 FfxLoadScanSource(FfxUInt32 index)
{
    return LoadScanSource(index);
}

void FfxStoreScanDest(FfxUInt32 index, FfxUInt32 value)
{
    StoreScanDest(index, value);
}

FfxUInt32 FfxLoadScanScratch(FfxUInt32 index)
{
    return LoadScanScratch(index);
}
