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

//= INCLUDES ================
#include "ImageLoader.h"
#include <FreeImagePlus.h>
#include "../IO/Log.h"
#include "../IO/FileHelper.h"

//===========================

ImageLoader::ImageLoader()
{
	m_bitmap = nullptr;
	m_bitmap32 = nullptr;
	m_bitmapScaled = nullptr;
	m_dataRGBA = nullptr;
	m_bpp = 0;
	m_width = 0;
	m_height = 0;
	m_path = "";
	m_channels = 4;
	m_grayscale = false;
	m_transparent = false;

	FreeImage_Initialise(true);
}

ImageLoader::~ImageLoader()
{
	Clear();
	FreeImage_DeInitialise();
}

void ImageLoader::Initialize(GraphicsDevice* D3D11evice)
{
	m_graphicsDevice = D3D11evice;
}

bool ImageLoader::Load(std::string path)
{
	// keep the path
	m_path = path;

	// clear memory in case there are leftovers from last call
	Clear();

	// try to load the image
	return Load(path, 0, 0, false);
}

bool ImageLoader::Load(std::string path, int width, int height)
{
	// keep the path
	m_path = path;

	// clear memory in case there are leftovers from last call
	Clear();

	// try to load the image
	return Load(path, width, height, true);
}

void ImageLoader::Clear()
{
	if (m_dataRGBA != nullptr)
	{
		delete[] m_dataRGBA;
		m_dataRGBA = nullptr;
	}

	m_bitmap = nullptr;
	m_bitmap32 = nullptr;
	m_bitmapScaled = nullptr;
	m_dataRGBA = nullptr;
	m_bpp = 0;
	m_width = 0;
	m_height = 0;
	m_path = "";
	m_grayscale = false;
	m_transparent = false;
}

//= PROPERTIES =====================================================
ID3D11ShaderResourceView* ImageLoader::GetAsD3D11ShaderResourceView()
{
	DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM; // texture format
	unsigned int mipLevels = 7; // 0 for a full mip chain. The mip chain will extend to 1x1 at the lowest level, even if the dimensions aren't square.

	// texture description
	D3D11_TEXTURE2D_DESC textureDesc{};
	textureDesc.Width = m_width;
	textureDesc.Height = m_height;
	textureDesc.MipLevels = mipLevels;
	textureDesc.ArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.Format = format;
	textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	textureDesc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

	// Create 2D texture from texture description
	ID3D11Texture2D* texture = nullptr;
	HRESULT hResult = m_graphicsDevice->GetDevice()->CreateTexture2D(&textureDesc, nullptr, &texture);
	if (FAILED(hResult))
	{
		LOG("Failed to create ID3D11Texture2D from imported image data while trying to load " + m_path + ".", Log::Error);
		return nullptr;
	}

	// Resource view description
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = textureDesc.MipLevels;

	// Create shader resource view from resource view description
	ID3D11ShaderResourceView* shaderResourceView = nullptr;
	hResult = m_graphicsDevice->GetDevice()->CreateShaderResourceView(texture, &srvDesc, &shaderResourceView);
	if (FAILED(hResult))
	{
		LOG("Failed to create the shader resource view.", Log::Error);
		return nullptr;
	}

	// Resource data description
	D3D11_SUBRESOURCE_DATA mapResource{};
	mapResource.pSysMem = m_dataRGBA;
	mapResource.SysMemPitch = sizeof(unsigned char) * m_width * m_channels;

	// Copy data from memory to the subresource created in non-mappable memory
	m_graphicsDevice->GetDeviceContext()->UpdateSubresource(texture, 0, nullptr, mapResource.pSysMem, mapResource.SysMemPitch, 0);

	// Generate mip chain
	m_graphicsDevice->GetDeviceContext()->GenerateMips(shaderResourceView);

	return shaderResourceView;
}

unsigned char* ImageLoader::GetRGBA()
{
	return m_dataRGBA;
}

unsigned char* ImageLoader::GetRGBACopy()
{
	unsigned char* dataRGBA = new unsigned char[m_width * m_height * 4];

	for (int i = 0; i < m_height; i++)
		for (int j = 0; j < m_width; j++)
		{
			int red = m_dataRGBA[(i * m_width + j) * 4 + 0];
			int green = m_dataRGBA[(i * m_width + j) * 4 + 1];
			int blue = m_dataRGBA[(i * m_width + j) * 4 + 2];
			int alpha = m_dataRGBA[(i * m_width + j) * 4 + 3];

			dataRGBA[(i * m_width + j) * 3 + 0] = red;
			dataRGBA[(i * m_width + j) * 3 + 1] = green;
			dataRGBA[(i * m_width + j) * 3 + 2] = blue;
			dataRGBA[(i * m_width + j) * 3 + 3] = alpha;
		}

	return dataRGBA;
}

unsigned char* ImageLoader::GetRGBCopy()
{
	unsigned char* m_dataRGB = new unsigned char[m_width * m_height * 3];

	for (int i = 0; i < m_height; i++)
		for (int j = 0; j < m_width; j++)
		{
			int red = m_dataRGBA[(i * m_width + j) * 4 + 0];
			int green = m_dataRGBA[(i * m_width + j) * 4 + 1];
			int blue = m_dataRGBA[(i * m_width + j) * 4 + 2];

			m_dataRGB[(i * m_width + j) * 3 + 0] = red;
			m_dataRGB[(i * m_width + j) * 3 + 1] = green;
			m_dataRGB[(i * m_width + j) * 3 + 2] = blue;
		}

	return m_dataRGB;
}

