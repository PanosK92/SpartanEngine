// dear imgui test engine
// (ui)
// If you run tests in an interactive or visible application, you may want to call ImGuiTestEngine_ShowTestEngineWindows()

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_te_ui.h"
#include "../imgui.h"
#include "../imgui_internal.h"
#include "imgui_te_engine.h"
#include "imgui_te_context.h"
#include "imgui_te_internal.h"
#include "imgui_te_perftool.h"
#include "thirdparty/Str/Str.h"

//-------------------------------------------------------------------------
// TEST ENGINE: USER INTERFACE
//-------------------------------------------------------------------------
// - DrawTestLog() [internal]
// - GetVerboseLevelName() [internal]
// - ShowTestGroup() [internal]
// - ImGuiTestEngine_ShowTestWindows()
//-------------------------------------------------------------------------

// Look for " filename:number " in the string and add menu option to open source.
static bool ParseLineAndDrawFileOpenItemForSourceFile(ImGuiTestEngine* e, ImGuiTest* test, const char* line_start, const char* line_end)
{
    const char* separator = ImStrchrRange(line_start, line_end, ':');
    if (separator == NULL)
        return false;

    const char* path_end = separator;
    const char* path_begin = separator - 1;
    while (path_begin > line_start&& path_begin[-1] != ' ')
        path_begin--;
    if (path_begin == path_end)
        return false;

    int line_no = -1;
    sscanf(separator + 1, "%d ", &line_no);
    if (line_no == -1)
        return false;

    Str256f buf("Open '%.*s' at line %d", (int)(path_end - path_begin), path_begin, line_no);
    if (ImGui::MenuItem(buf.c_str()))
    {
        // FIXME-TESTS: Assume folder is same as folder of test->SourceFile!
        const char* src_path = test->SourceFile;
        const char* src_name = ImPathFindFilename(src_path);
        buf.setf("%.*s%.*s", (int)(src_name - src_path), src_path, (int)(path_end - path_begin), path_begin);

        ImGuiTestEngineIO& e_io = ImGuiTestEngine_GetIO(e);
        e_io.SrcFileOpenFunc(buf.c_str(), line_no, e_io.SrcFileOpenUserData);
    }

    return true;
}

// Look for "[ ,"]filename.png" in the string and add menu option to open image.
static bool ParseLineAndDrawFileOpenItemForImageFile(ImGuiTestEngine* e, ImGuiTest* test, const char* line_start, const char* line_end, const char* file_ext)
{
    IM_UNUSED(e);
    IM_UNUSED(test);

    const char* extension = ImStristr(line_start, line_end, file_ext, NULL);
    if (extension == NULL)
        return false;

    const char* path_end = extension + strlen(file_ext);
    const char* path_begin = extension - 1;
    while (path_begin > line_start && path_begin[-1] != ' ' && path_begin[-1] != '\'' && path_begin[-1] != '\"')
        path_begin--;
    if (path_begin == path_end)
        return false;

    Str256 buf;

    // Open file
    buf.setf("Open file: %.*s", (int)(path_end - path_begin), path_begin);
    if (ImGui::MenuItem(buf.c_str()))
    {
        buf.setf("%.*s", (int)(path_end - path_begin), path_begin);
        ImPathFixSeparatorsForCurrentOS(buf.c_str());
        ImOsOpenInShell(buf.c_str());
    }

    // Open folder
    const char* folder_begin = path_begin;
    const char* folder_end = ImPathFindFilename(path_begin, path_end);
    buf.setf("Open folder: %.*s", (int)(folder_end - folder_begin), path_begin);
    if (ImGui::MenuItem(buf.c_str()))
    {
        buf.setf("%.*s", (int)(folder_end - folder_begin), folder_begin);
        ImPathFixSeparatorsForCurrentOS(buf.c_str());
        ImOsOpenInShell(buf.c_str());
    }

    return true;
}

static bool ParseLineAndDrawFileOpenItem(ImGuiTestEngine* e, ImGuiTest* test, const char* line_start, const char* line_end)
{
    if (ParseLineAndDrawFileOpenItemForSourceFile(e, test, line_start, line_end))
        return true;
    if (ParseLineAndDrawFileOpenItemForImageFile(e, test, line_start, line_end, ".png"))
        return true;
    if (ParseLineAndDrawFileOpenItemForImageFile(e, test, line_start, line_end, ".gif"))
        return true;
    if (ParseLineAndDrawFileOpenItemForImageFile(e, test, line_start, line_end, ".mp4"))
        return true;
    return false;
}

