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

//= INCLUDES =====================
#include "../../Core/EngineDefs.h"
#include <vector>
#include <map>
//================================

struct FT_FaceRec_;

namespace Directus
{
	class Context;

	struct Glyph
	{
		int xLeft;
		int xRight;
		int yTop;
		int yBottom;
		int width;
		int height;
		float uvXLeft;
		float uvXRight;
		float uvYTop;
		float uvYBottom;
		int descent;
		int horizontalOffset;
	};

	class ENGINE_API FontImporter
	{
	public:
		FontImporter(Context* context);
		~FontImporter();

		void Initialize();
		bool LoadFont(const std::string& filePath, int fontSize, std::vector<unsigned char>& atlasBuffer, unsigned int& atlasWidth, unsigned int& atlasHeight, std::map<unsigned int, Glyph>& characterInfo);

	private:
		void ComputeAtlasTextureDimensions(FT_FaceRec_* face, unsigned int& atlasWidth, unsigned int& atlasHeight, unsigned int& rowHeight);
		int GetCharacterMaxHeight(FT_FaceRec_* face);
		bool HandleError(int errorCode);

		Context* m_context;
	};
}