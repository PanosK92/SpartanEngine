/*
Copyright(c) 2016-2019 Panos Karabelas

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

/*
Supported formats
BMP files[reading, writing]
Dr.Halo CUT files[reading] *
DDS files[reading]
EXR files[reading, writing]
Raw Fax G3 files[reading]
GIF files[reading, writing]
HDR files[reading, writing]
ICO files[reading, writing]
IFF files[reading]
JBIG files[reading, writing] * *
JNG files[reading, writing]
JPEG / JIF files[reading, writing]
JPEG - 2000 File Format[reading, writing]
JPEG - 2000 codestream[reading, writing]
JPEG - XR files[reading, writing]
KOALA files[reading]
Kodak PhotoCD files[reading]
MNG files[reading]
PCX files[reading]
PBM / PGM / PPM files[reading, writing]
PFM files[reading, writing]
PNG files[reading, writing]
Macintosh PICT files[reading]
Photoshop PSD files[reading]
RAW camera files[reading]
Sun RAS files[reading]
SGI files[reading]
TARGA files[reading, writing]
TIFF files[reading, writing]
WBMP files[reading, writing]
WebP files[reading, writing]
XBM files[reading]
XPM files[reading, writing]
*/

//= INCLUDES ========================
#include <vector>
#include "../../Core/EngineDefs.h"
#include "../../RHI/RHI_Definition.h"
//===================================

struct FIBITMAP;

namespace Directus
{
	class Context;

	class ENGINE_CLASS ImageImporter
	{
	public:
		ImageImporter(Context* context);
		~ImageImporter();

		bool Load(const std::string& filePath, RHI_Texture* texture);

	private:	
		bool GetBitsFromFIBITMAP(std::vector<std::byte>* data, FIBITMAP* bitmap, unsigned int width, unsigned int height, unsigned int channels);
		void GenerateMipmaps(FIBITMAP* bitmap, RHI_Texture* texture, unsigned int width, unsigned int height, unsigned int channels);

		unsigned int ComputeChannelCount(FIBITMAP* bitmap);
		unsigned int ComputeBitsPerChannel(FIBITMAP* bitmap);
		RHI_Format ComputeTextureFormat(unsigned int bpp, unsigned int channels);
		bool IsVisuallyGrayscale(FIBITMAP* bitmap);
		FIBITMAP* ApplyBitmapCorrections(FIBITMAP* bitmap);
		FIBITMAP* _FreeImage_ConvertTo32Bits(FIBITMAP* bitmap);
		FIBITMAP* _FreeImage_Rescale(FIBITMAP* bitmap, unsigned int width, unsigned int height);

		Context* m_context;
	};
}