static float GetDpiScale()
{
#ifdef IMGUI_HAS_VIEWPORT
    return ImGui::GetWindowViewport()->DpiScale;
#else
    return 1.0f;
#endif
}

static void DrawTestLog(ImGuiTestEngine* e, ImGuiTest* test)
{
    const ImU32 error_col = IM_COL32(255, 150, 150, 255);
    const ImU32 warning_col = IM_COL32(240, 240, 150, 255);
    const ImU32 unimportant_col = IM_COL32(190, 190, 190, 255);
    const float dpi_scale = GetDpiScale();

    ImGuiTestLog* log = &test->TestLog;
    const char* text = test->TestLog.Buffer.begin();
    const char* text_end = test->TestLog.Buffer.end();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 2.0f) * dpi_scale);
    ImGuiListClipper clipper;
    ImGuiTestVerboseLevel max_log_level = test->Status == ImGuiTestStatus_Error ? e->IO.ConfigVerboseLevelOnError : e->IO.ConfigVerboseLevel;
    int line_count = log->ExtractLinesForVerboseLevels(ImGuiTestVerboseLevel_Silent, max_log_level, NULL);
    int current_index_clipped = -1;
    int current_index_abs = 0;
    clipper.Begin(line_count);
    while (clipper.Step())
    {
        for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd; line_no++)
        {
            // Advance index_by_log_level to find log entry indicated by line_no.
            ImGuiTestLogLineInfo* line_info = NULL;
            while (current_index_clipped < line_no)
            {
                line_info = &log->LineInfo[current_index_abs];
                if (line_info->Level <= max_log_level)
                    current_index_clipped++;
                current_index_abs++;
            }

            const char* line_start = text + line_info->LineOffset;
            const char* line_end = strchr(line_start, '\n');
            if (line_end == NULL)
                line_end = text_end;

            switch (line_info->Level)
            {
            case ImGuiTestVerboseLevel_Error:
                ImGui::PushStyleColor(ImGuiCol_Text, error_col);
                break;
            case ImGuiTestVerboseLevel_Warning:
                ImGui::PushStyleColor(ImGuiCol_Text, warning_col);
                break;
            case ImGuiTestVerboseLevel_Debug:
            case ImGuiTestVerboseLevel_Trace:
                ImGui::PushStyleColor(ImGuiCol_Text, unimportant_col);
                break;
            default:
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32_WHITE);
                break;
            }
            ImGui::TextUnformatted(line_start, line_end);
            ImGui::PopStyleColor();

            ImGui::PushID(line_no);
            if (ImGui::BeginPopupContextItem("Context", 1))
            {
                if (!ParseLineAndDrawFileOpenItem(e, test, line_start, line_end))
                    ImGui::MenuItem("No options", NULL, false, false);
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }
    ImGui::PopStyleVar();
}

#if IMGUI_VERSION_NUM <= 18963
namespace ImGui
{
    void SetItemTooltip(const char* fmt, ...)
    {
        if (ImGui::IsItemHovered())
        {
            va_list args;
            va_start(args, fmt);
            ImGui::SetTooltipV(fmt, args);
            va_end(args);
        }
    }
} // namespace ImGui
#endif

static bool ShowTestGroupFilterTest(ImGuiTestEngine* e, ImGuiTestGroup group, const char* filter, ImGuiTest* test)
{
    if (test->Group != group)
        return false;
    if (!ImGuiTestEngine_PassFilter(test, *filter ? filter : "all"))
        return false;
    if ((e->UiFilterByStatusMask & (1 << test->Status)) == 0)
        return false;
    return true;
}

static void GetFailingTestsAsString(ImGuiTestEngine* e, ImGuiTestGroup group, char separator, Str* out_string)
{
    IM_ASSERT(out_string != NULL);
    bool first = true;
    for (int i = 0; i < e->TestsAll.Size; i++)
    {
        ImGuiTest* failing_test = e->TestsAll[i];
        Str* filter = (group == ImGuiTestGroup_Tests) ? e->UiFilterTests : e->UiFilterPerfs;
        if (failing_test->Group != group)
            continue;
        if (failing_test->Status != ImGuiTestStatus_Error)
            continue;
        if (!ImGuiTestEngine_PassFilter(failing_test, filter->empty() ? "all" : filter->c_str()))
            continue;
        if (!first)
            out_string->append(separator);
        out_string->append(failing_test->Name);
        first = false;
    }
}