unsigned char* ImageLoader::GetAlphaCopy()
{
	unsigned char* m_dataAlpha = new unsigned char[m_width * m_height];

	for (int i = 0; i < m_height; i++)
		for (int j = 0; j < m_width; j++)
		{
			int alpha = m_dataRGBA[(i * m_width + j) * 4 + 3];
			m_dataAlpha[(i * m_width + j)] = alpha;
		}

	return m_dataAlpha;
}

unsigned ImageLoader::GetBPP()
{
	return m_bpp;
}

unsigned ImageLoader::GetWidth()
{
	return m_width;
}

unsigned ImageLoader::GetHeight()
{
	return m_height;
}

bool ImageLoader::IsGrayscale()
{
	return m_grayscale;
}

bool ImageLoader::IsTransparent()
{
	return m_transparent;
}

std::string ImageLoader::GetPath()
{
	return m_path;
}

bool ImageLoader::Load(std::string path, int width, int height, bool scale)
{
	// Clear any data left from a previous image loading (if necessary)
	Clear();

	if (!FileHelper::FileExists(path))
	{
		LOG("Could not find image \"" + path + "\".", Log::Error);
		return false;
	}

	// Get the format of the image
	FREE_IMAGE_FORMAT format = FreeImage_GetFileType(path.c_str(), 0);

	// If the image format couldn't be determined
	if (format == FIF_UNKNOWN)
	{
		// Try getting the format from the file extension
		LOG("Couldn't determine image format, attempting to get from file extension...", Log::Warning);
		format = FreeImage_GetFIFFromFilename(path.c_str());

		if (!FreeImage_FIFSupportsReading(format))
			LOG("Detected image format cannot be read.", Log::Warning);
	}

	// Get image format, format == -1 means the file was not found
	// but I am checking against it also, just in case.
	if (format == -1 || format == FIF_UNKNOWN)
		return false;

	// Load the image as a FIBITMAP*
	m_bitmap = FreeImage_Load(format, path.c_str());

	// Flip it vertically
	FreeImage_FlipVertical(m_bitmap);

	// Perform any scaling (if necessary)
	if (scale)
		m_bitmapScaled = FreeImage_Rescale(m_bitmap, width, height, FILTER_LANCZOS3);
	else
		m_bitmapScaled = m_bitmap;

	// Convert it to 32 bits (if necessery)
	m_bpp = FreeImage_GetBPP(m_bitmap); // get bits per pixel
	if (m_bpp != 32)
		m_bitmap32 = FreeImage_ConvertTo32Bits(m_bitmapScaled);
	else
		m_bitmap32 = m_bitmapScaled;

	// Store some useful data	
	m_transparent = bool(FreeImage_IsTransparent(m_bitmap32));
	m_path = path;
	m_width = FreeImage_GetWidth(m_bitmap32);
	m_height = FreeImage_GetHeight(m_bitmap32);
	m_dataRGBA = new unsigned char[m_width * m_height * m_channels];
	unsigned int bytespp = m_width != 0 ? FreeImage_GetLine(m_bitmap32) / m_width : -1;
	if (bytespp == -1)
		return false;

	// Construct a 2D RGBA array
	for (unsigned int y = 0; y < m_height; y++)
	{
		unsigned char* bits = (unsigned char*)FreeImage_GetScanLine(m_bitmap32, y);
		for (unsigned int x = 0; x < m_width; x++)
		{
			unsigned int id = (x + y * m_width) * m_channels;

			m_dataRGBA[id + 0] = bits[FI_RGBA_RED];
			m_dataRGBA[id + 1] = bits[FI_RGBA_GREEN];
			m_dataRGBA[id + 2] = bits[FI_RGBA_BLUE];
			m_dataRGBA[id + 3] = bits[FI_RGBA_ALPHA];

			// jump to next pixel
			bits += bytespp;
		}
	}

	// Store some useful data that require m_dataRGBA to be filled
	m_grayscale = CheckIfGrayscale();

	//= Free memory =====================================
	// unload the 32-bit bitmap
	FreeImage_Unload(m_bitmap32);

	// unload the scaled bitmap only if it was converted
	if (m_bpp != 32)
		FreeImage_Unload(m_bitmapScaled);

	// unload the non 32-bit bitmap only if it was scaled
	if (scale)
		FreeImage_Unload(m_bitmap);
	//====================================================

	return true;
}

bool ImageLoader::CheckIfGrayscale()
{
	int grayPixels = 0;
	int scannedPixels = 0;

	for (int i = 0; i < m_height; i++)
		for (int j = 0; j < m_width; j++)
		{
			scannedPixels++;

			int red = m_dataRGBA[(i * m_width + j) * 4 + 0];
			int green = m_dataRGBA[(i * m_width + j) * 4 + 1];
			int blue = m_dataRGBA[(i * m_width + j) * 4 + 2];

			if (red == green && red == blue)
				grayPixels++;
		}

	if (grayPixels == scannedPixels)
		return true;

	return false;
}
