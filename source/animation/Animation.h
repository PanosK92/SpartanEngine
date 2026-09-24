/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES =====================
#include <string>
#include "../resource/IResource.h"
#include "AnimationClip.h"
//================================

namespace spartan
{
    class Animation : public IResource
    {
    public:
        Animation();
        ~Animation() = default;

        void SaveToFile(const std::string& file_path) override;
        void LoadFromFile(const std::string& file_path) override;

        AnimationClip& GetClip()             { return m_clip; }
        const AnimationClip& GetClip() const { return m_clip; }

    private:
        AnimationClip m_clip;
    };
}
