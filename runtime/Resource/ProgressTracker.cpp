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

//= INCLUDES ===============
#include "pch.h"
#include "ProgressTracker.h"
//==========================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    void ProgressTracker::Reset(ProgressType progress_type)
    {
        m_reports[static_cast<uint32_t>(progress_type)].Clear();
    }

    const string& ProgressTracker::GetStatus(ProgressType progress_type)
    {
        return m_reports[static_cast<uint32_t>(progress_type)].status;
    }

    void ProgressTracker::SetStatus(ProgressType progress_type, const std::string& status)
    {
        m_reports[static_cast<uint32_t>(progress_type)].status = status;
    }

    void ProgressTracker::SetJobCount(ProgressType progress_type, int jobCount)
    {
        m_reports[static_cast<uint32_t>(progress_type)].job_count = jobCount;
    }

    void ProgressTracker::IncrementJobsDone(ProgressType progress_type)
    {
        m_reports[static_cast<uint32_t>(progress_type)].jods_done++;
    }

    void ProgressTracker::SetJobsDone(ProgressType progress_type, int jobsDone)
    {
        m_reports[static_cast<uint32_t>(progress_type)].jods_done = jobsDone;
    }

    float ProgressTracker::GetPercentage(ProgressType progress_type)
    {
        Progress& progress = m_reports[static_cast<uint32_t>(progress_type)];

        if (progress.job_count == 0)
            return 0.0f;

        return static_cast<float>(progress.jods_done) / static_cast<float>(progress.job_count);
    }

    bool ProgressTracker::GetIsLoading(ProgressType progress_type)
    {
        return m_reports[static_cast<uint32_t>(progress_type)].is_loading;
    }

    void ProgressTracker::SetIsLoading(ProgressType progress_type, bool isLoading)
    {
        m_reports[static_cast<uint32_t>(progress_type)].is_loading = isLoading;
    }
}
