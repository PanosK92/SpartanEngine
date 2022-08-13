/*
Copyright(c) 2016-2022 Panos Karabelas

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

//= INCLUDES ==========================

#include "../Core/SpartanDefinitions.h"
#include <array>
//=====================================

namespace Spartan
{
    enum class ProgressType
    {
        ModelImporter,
        World,
        ResourceCache
    };

    struct Progress
    {
        void Clear()
        {
            status.clear();
            jods_done  = 0;
            job_count  = 0;
            is_loading = false;
        }

        std::string status;
        int jods_done   = 0;
        int job_count   = 0;
        bool is_loading = false;
    };

    class SPARTAN_CLASS ProgressTracker
    {
    public:
        static ProgressTracker& Get()
        {
            static ProgressTracker instance;
            return instance;
        }

        ProgressTracker() = default;

        void Reset(ProgressType progress_type);
        const std::string& GetStatus(ProgressType progress_type);
        void SetStatus(ProgressType progress_type, const std::string& status);
        void SetJobCount(ProgressType progress_type, int jobCount);
        void IncrementJobsDone(ProgressType progress_type);
        void SetJobsDone(ProgressType progress_type, int jobsDone);
        float GetPercentage(ProgressType progress_type);
        bool GetIsLoading(ProgressType progress_type);
        void SetIsLoading(ProgressType progress_type, bool isLoading);

    private:
        std::array<Progress, 3> m_reports;
    };
}