static void TestStatusButton(const char* id, const ImVec4& color, bool running)
{
    ImGuiContext& g = *GImGui;
    ImGui::PushItemFlag(ImGuiItemFlags_NoTabStop, true);
    ImGui::ColorButton(id, color, ImGuiColorEditFlags_NoTooltip);
    ImGui::PopItemFlag();
    if (running)
    {
        //ImRect r = g.LastItemData.Rect;
        ImVec2 center = g.LastItemData.Rect.GetCenter();
        float radius = ImFloor(ImMin(g.LastItemData.Rect.GetWidth(), g.LastItemData.Rect.GetHeight()) * 0.40f);
        float t = (float)(ImGui::GetTime() * 20.0f);
        ImVec2 off(ImCos(t) * radius, ImSin(t) * radius);
        ImGui::GetWindowDrawList()->AddLine(center - off, center + off, ImGui::GetColorU32(ImGuiCol_Text), 1.5f);
        //ImGui::RenderText(r.Min + style.FramePadding + ImVec2(0, 0), &"|\0/\0-\0\\"[(((ImGui::GetFrameCount() / 5) & 3) << 1)], NULL);
    }
}

static void ShowTestGroup(ImGuiTestEngine* e, ImGuiTestGroup group, Str* filter)
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImGuiIO& io = ImGui::GetIO();
    const float dpi_scale = GetDpiScale();

    // Save position of test run status button and make space for it.
    const ImVec2 status_button_pos = ImGui::GetCursorPos();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetFrameHeight() + style.ItemInnerSpacing.x);

    //ImGui::Text("TESTS (%d)", engine->TestsAll.Size);
#if IMGUI_VERSION_NUM >= 18837
    if (ImGui::Button("Run") || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_R))
#else
    if (ImGui::Button("Run"))
