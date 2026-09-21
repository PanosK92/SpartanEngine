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

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace spartan
{
    enum class ProgressType { World, Download, ModelImporter, Terrain, Texture, Max };
    enum class ProgressMode { Loading, Background };
    struct ProgressTaskState;

    // Own the task for as long as the work is alive. Copies can travel with async
    // jobs; the last copy automatically finishes, including on early returns.
    class ProgressTask
    {
    public:
        ProgressTask() = default;
        void SetStep(const std::string& step, const std::string& detail = {}) const;
        void SetDetail(const std::string& detail) const;
        void SetFraction(float fraction) const; // Optional progress within this step; never finishes the task.
        void Finish() const; // Idempotent, for work handed off across frames.

    private:
        friend class ProgressTracker;
        explicit ProgressTask(std::shared_ptr<ProgressTaskState> state) : m_state(std::move(state)) {}
        std::shared_ptr<ProgressTaskState> m_state;
    };

    struct ProgressSnapshot
    {
        uint64_t id = 0;
        uint64_t step_id = 0;
        ProgressType type = ProgressType::World;
        std::string title;
        std::string step;
        std::string detail;
        float fraction = -1.0f; // Negative means indeterminate, not zero percent.
        double elapsed_seconds = 0.0;
    };

    struct ProgressDisplay
    {
        std::array<ProgressSnapshot, 2> tasks;
        uint32_t count = 0;
        uint32_t active_count = 0;
    };

    class ProgressTracker
    {
    public:
        // Typical use: auto task = Begin(...); task.SetStep("Reading geometry");
        // No declared step count, paired increments, or explicit cleanup required.
        static ProgressTask Begin(ProgressType type, const std::string& title, const std::string& step = {}, ProgressMode mode = ProgressMode::Loading);
        static bool IsLoading();
        static bool IsLoading(ProgressType type);
        static ProgressDisplay GetDisplay(); // One coherent snapshot, at most two visible tasks.
    };
}
