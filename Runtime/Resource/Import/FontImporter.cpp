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
#include "FontImporter.h"
#include "ft2build.h"
#include FT_FREETYPE_H  
#include "../../Logging/Log.h"
#include "../../FileSystem/FileSystem.h"
//======================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{
	FT_Library m_library;

	FontImporter::FontImporter()
	{

	}

	FontImporter::~FontImporter()
	{

	}

	void FontImporter::Initialize()
	{
		if (FT_Init_FreeType(&m_library))
		{
			LOG_ERROR("FreeType: Failed to initialize.");
		}	
	}

	bool FontImporter::LoadFont(const string& filePath, int size)
	{
		FT_Face face;
		if (FT_New_Face(m_library, filePath.c_str(), 0, &face))
		{
			LOG_ERROR("FreeType: Failed to load font \"" + FileSystem::GetFileNameFromFilePath(filePath)+ "\".");
			return false;
		}

		if (FT_Set_Pixel_Sizes(face, 0, size))
		{
			LOG_ERROR("FreeType: Failed to set size for \"" + FileSystem::GetFileNameFromFilePath(filePath) + "\".");
			return false;
		}

		return true;
	}
}