#endif
    {
        for (int n = 0; n < e->TestsAll.Size; n++)
        {
            ImGuiTest* test = e->TestsAll[n];
            if (!ShowTestGroupFilterTest(e, group, filter->c_str(), test))
                continue;
            ImGuiTestEngine_QueueTest(e, test, ImGuiTestRunFlags_None);
        }
    }
    ImGui::SameLine();

    {
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
        const char* filter_by_status_desc = "";
        if (e->UiFilterByStatusMask == ~0u)
            filter_by_status_desc = "All";
        else if (e->UiFilterByStatusMask == ~(1u << ImGuiTestStatus_Success))
            filter_by_status_desc = "Not OK";
        else if (e->UiFilterByStatusMask == (1u << ImGuiTestStatus_Error))
            filter_by_status_desc = "Errors";
        if (ImGui::BeginCombo("##filterbystatus", filter_by_status_desc))
        {
            if (ImGui::Selectable("All", e->UiFilterByStatusMask == ~0u))
                e->UiFilterByStatusMask = (ImU32)~0u;
            if (ImGui::Selectable("Not OK", e->UiFilterByStatusMask == ~(1u << ImGuiTestStatus_Success)))
                e->UiFilterByStatusMask = (ImU32)~(1u << ImGuiTestStatus_Success);
            if (ImGui::Selectable("Errors", e->UiFilterByStatusMask == (1u << ImGuiTestStatus_Error)))
                e->UiFilterByStatusMask = (ImU32)(1u << ImGuiTestStatus_Error);
            ImGui::EndCombo();
        }
    }

    ImGui::SameLine();
    const char* perflog_label = "Perf Tool";
    float filter_width = ImGui::GetWindowContentRegionMax().x - ImGui::GetCursorPos().x;
    float perf_stress_factor_width = (30 * dpi_scale);
    if (group == ImGuiTestGroup_Perfs)
    {
        filter_width -= style.ItemSpacing.x + perf_stress_factor_width;
        filter_width -= style.ItemSpacing.x + style.FramePadding.x * 2 + ImGui::CalcTextSize(perflog_label).x;
    }
    filter_width -= ImGui::CalcTextSize("(?)").x + style.ItemSpacing.x;
    ImGui::SetNextItemWidth(ImMax(20.0f, filter_width));
    ImGui::InputText("##filter", filter);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    ImGui::SetItemTooltip("Query is composed of one or more comma-separated filter terms with optional modifiers.\n"
        "Available modifiers:\n"
        "- '-' prefix excludes tests matched by the term.\n"
        "- '^' prefix anchors term matching to the start of the string.\n"
        "- '$' suffix anchors term matching to the end of the string.");
    if (group == ImGuiTestGroup_Perfs)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(perf_stress_factor_width);
        ImGui::DragInt("##PerfStress", &e->IO.PerfStressAmount, 0.1f, 1, 20, "x%d");
        ImGui::SetItemTooltip("Increase workload of performance tests (higher means longer run)."); // FIXME: Move?
        ImGui::SameLine();
        if (ImGui::Button(perflog_label))
        {
            e->UiPerfToolOpen = true;
            ImGui::FocusWindow(ImGui::FindWindowByName("Dear ImGui Perf Tool"));
        }
    }

    int tests_completed = 0;
    int tests_succeeded = 0;
    int tests_failed = 0;
    if (ImGui::BeginTable("Tests", 3, ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Status");
        ImGui::TableSetupColumn("Category");
        ImGui::TableSetupColumn("Test", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 4) * dpi_scale);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 0) * dpi_scale);
        //ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(100, 10) * dpi_scale);
        for (int test_n = 0; test_n < e->TestsAll.Size; test_n++)
        {
            ImGuiTest* test = e->TestsAll[test_n];
            if (!ShowTestGroupFilterTest(e, group, filter->c_str(), test))
                continue;

            ImGuiTestContext* test_context = (e->TestContext && e->TestContext->Test == test) ? e->TestContext : NULL;

            ImGui::TableNextRow();
            ImGui::PushID(test_n);

            // Colors match general test status colors defined below.
            ImVec4 status_color;
            switch (test->Status)
            {
            case ImGuiTestStatus_Error:
                status_color = ImVec4(0.9f, 0.1f, 0.1f, 1.0f);
                tests_completed++;
                tests_failed++;
                break;
            case ImGuiTestStatus_Success:
                status_color = ImVec4(0.1f, 0.9f, 0.1f, 1.0f);
                tests_completed++;
                tests_succeeded++;
                break;
            case ImGuiTestStatus_Queued:
            case ImGuiTestStatus_Running:
            case ImGuiTestStatus_Suspended:
                if (test_context && (test_context->RunFlags & ImGuiTestRunFlags_GuiFuncOnly))
                    status_color = ImVec4(0.8f, 0.0f, 0.8f, 1.0f);
                else
                    status_color = ImVec4(0.8f, 0.4f, 0.1f, 1.0f);
                break;
            default:
                status_color = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
                break;
            }

            ImGui::TableNextColumn();
            TestStatusButton("status", status_color, test->Status == ImGuiTestStatus_Running || test->Status == ImGuiTestStatus_Suspended);
            ImGui::SameLine();

            bool queue_test = false;
            bool queue_gui_func_toggle = false;
            bool select_test = false;

            if (test->Status == ImGuiTestStatus_Suspended)
            {
                // Resume IM_SUSPEND_TESTFUNC
                // FIXME: Terrible user experience to have this here.
                if (ImGui::Button("Con###Run"))
                    test->Status = ImGuiTestStatus_Running;
                ImGui::SetItemTooltip("CTRL+Space to continue.");
                if (ImGui::IsKeyPressed(ImGuiKey_Space) && io.KeyCtrl)
                    test->Status = ImGuiTestStatus_Running;
            }
            else
            {
                if (ImGui::Button("Run###Run"))
                   queue_test = select_test = true;
            }

            ImGui::TableNextColumn();
            if (ImGui::Selectable(test->Category, test == e->UiSelectedTest, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_SelectOnNav))
                select_test = true;

            // Double-click to run test, CTRL+Double-click to run GUI function
            const bool is_running_gui_func = (test_context && (test_context->RunFlags & ImGuiTestRunFlags_GuiFuncOnly));
            const bool has_gui_func = (test->GuiFunc != NULL);
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
            {
                if (ImGui::GetIO().KeyCtrl)
                    queue_gui_func_toggle = true;
                else
                    queue_test = true;
            }

            /*if (ImGui::IsItemHovered() && test->TestLog.size() > 0)
            {
            ImGui::BeginTooltip();
            DrawTestLog(engine, test, false);
            ImGui::EndTooltip();
            }*/

            if (e->UiSelectAndScrollToTest == test)
                ImGui::SetScrollHereY();

            bool view_source = false;
            if (ImGui::BeginPopupContextItem())
            {
                select_test = true;

                if (ImGui::MenuItem("Run test"))
                    queue_test = true;
                if (ImGui::MenuItem("Run GUI func", "Ctrl+DblClick", is_running_gui_func, has_gui_func))
                    queue_gui_func_toggle = true;

                ImGui::Separator();

                const bool open_source_available = (test->SourceFile != NULL) && (e->IO.SrcFileOpenFunc != NULL);

                Str128 buf;
                if (test->SourceFile != NULL) // This is normally set by IM_REGISTER_TEST() but custom registration may omit it.
                    buf.setf("Open source (%s:%d)", ImPathFindFilename(test->SourceFile), test->SourceLine);
                else
                    buf.set("Open source");
                if (ImGui::MenuItem(buf.c_str(), NULL, false, open_source_available))
                    e->IO.SrcFileOpenFunc(test->SourceFile, test->SourceLine, e->IO.SrcFileOpenUserData);
                if (ImGui::MenuItem("View source...", NULL, false, test->SourceFile != NULL))
                    view_source = true;

                if (group == ImGuiTestGroup_Perfs && ImGui::MenuItem("View perflog"))
                {
                    e->PerfTool->ViewOnly(test->Name);
                    e->UiPerfToolOpen = true;
                }

                ImGui::Separator();
                if (ImGui::MenuItem("Copy name", NULL, false))
                    ImGui::SetClipboardText(test->Name);

                if (test->Status == ImGuiTestStatus_Error)
                    if (ImGui::MenuItem("Copy names of all failing tests"))
                    {
                        Str256 failing_tests;
                        GetFailingTestsAsString(e, group, ',', &failing_tests);
                        ImGui::SetClipboardText(failing_tests.c_str());
                    }

                ImGuiTestLog* test_log = &test->TestLog;
                if (ImGui::BeginMenu("Copy log", !test_log->IsEmpty()))
                {
                    for (int level_n = ImGuiTestVerboseLevel_Error; level_n < ImGuiTestVerboseLevel_COUNT; level_n++)
                    {
                        ImGuiTestVerboseLevel level = (ImGuiTestVerboseLevel)level_n;
                        int count = test_log->ExtractLinesForVerboseLevels((ImGuiTestVerboseLevel)0, level, NULL);
                        if (ImGui::MenuItem(Str64f("%s (%d lines)", ImGuiTestEngine_GetVerboseLevelName(level), count).c_str(), NULL, false, count > 0))
                        {
                            ImGuiTextBuffer buffer;
                            test_log->ExtractLinesForVerboseLevels((ImGuiTestVerboseLevel)0, level, &buffer);
                            ImGui::SetClipboardText(buffer.c_str());
                        }
                    }
                    ImGui::EndMenu();
                }

                if (ImGui::MenuItem("Clear log", NULL, false, !test_log->IsEmpty()))
                    test_log->Clear();

                ImGui::EndPopup();
            }

            // Process source popup
            static ImGuiTextBuffer source_blurb;
            static int goto_line = -1;
            if (view_source)
            {
                source_blurb.clear();
                size_t file_size = 0;
                char* file_data = (char*)ImFileLoadToMemory(test->SourceFile, "rb", &file_size);
                if (file_data)
                    source_blurb.append(file_data, file_data + file_size);
                else
                    source_blurb.append("<Error loading sources>");
                goto_line = (test->SourceLine + test->SourceLineEnd) / 2;
                ImGui::OpenPopup("Source");
            }
            if (ImGui::BeginPopup("Source"))
            {
                // FIXME: Local vs screen pos too messy :(
                const ImVec2 start_pos = ImGui::GetCursorStartPos();
                const float line_height = ImGui::GetTextLineHeight();
                if (goto_line != -1)
                    ImGui::SetScrollFromPosY(start_pos.y + (goto_line - 1) * line_height, 0.5f);
                goto_line = -1;

                ImRect r(0.0f, test->SourceLine * line_height, ImGui::GetWindowWidth(), (test->SourceLine + 1) * line_height); // SourceLineEnd is too flaky
                ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetWindowPos() + start_pos + r.Min, ImGui::GetWindowPos() + start_pos + r.Max, IM_COL32(80, 80, 150, 150));

                ImGui::TextUnformatted(source_blurb.c_str(), source_blurb.end());
                ImGui::EndPopup();
            }

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(test->Name);

            // Process selection
            if (select_test)
                e->UiSelectedTest = test;

            // Process queuing
            if (queue_gui_func_toggle && is_running_gui_func)
                ImGuiTestEngine_AbortCurrentTest(e);
            else if (queue_gui_func_toggle && !e->IO.IsRunningTests)
                ImGuiTestEngine_QueueTest(e, test, ImGuiTestRunFlags_ManualRun | ImGuiTestRunFlags_GuiFuncOnly);
            if (queue_test && !e->IO.IsRunningTests)
                ImGuiTestEngine_QueueTest(e, test, ImGuiTestRunFlags_ManualRun);

            ImGui::PopID();
        }
        ImGui::Spacing();
        ImGui::PopStyleVar(2);
        ImGui::EndTable();
    }

    // Display test status recap (colors match per-test run button colors defined above)
    {
        ImVec4 status_color;
        if (tests_failed > 0)
            status_color = ImVec4(0.9f, 0.1f, 0.1f, 1.0f);
        else if (e->IO.IsRunningTests)
            status_color = ImVec4(0.8f, 0.4f, 0.1f, 1.0f);
        else if (tests_succeeded > 0 && tests_completed == tests_succeeded)
            status_color = ImVec4(0.1f, 0.9f, 0.1f, 1.0f);
        else
            status_color = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
        //ImVec2 cursor_pos_bkp = ImGui::GetCursorPos();
        ImGui::SetCursorPos(status_button_pos);
        TestStatusButton("status", status_color, false);// e->IO.IsRunningTests);
        ImGui::SetItemTooltip("Filtered: %d\n- OK: %d\n- Errors: %d", tests_completed, tests_succeeded, tests_failed);
        //ImGui::SetCursorPos(cursor_pos_bkp);  // Restore cursor position for rendering further widgets
    }
}

