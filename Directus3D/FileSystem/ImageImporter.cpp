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
#include "ImageImporter.h"
#include "../Logging/Log.h"
#include "../FileSystem/FileSystem.h"
#include "FreeImagePlus.h"
#include "../Multithreading/ThreadPool.h"
//===========================

//= NAMESPACES =====
using namespace std;
//==================

ImageImporter::ImageImporter()
{
	m_bitmap = nullptr;
	m_bitmap32 = nullptr;
	m_bitmapScaled = nullptr;
	m_bpp = 0;
	m_width = 0;
	m_height = 0;
	m_path = "";
	m_channels = 4;
	m_grayscale = false;
	m_transparent = false;

	FreeImage_Initialise(true);
}

ImageImporter::~ImageImporter()
{
	Clear();
	FreeImage_DeInitialise();
}

void ImageImporter::LoadAsync(const string& filePath)
{
	//m_threadPool->AddTask(std::bind(&ImageImporter::Load, this, filePath));
}

bool ImageImporter::Load(const string& filePath)
{
	// keep the path
	m_path = filePath;

	// clear memory in case there are leftovers from last call
	Clear();

	// try to load the image
	return Load(filePath, 0, 0, false);
}

bool ImageImporter::Load(const string& path, int width, int height)
{
	// keep the path
	m_path = path;

	// clear memory in case there are leftovers from last call
	Clear();

	// try to load the image
	return Load(path, width, height, true);
}

bool ImageImporter::Load(const string& path, int width, int height, bool scale)
{
	// Clear any data left from a previous image loading (if necessary)
	Clear();

	if (!FileSystem::FileExists(path))
	{
		LOG_WARNING("Failed to load image \"" + path + "\", it doesn't exist.");
		return false;
	}

	// Get the format of the image
	FREE_IMAGE_FORMAT format = FreeImage_GetFileType(path.c_str(), 0);

	// If the image format couldn't be determined
	if (format == FIF_UNKNOWN)
	{
		// Try getting the format from the file extension
		LOG_WARNING("Failed to determine image format for \"" + path + "\", attempting to detect it from the file's extension...");
		format = FreeImage_GetFIFFromFilename(path.c_str());

		if (!FreeImage_FIFSupportsReading(format))
			LOG_WARNING("Failed to detect the image format.");
		else
			LOG_WARNING("The image format has been detected succesfully.");
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
	unsigned int bytespp = m_width != 0 ? FreeImage_GetLine(m_bitmap32) / m_width : -1;
	if (bytespp == -1)
		return false;

	// Construct an RGBA array
	for (unsigned int y = 0; y < m_height; y++)
	{
		unsigned char* bits = (unsigned char*)FreeImage_GetScanLine(m_bitmap32, y);
		for (unsigned int x = 0; x < m_width; x++)
		{
			m_dataRGBA.push_back(bits[FI_RGBA_RED]);
			m_dataRGBA.push_back(bits[FI_RGBA_GREEN]);
			m_dataRGBA.push_back(bits[FI_RGBA_BLUE]);
			m_dataRGBA.push_back(bits[FI_RGBA_ALPHA]);

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

void ImageImporter::Clear()
{
	m_dataRGBA.clear();
	m_dataRGBA.shrink_to_fit();
	m_bitmap = nullptr;
	m_bitmap32 = nullptr;
	m_bitmapScaled = nullptr;
	m_bpp = 0;
	m_width = 0;
	m_height = 0;
	m_path = "";
	m_grayscale = false;
	m_transparent = false;
}

//= PROPERTIES =====================================================
unsigned char* ImageImporter::GetRGBA()
{
	return m_dataRGBA.data();
}

unsigned char* ImageImporter::GetRGBACopy()
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

unsigned char* ImageImporter::GetRGBCopy()
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

unsigned char* ImageImporter::GetAlphaCopy()
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

unsigned ImageImporter::GetBPP()
{
	return m_bpp;
}

unsigned ImageImporter::GetWidth()
{
	return m_width;
}

unsigned ImageImporter::GetHeight()
{
	return m_height;
}

bool ImageImporter::IsGrayscale()
{
	return m_grayscale;
}

bool ImageImporter::IsTransparent()
{
	return m_transparent;
}

string ImageImporter::GetPath()
{
	return m_path;
}

bool ImageImporter::CheckIfGrayscale()
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
