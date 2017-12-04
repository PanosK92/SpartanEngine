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

//= INCLUDES ================
#include <QPixmap>
#include "Core/Context.h"
#include "Graphics/Texture.h"
#include "Logging/Log.h"
//===========================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

class DirectusUtilities
{
public:
    static QPixmap LoadQPixmap(Context* context, const std::string& filePath, unsigned int width, unsigned int height)
    {
        QPixmap pixmap;
        if (!FileSystem::IsEngineTextureFile(filePath) && !FileSystem::IsSupportedImageFile(filePath))
        {
            LOG_WARNING("DirectusUtilities: Can't create QPixmap. Provided filepath \"" + filePath + "\" is not a supported texture file.");
            return pixmap;
        }

        // Load texture
        shared_ptr<Texture> texture = make_shared<Texture>(context);
        if (texture->LoadFromFile(filePath))
        {
            // Get smallest mip
            unsigned char* bits = &texture->GetRGBA().front()[0];
            int texWidth = texture->GetWidth();
            int texHeight = texture->GetHeight();

            QImage image = QImage((const uchar*)bits, texWidth, texHeight, QImage::Format_RGBA8888);
            pixmap = QPixmap::fromImage(image);

            // Is rescaling required?
            if (texture->GetWidth() != width || texture->GetHeight() != height)
            {
                pixmap = pixmap.scaled(width, height, Qt::IgnoreAspectRatio, Qt::FastTransformation);
            }
        }

        return pixmap;
    }
};