static void ImGuiTestEngine_ShowLogAndTools(ImGuiTestEngine* engine)
{
    ImGuiContext& g = *GImGui;
    const float dpi_scale = GetDpiScale();

    if (!ImGui::BeginTabBar("##tools"))
        return;

    if (ImGui::BeginTabItem("LOG"))
    {
        if (engine->UiSelectedTest)
            ImGui::Text("Log for '%s' '%s'", engine->UiSelectedTest->Category, engine->UiSelectedTest->Name);
        else
            ImGui::Text("N/A");
        if (ImGui::SmallButton("Clear"))
            if (engine->UiSelectedTest)
                engine->UiSelectedTest->TestLog.Clear();
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy to clipboard"))
            if (engine->UiSelectedTest)
                ImGui::SetClipboardText(engine->UiSelectedTest->TestLog.Buffer.c_str());
        ImGui::Separator();

        ImGui::BeginChild("Log");
        if (engine->UiSelectedTest)
        {
            DrawTestLog(engine, engine->UiSelectedTest);
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY();
        }
        ImGui::EndChild();
        ImGui::EndTabItem();
    }

    // Options
    if (ImGui::BeginTabItem("OPTIONS"))
    {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("%.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
        ImGui::Text("TestEngine: HookItems: %d, HookPushId: %d, InfoTasks: %d", g.TestEngineHookItems, g.DebugHookIdInfo != 0, engine->InfoTasks.Size);
        ImGui::Separator();

        if (ImGui::Button("Reboot UI context"))
            engine->ToolDebugRebootUiContext = true;

        const ImGuiInputTextCallback filter_callback = [](ImGuiInputTextCallbackData* data) { return (data->EventChar == ',' || data->EventChar == ';') ? 1 : 0; };
        ImGui::InputText("Branch/Annotation", engine->IO.GitBranchName, IM_ARRAYSIZE(engine->IO.GitBranchName), ImGuiInputTextFlags_CallbackCharFilter, filter_callback, NULL);
        ImGui::SetItemTooltip("This will be stored in the CSV file for performance tools.");

        ImGui::Separator();

        if (ImGui::TreeNode("Screen/video capture"))
        {
            ImGui::Checkbox("Capture when requested by API", &engine->IO.ConfigCaptureEnabled);
            ImGui::SetItemTooltip("Enable or disable screen capture API completely.");
            ImGui::Checkbox("Capture screen on error", &engine->IO.ConfigCaptureOnError);
            ImGui::SetItemTooltip("Capture a screenshot on test failure.");

            // Fields modified by in this call will be synced to engine->CaptureContext.
            engine->CaptureTool._ShowEncoderConfigFields(&engine->CaptureContext);

            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Performances"))
        {
            ImGui::Checkbox("Slow down whole app", &engine->ToolSlowDown);
            ImGui::SameLine(); ImGui::SetNextItemWidth(70 * dpi_scale);
            ImGui::SliderInt("##ms", &engine->ToolSlowDownMs, 0, 400, "%d ms");

            // FIXME-TESTS: Need to be visualizing the samples/spikes.
            double dt_1 = 1.0 / ImGui::GetIO().Framerate;
            double fps_now = 1.0 / dt_1;
            double dt_100 = engine->PerfDeltaTime100.GetAverage();
            double dt_500 = engine->PerfDeltaTime500.GetAverage();

            //if (engine->PerfRefDeltaTime <= 0.0 && engine->PerfRefDeltaTime.IsFull())
            //    engine->PerfRefDeltaTime = dt_2000;

            ImGui::Checkbox("Unthrolled", &engine->IO.ConfigNoThrottle);
            ImGui::SameLine();
            if (ImGui::Button("Pick ref dt"))
                engine->PerfRefDeltaTime = dt_500;

            double dt_ref = engine->PerfRefDeltaTime;
            ImGui::Text("[ref dt]    %6.3f ms", engine->PerfRefDeltaTime * 1000);
            ImGui::Text("[last 001] %6.3f ms (%.1f FPS) ++ %6.3f ms", dt_1 * 1000.0, 1.0 / dt_1, (dt_1 - dt_ref) * 1000);
            ImGui::Text("[last 100] %6.3f ms (%.1f FPS) ++ %6.3f ms ~ converging in %.1f secs", dt_100 * 1000.0, 1.0 / dt_100, (dt_1 - dt_ref) * 1000, 100.0 / fps_now);
            ImGui::Text("[last 500] %6.3f ms (%.1f FPS) ++ %6.3f ms ~ converging in %.1f secs", dt_500 * 1000.0, 1.0 / dt_500, (dt_1 - dt_ref) * 1000, 500.0 / fps_now);

            //ImGui::PlotLines("Last 100", &engine->PerfDeltaTime100.Samples.Data, engine->PerfDeltaTime100.Samples.Size, engine->PerfDeltaTime100.Idx, NULL, 0.0f, dt_1000 * 1.10f, ImVec2(0.0f, ImGui::GetFontSize()));
            ImVec2 plot_size(0.0f, ImGui::GetFrameHeight() * 3);
            ImMovingAverage<double>* ma = &engine->PerfDeltaTime500;
            ImGui::PlotLines("Last 500",
                [](void* data, int n) { ImMovingAverage<double>* ma = (ImMovingAverage<double>*)data; return (float)(ma->Samples[n] * 1000); },
                ma, ma->Samples.Size, 0 * ma->Idx, NULL, 0.0f, (float)(ImMax(dt_100, dt_500) * 1000.0 * 1.2f), plot_size);

            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Dear ImGui Configuration Flags"))
        {
            ImGui::CheckboxFlags("io.ConfigFlags: NavEnableKeyboard", &io.ConfigFlags, ImGuiConfigFlags_NavEnableKeyboard);
            ImGui::CheckboxFlags("io.ConfigFlags: NavEnableGamepad", &io.ConfigFlags, ImGuiConfigFlags_NavEnableGamepad);
#ifdef IMGUI_HAS_DOCK
            ImGui::Checkbox("io.ConfigDockingAlwaysTabBar", &io.ConfigDockingAlwaysTabBar);
#endif
            ImGui::TreePop();
        }

        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

static void ImGuiTestEngine_ShowTestTool(ImGuiTestEngine* engine, bool* p_open)
{
    const float dpi_scale = GetDpiScale();

    if (engine->UiFocus)
    {
        ImGui::SetNextWindowFocus();
        engine->UiFocus = false;
    }
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 50, ImGui::GetFontSize() * 40), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Dear ImGui Test Engine", p_open, ImGuiWindowFlags_MenuBar))
    {
        ImGui::End();
        return;
    }

    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("Tools"))
        {
            ImGuiContext& g = *GImGui;
            ImGui::MenuItem("Metrics/Debugger", "", &engine->UiMetricsOpen);
            ImGui::MenuItem("Debug Log", "", &engine->UiDebugLogOpen);
            ImGui::MenuItem("Stack Tool", "", &engine->UiStackToolOpen);
            ImGui::MenuItem("Item Picker", "", &g.DebugItemPickerActive);
            ImGui::Separator();
            ImGui::MenuItem("Capture Tool", "", &engine->UiCaptureToolOpen);
            ImGui::MenuItem("Perf Tool", "", &engine->UiPerfToolOpen);
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    ImGui::SetNextItemWidth(90 * dpi_scale);
    if (ImGui::BeginCombo("##RunSpeed", ImGuiTestEngine_GetRunSpeedName(engine->IO.ConfigRunSpeed), ImGuiComboFlags_None))
    {
        for (ImGuiTestRunSpeed level = (ImGuiTestRunSpeed)0; level < ImGuiTestRunSpeed_COUNT; level = (ImGuiTestRunSpeed)(level + 1))
            if (ImGui::Selectable(ImGuiTestEngine_GetRunSpeedName(level), engine->IO.ConfigRunSpeed == level))
                engine->IO.ConfigRunSpeed = level;
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip(
        "Running speed\n"
        "- Fast: Run tests as fast as possible (no delay/vsync, teleport mouse, etc.).\n"
        "- Normal: Run tests at human watchable speed (for debugging).\n"
        "- Cinematic: Run tests with pauses between actions (for e.g. tutorials)."
    );
    ImGui::SameLine();
    //ImGui::Checkbox("Fast", &engine->IO.ConfigRunFast);
    //ImGui::SameLine();
    ImGui::Checkbox("Stop", &engine->IO.ConfigStopOnError);
    ImGui::SetItemTooltip("Stop running tests when hitting an error.");
    ImGui::SameLine();
    ImGui::Checkbox("DbgBrk", &engine->IO.ConfigBreakOnError);
    ImGui::SetItemTooltip("Break in debugger when hitting an error.");
    ImGui::SameLine();
    ImGui::Checkbox("KeepGUI", &engine->IO.ConfigKeepGuiFunc);
    ImGui::SetItemTooltip("Keep GUI function running after a test fails, or when a single queued test is finished.\nHold ESC to abort a running GUI function.");
    ImGui::SameLine();
    ImGui::Checkbox("Refocus", &engine->IO.ConfigTakeFocusBackAfterTests);
    ImGui::SetItemTooltip("Set focus back to Test window after running tests.");
    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70 * dpi_scale);
    if (ImGui::BeginCombo("##Verbose", ImGuiTestEngine_GetVerboseLevelName(engine->IO.ConfigVerboseLevel), ImGuiComboFlags_None))
    {
        for (ImGuiTestVerboseLevel level = (ImGuiTestVerboseLevel)0; level < ImGuiTestVerboseLevel_COUNT; level = (ImGuiTestVerboseLevel)(level + 1))
            if (ImGui::Selectable(ImGuiTestEngine_GetVerboseLevelName(level), engine->IO.ConfigVerboseLevel == level))
                engine->IO.ConfigVerboseLevel = engine->IO.ConfigVerboseLevelOnError = level;
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Verbose level.");
    //ImGui::PopStyleVar();
    ImGui::Separator();

    // SPLITTER
    // FIXME-OPT: A better splitter API supporting arbitrary number of splits would be useful.
    float list_height = 0.0f;
    float& log_height = engine->UiLogHeight;
    ImGui::Splitter("splitter", &list_height, &log_height, ImGuiAxis_Y, +1);

    // TESTS
    ImGui::BeginChild("List", ImVec2(0, list_height), false, ImGuiWindowFlags_NoScrollbar);
    if (ImGui::BeginTabBar("##Tests", ImGuiTabBarFlags_NoTooltip))  // Add _NoPushId flag in TabBar?
    {
        if (ImGui::BeginTabItem("TESTS", NULL, ImGuiTabItemFlags_NoPushId))
        {
            ShowTestGroup(engine, ImGuiTestGroup_Tests, engine->UiFilterTests);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("PERFS", NULL, ImGuiTabItemFlags_NoPushId))
        {
            ShowTestGroup(engine, ImGuiTestGroup_Perfs, engine->UiFilterPerfs);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
    engine->UiSelectAndScrollToTest = NULL;

    // LOG & TOOLS
    ImGui::BeginChild("Log", ImVec2(0, log_height));
    ImGuiTestEngine_ShowLogAndTools(engine);
    ImGui::EndChild();

    ImGui::End();
}

void    ImGuiTestEngine_ShowTestEngineWindows(ImGuiTestEngine* e, bool* p_open)
{
    // Test Tool
    ImGuiTestEngine_ShowTestTool(e, p_open);

    // Stack Tool
    if (e->UiStackToolOpen)
        ImGui::ShowStackToolWindow(&e->UiStackToolOpen);

    // Capture Tool
    if (e->UiCaptureToolOpen)
        e->CaptureTool.ShowCaptureToolWindow(&e->CaptureContext, &e->UiCaptureToolOpen);

    // Performance tool
    if (e->UiPerfToolOpen)
        e->PerfTool->ShowPerfToolWindow(e, &e->UiPerfToolOpen);;

    // Show Dear ImGui windows
    // (we cannot show demo window here because it could lead to duplicate display, which demo windows isn't guarded for)
    if (e->UiMetricsOpen)
        ImGui::ShowMetricsWindow(&e->UiMetricsOpen);
    if (e->UiDebugLogOpen)
        ImGui::ShowDebugLogWindow(&e->UiDebugLogOpen);
}
