/*
Copyright(c) 2016-2020 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#pragma once

//= INCLUDES ==============================
#include <vector>
#include <string>
#include "../../RHI/RHI_Definition.h"
#include "../../Core/Spartan_Definitions.h"
//=========================================

struct FIBITMAP;

namespace Spartan
{
    class Context;

    class SPARTAN_CLASS ImageImporter
    {
    public:
        ImageImporter(Context* context);
        ~ImageImporter();

        bool Load(const std::string& file_path, RHI_Texture* texture, bool generate_mipmaps = true);

    private:    
        bool GetBitsFromFibitmap(std::vector<std::byte>* data, FIBITMAP* bitmap, uint32_t width, uint32_t height, uint32_t channels) const;
        void GenerateMipmaps(FIBITMAP* bitmap, RHI_Texture* texture, uint32_t width, uint32_t height, uint32_t channels);
        FIBITMAP* ApplyBitmapCorrections(FIBITMAP* bitmap) const;
        FIBITMAP* _FreeImage_ConvertTo32Bits(FIBITMAP* bitmap) const;
        FIBITMAP* _FreeImage_Rescale(FIBITMAP* bitmap, uint32_t width, uint32_t height) const;

        Context* m_context = nullptr;
    };
}
