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

//= INCLUDES ===============
#include "pch.h"
#include "ProgressTracker.h"
//==========================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    { 
        array<Progress, static_cast<size_t>(ProgressType::Max)> progresses;
        recursive_mutex mutex_jobs;
        uint32_t anonymous_jobs = 0;
    }

    void Progress::Start(const uint32_t job_count, const string& text)
    {
        SP_ASSERT_MSG(GetFraction() == 1.0f, "The previous progress tracking hasn't finished");

        lock_guard lock(mutex_jobs);

        m_job_count       = job_count;
        m_jobs_done       = 0;
        m_text            = text;
        m_continuous_mode = (job_count == 0); // if job_count is 0, use continuous mode
        m_fraction        = 0.0f;
    }

    float Progress::GetFraction() const
    {
        lock_guard lock(mutex_jobs);

        if (m_continuous_mode)
        {
            return m_fraction.load();
        }

        if (m_job_count == 0)
            return 1.0f;

        return static_cast<float>(m_jobs_done) / static_cast<float>(m_job_count);
    }

    void Progress::SetFraction(float fraction)
    {
        lock_guard lock(mutex_jobs);
        m_fraction = (fraction < 0.0f) ? 0.0f : (fraction > 1.0f) ? 1.0f : fraction;
    }

    bool Progress::IsProgressing() const
    {
        return GetFraction() != 1.0f;
    }

    void Progress::JobDone()
    {
        lock_guard lock(mutex_jobs);

        SP_ASSERT_MSG(m_jobs_done + 1 <= m_job_count, "Job count exceeded");
        m_jobs_done++;
    }

	const string& Progress::GetText()
    {
        return m_text;
    }

    void Progress::SetText(const string& text)
    {
        m_text = text;
    }

    Progress& ProgressTracker::GetProgress(const ProgressType progress_type)
    {
        lock_guard lock(mutex_jobs);

        return progresses[static_cast<uint32_t>(progress_type)];
    }

    bool ProgressTracker::IsLoading()
    {
        lock_guard lock(mutex_jobs);

        if (anonymous_jobs > 0)
            return true;

        for (const Progress& progress : progresses)
        {
            if (progress.IsProgressing())
                return true;
        }

        return false; 
    }

    void ProgressTracker::SetGlobalLoadingState(bool is_loading)
    {
        lock_guard lock(mutex_jobs);

        if (is_loading)
        {
            anonymous_jobs++;
        }
        else
        {
            anonymous_jobs--;
        }
    }
}
