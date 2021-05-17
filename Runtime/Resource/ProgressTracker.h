/*
Copyright(c) 2016-2021 Panos Karabelas

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

//= INCLUDES ===========================
#include <string>
#include <unordered_map>
#include "../Core/Spartan_Definitions.h"
//======================================

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
        Progress(){ Clear(); }

        void Clear()
        {
            status.clear();
            jods_done   = 0;
            job_count   = 0;
            is_loading  = false;
        }

        std::string status;
        int jods_done;
        int job_count;
        bool is_loading;
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

        void Reset(ProgressType progress_type)
        {
            m_reports[progress_type].Clear();
        }

        const std::string& GetStatus(ProgressType progress_type)                { return m_reports[progress_type].status; }
        void SetStatus(ProgressType progress_type, const std::string& status)   { m_reports[progress_type].status = status; }
        void SetJobCount(ProgressType progress_type, int jobCount)              { m_reports[progress_type].job_count = jobCount;}
        void IncrementJobsDone(ProgressType progress_type)                      { m_reports[progress_type].jods_done++; }
        void SetJobsDone(ProgressType progress_type, int jobsDone)              { m_reports[progress_type].jods_done = jobsDone; }
        float GetPercentage(ProgressType progress_type)                         { return m_reports[progress_type].job_count == 0 ? 0 : (static_cast<float>(m_reports[progress_type].jods_done) / static_cast<float>(m_reports[progress_type].job_count)); }
        bool GetIsLoading(ProgressType progress_type)                           { return m_reports[progress_type].is_loading; }
        void SetIsLoading(ProgressType progress_type, bool isLoading)           { m_reports[progress_type].is_loading = isLoading; }

    private:
        std::unordered_map<ProgressType, Progress> m_reports;
    };
}
