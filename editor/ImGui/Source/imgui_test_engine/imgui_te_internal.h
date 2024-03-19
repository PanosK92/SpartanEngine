// dear imgui test engine
// (internal api)

#pragma once

#include "imgui_te_coroutine.h"
#include "imgui_te_utils.h"         // ImMovingAverage
#include "imgui_capture_tool.h"     // ImGuiCaptureTool  // FIXME

//-------------------------------------------------------------------------
// FORWARD DECLARATIONS
//-------------------------------------------------------------------------

class Str;                          // Str<> from thirdparty/Str/Str.h
struct ImGuiPerfTool;

//-------------------------------------------------------------------------
// DATA STRUCTURES
//-------------------------------------------------------------------------

// Query item position/window/state given ID.
struct ImGuiTestInfoTask
{
    // Input
    ImGuiID                 ID = 0;
    int                     FrameCount = -1;        // Timestamp of request
    char                    DebugName[64] = "";     // Debug string representing the queried ID

    // Output
    ImGuiTestItemInfo       Result;
};

// Gather item list in given parent ID.
struct ImGuiTestGatherTask
{
    // Input
    ImGuiID                 InParentID = 0;
    int                     InMaxDepth = 0;
    short                   InLayerMask = 0;

    // Output/Temp
    ImGuiTestItemList*      OutList = NULL;
    ImGuiTestItemInfo*      LastItemInfo = NULL;

    void Clear() { memset(this, 0, sizeof(*this)); }
};

// Find item ID given a label and a parent id
// Usually used by queries with wildcards such as ItemInfo("hello/**/foo/bar")
struct ImGuiTestFindByLabelTask
{
    // Input
    ImGuiID                 InPrefixId = 0;                 // A known base ID which appears BEFORE the wildcard ID (for "hello/**/foo/bar" it would be hash of "hello")
    int                     InSuffixDepth = 0;              // Number of labels in a path, after unknown base ID (for "hello/**/foo/bar" it would be 2)
    const char*             InSuffix = NULL;                // A label string which appears on ID stack after unknown base ID (for "hello/**/foo/bar" it would be "foo/bar")
    const char*             InSuffixLastItem = NULL;        // A last label string (for "hello/**/foo/bar" it would be "bar")
    ImGuiID                 InSuffixLastItemHash = 0;
    ImGuiItemStatusFlags    InFilterItemStatusFlags = 0;    // Flags required for item to be returned

    // Output
    ImGuiID                 OutItemId = 0;                  // Result item ID
};

enum ImGuiTestInputType
{
    ImGuiTestInputType_None,
    ImGuiTestInputType_Key,
    ImGuiTestInputType_Char,
    ImGuiTestInputType_ViewportFocus,
    ImGuiTestInputType_ViewportClose
};

// FIXME: May want to strip further now that core imgui is using its own input queue
struct ImGuiTestInput
{
    ImGuiTestInputType      Type = ImGuiTestInputType_None;
    ImGuiKeyChord           KeyChord = ImGuiKey_None;
    ImWchar                 Char = 0;
    bool                    Down = false;
    ImGuiID                 ViewportId = 0;

    static ImGuiTestInput   ForKeyChord(ImGuiKeyChord key_chord, bool down)
    {
        ImGuiTestInput inp;
        inp.Type = ImGuiTestInputType_Key;
        inp.KeyChord = key_chord;
        inp.Down = down;
        return inp;
    }

    static ImGuiTestInput   ForChar(ImWchar v)
    {
        ImGuiTestInput inp;
        inp.Type = ImGuiTestInputType_Char;
        inp.Char = v;
        return inp;
    }

    static ImGuiTestInput   ForViewportFocus(ImGuiID viewport_id)
    {
        ImGuiTestInput inp;
        inp.Type = ImGuiTestInputType_ViewportFocus;
        inp.ViewportId = viewport_id;
        return inp;
    }

    static ImGuiTestInput   ForViewportClose(ImGuiID viewport_id)
    {
        ImGuiTestInput inp;
        inp.Type = ImGuiTestInputType_ViewportClose;
        inp.ViewportId = viewport_id;
        return inp;
    }
};

struct ImGuiTestInputs
{
    ImVec2                      MousePosValue;                  // Own non-rounded copy of MousePos in order facilitate simulating mouse movement very slow speed and high-framerate
    ImVec2                      MouseWheel;
    ImGuiID                     MouseHoveredViewport = 0;
    int                         MouseButtonsValue = 0x00;       // FIXME-TESTS: Use simulated_io.MouseDown[] ?
    ImVector<ImGuiTestInput>    Queue;
    bool                        HostEscDown = false;
    float                       HostEscDownDuration = -1.0f;    // Maintain our own DownDuration for host/backend ESC key so we can abort.
};

