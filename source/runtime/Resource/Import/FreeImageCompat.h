#pragma once

#if defined(__linux__)

#ifndef FREEIMAGE_LIB
#define FREEIMAGE_LIB
#endif
#include <FreeImage/FreeImage.h>

extern "C" unsigned       FreeImage_GetBPP(FIBITMAP* dib);
extern "C" unsigned char* FreeImage_GetBits(FIBITMAP* dib);
extern "C" unsigned       FreeImage_GetWidth(FIBITMAP* dib);
extern "C" unsigned       FreeImage_GetHeight(FIBITMAP* dib);
extern "C" unsigned       FreeImage_GetPitch(FIBITMAP* dib);

inline int SwapRedBlue32(FIBITMAP* dib)
{
    if (FreeImage_GetBPP(dib) != 32)
        return 0;

    unsigned char* bits = FreeImage_GetBits(dib);
    unsigned width      = FreeImage_GetWidth(dib);
    unsigned height     = FreeImage_GetHeight(dib);
    unsigned pitch      = FreeImage_GetPitch(dib);

    for (unsigned y = 0; y < height; ++y)
    {
        unsigned char* row = bits + y * pitch;
        for (unsigned x = 0; x < width; ++x)
        {
            unsigned char tmp = row[x * 4 + 0];
            row[x * 4 + 0]    = row[x * 4 + 2];
            row[x * 4 + 2]    = tmp;
        }
    }

    return 1;
}

#endif
