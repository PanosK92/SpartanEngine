/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ======================
#include "../../animation/Skeleton.h"
#include <string>
//=================================

namespace spartan
{
    class SkeletonReader
    {
    public:
        static bool ReadFromFile(const std::string& path, Skeleton& skeleton);
    };

    class SkeletonWriter
    {
    public:
        static bool WriteToFile(const Skeleton& skeleton, const std::string& path);
    };
}
