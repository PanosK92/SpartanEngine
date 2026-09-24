/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ==================================
#include "../../animation/AnimationClip.h"
#include <string>
//=============================================

namespace spartan
{
    class AnimationClipReader
    {
    public:
        static bool ReadFromFile(const std::string& path, AnimationClip& clip);
    };

    class AnimationClipWriter
    {
    public:
        static bool WriteToFile(const AnimationClip& clip, const std::string& path);
    };
}
