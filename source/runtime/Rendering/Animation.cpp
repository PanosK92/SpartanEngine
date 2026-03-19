/*
Copyright(c) 2015-2026 Panos Karabelas

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

//= INCLUDES ========================================
#include "pch.h"
#include "Animation.h"
#include "../Resource/Animation/AnimationClipIO.h"
//===================================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    Animation::Animation(): IResource(ResourceType::Animation)
    {

    }

    void Animation::LoadFromFile(const string& file_path)
    {
        if (!AnimationClipReader::ReadFromFile(file_path, m_clip))
        {
            SP_LOG_ERROR("Failed to load animation clip from %s", file_path.c_str());
            return;
        }

        SetResourceFilePath(file_path);
    }

    void Animation::SaveToFile(const string& file_path)
    {
        if (!AnimationClipWriter::WriteToFile(m_clip, file_path))
        {
            SP_LOG_ERROR("Failed to save animation clip to %s", file_path.c_str());
            return;
        }

        SetResourceFilePath(file_path);
    }
}
