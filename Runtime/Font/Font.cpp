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

//= INCLUDES ===========================
#include "Font.h"
#include "../Resource/ResourceManager.h"
//======================================

namespace Directus
{
	Font::Font(Context* context)
	{
		m_context = context;
	}

	Font::~Font()
	{
	}

	bool Font::SaveToFile(const std::string& filePath)
	{
		return true;
	}

	bool Font::LoadFromFile(const std::string& filePath)
	{
		if (!m_context)
			return false;

		int fontSize = 200;
		m_textureAtlas = m_context->GetSubsystem<ResourceManager>()->GetFontImporter()._Get()->LoadFont(filePath, fontSize);

		return true;
	}

	void** Font::GetShaderResource()
	{
		return m_textureAtlas->GetShaderResource();
	}
}
