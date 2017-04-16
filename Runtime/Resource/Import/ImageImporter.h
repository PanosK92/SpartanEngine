/*
Copyright(c) 2016-2017 Panos Karabelas

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

#define FREEIMAGE_LIB

//= INCLUDES ===============================
#include <vector>
#include "../../Multithreading/ThreadPool.h"
//==========================================

class FIBITMAP;

namespace Directus
{
	class DllExport ImageImporter
	{
	public:
		static ImageImporter& GetInstance()
		{
			static ImageImporter instance;
			return instance;
		}

		ImageImporter();
		~ImageImporter();

		void LoadAsync(const std::string& filePath);
		bool Load(const std::string& filePath) { return Load(filePath, 0, 0, false, false); }
		bool Load(const std::string& filePath, int width, int height) { return Load(filePath, width, height, true, false); }
		bool LoadAndCreateMipchain(const std::string& filePath) { return Load(filePath, 0, 0, false, true); }

		void Clear();

		//= PROPERTIES =======================================
		unsigned char* GetRGBA() { return m_dataRGBA.data(); }
		const std::vector<std::vector<unsigned char>>& GetRGBAMipchain();
		unsigned int GetBPP() { return m_bpp; }
		unsigned int GetWidth() { return m_width; }
		unsigned int GetHeight() { return m_height; }
		bool IsGrayscale() { return m_grayscale; }
		bool IsTransparent() { return m_transparent; }
		const std::string& GetPath() { return m_path; }
		int GetChannels() { return m_channels; }
		//====================================================

	private:
		std::vector<unsigned char> m_dataRGBA;
		std::vector<std::vector<unsigned char>> m_mipchainDataRGBA;
		unsigned int m_bpp;
		unsigned int m_width;
		unsigned int m_height;
		int m_channels;
		std::string m_path;
		bool m_grayscale;
		bool m_transparent;

		bool Load(const std::string& path, int width, int height, bool scale, bool generateMipmap);
		bool GetDataRGBAFromFIBITMAP(FIBITMAP* fibtimap, std::vector<unsigned char>* data);
		void GenerateMipChainFromFIBITMAP(FIBITMAP* original, std::vector<std::vector<unsigned char>>*);
		bool GrayscaleCheck(const std::vector<unsigned char>& dataRGBA, int width, int height);
	};
}