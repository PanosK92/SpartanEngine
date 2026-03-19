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
    class AnimationClipReader
    {
    public:
        static bool ReadFromFile(const std::string& path, AnimationClip& clip);
    };
}