// [Internal] Test Engine Context
struct ImGuiTestEngine
{
    ImGuiTestEngineIO           IO;
    ImGuiContext*               UiContextTarget = NULL;         // imgui context for testing
    ImGuiContext*               UiContextActive = NULL;         // imgui context for testing == UiContextTarget or NULL

    bool                        Started = false;
    ImU64                       BatchStartTime = 0;
    ImU64                       BatchEndTime = 0;
    int                         FrameCount = 0;
    float                       OverrideDeltaTime = -1.0f;      // Inject custom delta time into imgui context to simulate clock passing faster than wall clock time.
    ImVector<ImGuiTest*>        TestsAll;
    ImVector<ImGuiTestRunTask>  TestsQueue;
    ImGuiTestContext*           TestContext = NULL;
    ImVector<ImGuiTestInfoTask*>InfoTasks;
    ImGuiTestGatherTask         GatherTask;
    ImGuiTestFindByLabelTask    FindByLabelTask;
    void*                       UserDataBuffer = NULL;
    size_t                      UserDataBufferSize = 0;
    ImGuiTestCoroutineHandle    TestQueueCoroutine = NULL;      // Coroutine to run the test queue
    bool                        TestQueueCoroutineShouldExit = false; // Flag to indicate that we are shutting down and the test queue coroutine should stop

    // Inputs
    ImGuiTestInputs             Inputs;

    // UI support
    bool                        Abort = false;
    bool                        UiFocus = false;
    ImGuiTest*                  UiSelectAndScrollToTest = NULL;
    ImGuiTest*                  UiSelectedTest = NULL;
    Str*                        UiFilterTests;
    Str*                        UiFilterPerfs;
    ImU32                       UiFilterByStatusMask = ~0u;
    bool                        UiMetricsOpen = false;
    bool                        UiDebugLogOpen = false;
    bool                        UiCaptureToolOpen = false;
    bool                        UiStackToolOpen = false;
    bool                        UiPerfToolOpen = false;
    float                       UiLogHeight = 150.0f;

    // Performance Monitor
    double                      PerfRefDeltaTime;
    ImMovingAverage<double>     PerfDeltaTime100;
    ImMovingAverage<double>     PerfDeltaTime500;
    ImGuiPerfTool*              PerfTool = NULL;

    // Screen/Video Capturing
    ImGuiCaptureToolUI          CaptureTool;                        // Capture tool UI
    ImGuiCaptureContext         CaptureContext;                     // Capture context used in tests
    ImGuiCaptureArgs*           CaptureCurrentArgs = NULL;

    // Tools
    bool                        PostSwapCalled = false;
    bool                        ToolDebugRebootUiContext = false;   // Completely shutdown and recreate the dear imgui context in place
    bool                        ToolSlowDown = false;
    int                         ToolSlowDownMs = 100;
    ImGuiTestRunSpeed           BackupConfigRunSpeed = ImGuiTestRunSpeed_Fast;
    bool                        BackupConfigNoThrottle = false;

    // Functions
    ImGuiTestEngine();
    ~ImGuiTestEngine();
};

//-------------------------------------------------------------------------
// INTERNAL FUNCTIONS
//-------------------------------------------------------------------------

ImGuiTestItemInfo*  ImGuiTestEngine_FindItemInfo(ImGuiTestEngine* engine, ImGuiID id, const char* debug_id);
void                ImGuiTestEngine_Yield(ImGuiTestEngine* engine);
void                ImGuiTestEngine_SetDeltaTime(ImGuiTestEngine* engine, float delta_time);
int                 ImGuiTestEngine_GetFrameCount(ImGuiTestEngine* engine);
bool                ImGuiTestEngine_PassFilter(ImGuiTest* test, const char* filter);

void                ImGuiTestEngine_RebootUiContext(ImGuiTestEngine* engine);
ImGuiPerfTool*      ImGuiTestEngine_GetPerfTool(ImGuiTestEngine* engine);

// Screen/Video Capturing
bool                ImGuiTestEngine_CaptureScreenshot(ImGuiTestEngine* engine, ImGuiCaptureArgs* args);
bool                ImGuiTestEngine_CaptureBeginVideo(ImGuiTestEngine* engine, ImGuiCaptureArgs* args);
bool                ImGuiTestEngine_CaptureEndVideo(ImGuiTestEngine* engine, ImGuiCaptureArgs* args);

// Helper functions
const char*         ImGuiTestEngine_GetRunSpeedName(ImGuiTestRunSpeed v);
const char*         ImGuiTestEngine_GetVerboseLevelName(ImGuiTestVerboseLevel v);

//-------------------------------------------------------------------------
