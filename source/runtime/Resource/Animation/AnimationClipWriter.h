/*
Copyright(c) 2015-2026 Panos Karabelas
*/

#pragma once

//= INCLUDES ==========================
#include "../../Rendering/Animation/AnimationClip.h"
#include <string>
//=====================================

namespace spartan
{
    class AnimationClipWriter
    {
    public:
        static bool WriteToFile(const AnimationClip& clip, const std::string& path);
    };
}
