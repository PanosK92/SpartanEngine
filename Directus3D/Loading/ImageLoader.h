/*
Copyright(c) 2016 Panos Karabelas

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

//= INCLUDES =============================
#include "../Core/Texture.h"
#include "../Graphics/D3D11/D3D11Shader.h"
//========================================

class FIBITMAP;

class __declspec(dllexport) ImageLoader
{
public:
	ImageLoader(D3D11Device* d3d11device);
	~ImageLoader();

	bool Load(std::string path);
	bool Load(std::string path, int width, int height);

	void Clear();

	/*------------------------------------------------------------------------------
									[PROPERTIES]
	------------------------------------------------------------------------------*/
	Texture* GetAsTexture();
	ID3D11ShaderResourceView* GetAsD3D11ShaderResourceView();
	unsigned char* GetRGBA();
	unsigned char* GetRGBACopy();
	unsigned char* GetRGBCopy();
	unsigned char* GetAlphaCopy();
	unsigned int GetBPP();
	unsigned int GetWidth();
	unsigned int GetHeight();
	bool IsGrayscale();
	bool IsTransparent();
	std::string GetPath();

private:
	bool Load(std::string path, int width, int height, bool scale);
	bool CheckIfGrayscale();

	FIBITMAP* m_bitmap;
	FIBITMAP* m_bitmap32;
	FIBITMAP* m_bitmapScaled;
	unsigned char* m_dataRGBA;
	unsigned int m_bpp;
	unsigned int m_width;
	unsigned int m_height;
	int m_channels;
	std::string m_path;
	bool m_grayscale;
	bool m_transparent;

	// dependencies
	D3D11Device* m_d3d11Device;
};
