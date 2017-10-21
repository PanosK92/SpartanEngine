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

//= INCLUDES ===========================
#include "ImageImporter.h"
#include "../../Logging/Log.h"
#include "../../FileSystem/FileSystem.h"
#include "FreeImagePlus.h"
#include <future>
#include <functional>
#include "../../Core/Context.h"
#include "../../Threading/Threading.h"
#include <map>
#include "../../Math/Vector2.h"
//======================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	ImageImporter::ImageImporter(Context* context)
	{
		m_context = context;
		m_bpp = 0;
		m_width = 0;
		m_height = 0;
		m_path.clear();
		m_channels = 4;
		m_grayscale = false;
		m_transparent = false;
		m_isLoading = false;

		FreeImage_Initialise(true);
	}

	ImageImporter::~ImageImporter()
	{
		Clear();
		FreeImage_DeInitialise();
	}

	void ImageImporter::LoadAsync(const string& filePath)
	{
		m_path = filePath;
		m_context->GetSubsystem<Threading>()->AddTask([this]()
		{
			Load(m_path);
		});
	}

	bool ImageImporter::Load(const string& path, int width, int height, bool scale, bool generateMipchain)
	{
		m_path = path;
		m_isLoading = true;

		Clear();

		if (!FileSystem::FileExists(path))
		{
			LOG_WARNING("Texture \"" + path + "\" doesn't exist.");
			m_isLoading = false;
			return false;
		}

		// Get image format
		FREE_IMAGE_FORMAT format = FreeImage_GetFileType(path.c_str(), 0);

		// If the format is unknown
		if (format == FIF_UNKNOWN)
		{
			// Try getting the format from the file extension
			LOG_WARNING("Failed to determine image format for \"" + path + "\", attempting to detect it from the file's extension...");
			format = FreeImage_GetFIFFromFilename(path.c_str());

			// If the format is still unknown, give up
			if (!FreeImage_FIFSupportsReading(format))
			{
				LOG_WARNING("Failed to detect the image format.");
				m_isLoading = false;
				return false;
			}

			LOG_WARNING("The image format has been detected succesfully.");
		}

		// Get image format, format == -1 means the file was not found
		// but I am checking against it also, just in case.
		if (format == -1 || format == FIF_UNKNOWN)
		{
			m_isLoading = false;
			return false;
		}

		// Load the image as a FIBITMAP*
		FIBITMAP* bitmapOriginal = FreeImage_Load(format, path.c_str());

		// Flip it vertically
		FreeImage_FlipVertical(bitmapOriginal);

		// Perform any scaling (if necessary)
		FIBITMAP* bitmapScaled = scale ? FreeImage_Rescale(bitmapOriginal, width, height, FILTER_LANCZOS3) : bitmapOriginal;

		// Convert it to 32 bits (if neccessery)
		m_bpp = FreeImage_GetBPP(bitmapOriginal);
		FIBITMAP* bitmap32 = m_bpp != 32 ? FreeImage_ConvertTo32Bits(bitmapScaled) : bitmapScaled;

		// Store some useful data	
		m_transparent = bool(FreeImage_IsTransparent(bitmap32));
		m_path = path;
		m_width = FreeImage_GetWidth(bitmap32);
		m_height = FreeImage_GetHeight(bitmap32);

		// Fill RGBA vector with the data from the FIBITMAP
		FIBTIMAPToRGBA(bitmap32, &m_dataRGBA);

		// Check if the image is grayscale
		m_grayscale = GrayscaleCheck(m_dataRGBA, m_width, m_height);

		if (generateMipchain)
		{
			GenerateMipmapsFromFIBITMAP(bitmap32, m_mipchainDataRGBA);
		}

		//= Free memory =====================================
		// unload the 32-bit bitmap
		FreeImage_Unload(bitmap32);

		// unload the scaled bitmap only if it was converted
		if (m_bpp != 32)
		{
			FreeImage_Unload(bitmapScaled);
		}

		// unload the non 32-bit bitmap only if it was scaled
		if (scale)
		{
			FreeImage_Unload(bitmapOriginal);
		}
		//====================================================

		m_isLoading = false;
		return true;
	}

	void ImageImporter::Clear()
	{
		m_dataRGBA.clear();
		m_dataRGBA.shrink_to_fit();
		m_mipchainDataRGBA.clear();
		m_mipchainDataRGBA.shrink_to_fit();
		m_bpp = 0;
		m_width = 0;
		m_height = 0;
		m_path.clear();
		m_grayscale = false;
		m_transparent = false;
	}

	bool ImageImporter::FIBTIMAPToRGBA(FIBITMAP* fibtimap, vector<unsigned char>* rgba)
	{
		int width = FreeImage_GetWidth(fibtimap);
		int height = FreeImage_GetHeight(fibtimap);

		unsigned int bytespp = width != 0 ? FreeImage_GetLine(fibtimap) / width : -1;
		if (bytespp == -1)
			return false;

		// Construct an RGBA array
		for (unsigned int y = 0; y < height; y++)
		{
			unsigned char* bits = (unsigned char*)FreeImage_GetScanLine(fibtimap, y);
			for (unsigned int x = 0; x < width; x++)
			{
				rgba->emplace_back(bits[FI_RGBA_RED]);
				rgba->emplace_back(bits[FI_RGBA_GREEN]);
				rgba->emplace_back(bits[FI_RGBA_BLUE]);
				rgba->emplace_back(bits[FI_RGBA_ALPHA]);

				// jump to next pixel
				bits += bytespp;
			}
		}

		return true;
	}

	struct ImageScalingInfo
	{
		int width = 0;
		int height = 0;
		bool scaled = false;
		vector<unsigned char> data;

		ImageScalingInfo(int width, int height, bool scaled)
		{
			this->width = width;
			this->height = height;
			this->scaled = scaled;
		}
	};

	void ImageImporter::GenerateMipmapsFromFIBITMAP(FIBITMAP* original, vector<vector<unsigned char>>& mimaps)
	{
		// First mip is full size
		mimaps.emplace_back(m_dataRGBA);
		int width = FreeImage_GetWidth(original);
		int height = FreeImage_GetHeight(original);

		// Compute the rest mip mipmapInfos
		vector<ImageScalingInfo> mipmapInfos;
		while (width > 1 && height > 1)
		{
			width = max(width / 2, 1);
			height = max(height / 2, 1);

			mipmapInfos.emplace_back(width, height, false);
		}

		// Parallelize mipmap generation using multiple
		// threads as FreeImage_Rescale() can take a while.
		Threading* threading = m_context->GetSubsystem<Threading>();
		for (int i = 0; i < mipmapInfos.size(); i++)
		{
			threading->AddTask([this, &mipmapInfos, i, original]()
			{
				if (!RescaleFIBITMAP(original, mipmapInfos[i].width, mipmapInfos[i].height, mipmapInfos[i].data))
				{
					string mipLevel = to_string(i + 2) + " (" + to_string(mipmapInfos[i].width) + "x" +to_string(mipmapInfos[i].height) + ")";
					string texName = FileSystem::GetFileNameFromFilePath(m_path);
					LOG_INFO("ImageImporter: Failed to create mip level " + mipLevel + " for texture \"" + texName + "\".");
				}
				mipmapInfos[i].scaled = true;
			});
		}

		// Wait until all mimaps have been generated
		bool ready = false;
		while (!ready)
		{
			ready = true;
			for (const auto& mimapInfo : mipmapInfos)
			{
				if (!mimapInfo.scaled)
				{
					ready = false;
				}
			}
		}

		// Now copy all the mimaps
		for (const auto& mimapInfo : mipmapInfos)
		{
			mimaps.emplace_back(move(mimapInfo.data));
		}
	}

	bool ImageImporter::RescaleFIBITMAP(FIBITMAP* fibtimap, int width, int height, vector<unsigned char>& rgba)
	{
		// Rescale
		FIBITMAP* scaled = FreeImage_Rescale(fibtimap, width, height, FILTER_LANCZOS3);

		// Extract RGBA data from the FIBITMAP
		bool result = FIBTIMAPToRGBA(scaled, &rgba);

		// Unload the FIBITMAP
		FreeImage_Unload(scaled);

		return result;
	}

	bool ImageImporter::GrayscaleCheck(const vector<unsigned char>& dataRGBA, int width, int height)
	{
		int grayPixels = 0;
		int scannedPixels = 0;

		for (int i = 0; i < height; i++)
		{
			for (int j = 0; j < width; j++)
			{
				scannedPixels++;

				int red = dataRGBA[(i * width + j) * 4 + 0];
				int green = dataRGBA[(i * width + j) * 4 + 1];
				int blue = dataRGBA[(i * width + j) * 4 + 2];

				if (red == green && red == blue)
					grayPixels++;
			}
		}

		if (grayPixels == scannedPixels)
			return true;

		return false;
	}
}
