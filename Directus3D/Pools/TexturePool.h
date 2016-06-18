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

//= INCLUDES ======================
#include <vector>
#include "../Loading/ImageLoader.h"
#include "../Core/Texture.h"
///================================

#define TEXTURE_PATH_UNKNOWN "-1"

class TexturePool
{
public:
	TexturePool(ImageLoader* imageLoader);
	~TexturePool();

	void Save();
	void Load();

	Texture* Add(std::string path, TextureType type);
	Texture* GetTextureByID(std::string ID);
	Texture* GetTextureByPath(std::string path);
	void RemoveTextureByPath(std::string path);
	void Clear();

private:
	std::vector<Texture*> m_textures;
	ImageLoader* m_imageLoader;

	/*------------------------------------------------------------------------------
							[HELPER FUNCTIONS]
	------------------------------------------------------------------------------*/
	int GetTextureIndex(Texture* texture);
	bool TexturePool::IsTextureLoadedByPath(std::string path);
};
