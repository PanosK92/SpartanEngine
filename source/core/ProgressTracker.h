/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
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
