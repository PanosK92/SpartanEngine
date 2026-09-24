/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =====================================
#include "pch.h"
#include "Animation.h"
#include "../resource/animation/AnimationClipIO.h"
//================================================

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
