// dear imgui test engine
// (helpers/utilities. do NOT use this as a general purpose library)

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "imgui_te_utils.h"
#include "../imgui.h"
#include "../imgui_internal.h"
#define STR_IMPLEMENTATION
#include "thirdparty/Str/Str.h"

#if defined(_WIN32)
#if !defined(_WINDOWS_)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <shellapi.h>   // ShellExecuteA()
#include <stdio.h>
#else
#include <errno.h>
#include <unistd.h>
#endif
#ifndef _MSC_VER
#include <sys/types.h>
#include <sys/stat.h>   // stat()
#endif

#if defined(__linux) || defined(__linux__) || defined(__MACH__) || defined(__MSL__) || defined(__MINGW32__)
#include <pthread.h>    // pthread_setname_np()
#endif
#include <chrono>       // high_resolution_clock::now()
#include <thread>       // this_thread::sleep_for()

//-----------------------------------------------------------------------------
// Hashing Helpers
//-----------------------------------------------------------------------------
// - ImHashDecoratedPathParseLiteral() [internal]
// - ImHashDecoratedPath()
// - ImFindNextDecoratedPartInPath()
//-----------------------------------------------------------------------------

// - Parse literals encoded as "$$xxxx/" and incorporate into our hash based on type.
// - $$ not passed by caller.
static ImGuiID ImHashDecoratedPathParseLiteral(ImGuiID crc, const unsigned char* str, const unsigned char* str_end, const unsigned char** out_str_remaining)
{
    // Parse type (default to int)
    ImGuiDataType type = ImGuiDataType_S32;
    if (*str == '(')
    {
        // "$$(int)????" where ???? is s32 or u32
        if (str + 5 < str_end && memcmp(str, "(int)", 5) == 0)
        {
            type = ImGuiDataType_S32;
            str += 5;
        }
        // "$$(ptr)0x????" where ???? is ptr size
        else if (str + 7 < str_end && memcmp(str, "(ptr)0x", 7) == 0)
        {
            type = ImGuiDataType_Pointer;
            str += 7;
        }
    }

    // Parse value
    switch (type)
    {
    case ImGuiDataType_S32:
    {
        // e.g. "$$(int)123" for s32/u32/ImGuiID, same as PushID(int)
        int v = 0;
        {
            int negative = 0;
            if (str < str_end && *str == '-') { negative = 1; str++; }
            if (str < str_end && *str == '+') { str++; }
            for (char c = *str; str < str_end; c = *(++str))
            {
                if (c >= '0' && c <= '9') { v = (v * 10) + (c - '0'); }
                else break;
            }
            if (negative)
                v = -v;
        }
        crc = ~ImHashData(&v, sizeof(int), ~crc);
        break;
    }
    case ImGuiDataType_Pointer:
    {
        // e.g. "$$(ptr)0x1234FFFF" for pointers, same as PushID(void*)
        intptr_t v = 0;
        {
            for (char c = *str; str < str_end; c = *(++str))
            {
                if (c >= '0' && c <= '9')       { v = (v << 4) + (c - '0'); }
                else if (c >= 'A' && c <= 'F')  { v = (v << 4) + 10 + (c - 'A'); }
                else if (c >= 'a' && c <= 'f')  { v = (v << 4) + 10 + (c - 'a'); }
                else break;
            }
        }
        crc = ~ImHashData(&v, sizeof(void*), ~crc);
        break;
    }
    }

    // "$$xxxx" must always be either end of string, either leading to a next section e.g. "$$xxxx/"
    IM_ASSERT(str == str_end || *str == '/');

    *out_str_remaining = str;
    return crc;
}

// Hash "hello/world" as if it was "helloworld"
// To hash a forward slash we need to use "hello\\/world"
//   IM_ASSERT(ImHashDecoratedPath("Hello/world")   == ImHashStr("Helloworld", 0));
//   IM_ASSERT(ImHashDecoratedPath("Hello\\/world") == ImHashStr("Hello/world", 0));
//   IM_ASSERT(ImHashDecoratedPath("$$1")           == (n = 1, ImHashData(&n, sizeof(int))));
// Adapted from ImHash(). Not particularly fast!
ImGuiID ImHashDecoratedPath(const char* str, const char* str_end, ImGuiID seed)
{
    static ImU32 crc32_lut[256] = { 0 };
    if (!crc32_lut[1])
    {
        const ImU32 polynomial = 0xEDB88320;
        for (ImU32 i = 0; i < 256; i++)
        {
            ImU32 crc = i;
            for (ImU32 j = 0; j < 8; j++)
                crc = (crc >> 1) ^ (ImU32(-int(crc & 1)) & polynomial);
            crc32_lut[i] = crc;
        }
    }

    // Prefixing the string with / ignore the seed
    if (str != str_end && str[0] == '/')
        seed = 0;

    seed = ~seed;
    ImU32 crc = seed;

    // Focus for non-zero terminated string for consistency
    if (str_end == NULL)
        str_end = str + strlen(str);

    bool inhibit_one = false;
    bool new_section = true;
    const unsigned char* current = (const unsigned char*)str;
    while (current < (const unsigned char*)str_end)
    {
        const unsigned char c = *current++;

        // Backslash to inhibit special behavior of following character
        if (c == '\\' && !inhibit_one)
        {
            inhibit_one = true;
            continue;
        }

        // Forward slashes are ignored unless prefixed with a backward slash
        if (c == '/' && !inhibit_one)
        {
            inhibit_one = false;
            new_section = true;
            seed = crc; // Set seed to the new path
            continue;
        }

        // $$ at the beginning of a section to encode literals.
        // - Currently: "$$????" = hash of 1 as int
        // - May add pointers and other types.
        if (c == '$' && current[0] == '$' && !inhibit_one && new_section)
        {
            crc = ImHashDecoratedPathParseLiteral(crc, current + 1, (const unsigned char*)str_end, &current);
            continue;
        }

        // Reset the hash when encountering ###
        if (c == '#' && current[0] == '#' && current[1] == '#')
            crc = seed;

        // Hash byte
        crc = (crc >> 8) ^ crc32_lut[(crc & 0xFF) ^ c];

        inhibit_one = new_section = false;
    }
    return ~crc;
}

// Returns a next element of decorated hash path.
//    "//hello/world/child" --> "world/child"
//    "world/child"         --> "child"
// This is a helper for code needing to do some parsing of individual nodes in a path.
// Note: we need the (unsigned char*) stuff in order to keep code similar to ImHashDecoratedPath(). They are not really necessary in this function tho.
const char* ImFindNextDecoratedPartInPath(const char* str, const char* str_end)
{
    const unsigned char* current = (const unsigned char*)str;
    while (*current == '/')
        current++;

    bool inhibit_one = false;
    while (true)
    {
        if (str_end != NULL && current == (const unsigned char*)str_end)
            break;

        const unsigned char c = *current++;
        if (c == 0)
            break;
        if (c == '\\' && !inhibit_one)
        {
            inhibit_one = true;
            continue;
        }

        // Forward slashes are ignored unless prefixed with a backward slash
        if (c == '/' && !inhibit_one)
            return (const char*)current;

        inhibit_one = false;
    }
    return NULL;
}

//-----------------------------------------------------------------------------
// File/Directory Helpers
//-----------------------------------------------------------------------------
// - ImFileExist()
// - ImFileCreateDirectoryChain()
// - ImFileFindInParents()
// - ImFileLoadSourceBlurb()
//-----------------------------------------------------------------------------

#if _WIN32
static const char IM_DIR_SEPARATOR = '\\';

static void ImUtf8ToWideChar(const char* multi_byte, ImVector<wchar_t>* buf)
{
    const int wsize = ::MultiByteToWideChar(CP_UTF8, 0, multi_byte, -1, NULL, 0);
    buf->resize(wsize);
    ::MultiByteToWideChar(CP_UTF8, 0, multi_byte, -1, (wchar_t*)buf->Data, wsize);
}
#else
static const char IM_DIR_SEPARATOR = '/';
#endif

bool ImFileExist(const char* filename)
{
    struct stat dir_stat;
    int ret = stat(filename, &dir_stat);
    return (ret == 0);
}

bool ImFileDelete(const char* filename)
{
#if _WIN32
    ImVector<wchar_t> buf;
    ImUtf8ToWideChar(filename, &buf);
    return ::DeleteFileW(&buf[0]) == TRUE;
#else
    unlink(filename);
#endif
    return false;
}

// Create directories for specified path. Slashes will be replaced with platform directory separators.
// e.g. ImFileCreateDirectoryChain("aaaa/bbbb/cccc.png")
// will try to create "aaaa/" then "aaaa/bbbb/".
bool ImFileCreateDirectoryChain(const char* path, const char* path_end)
{
    IM_ASSERT(path != NULL);
    IM_ASSERT(path[0] != 0);

    if (path_end == NULL)
        path_end = path + strlen(path);

    // Copy in a local, zero-terminated buffer
    size_t path_len = (size_t)(path_end - path);
    char* path_local = (char*)IM_ALLOC(path_len + 1);
    memcpy(path_local, path, path_len);
    path_local[path_len] = 0;

#if defined(_WIN32)
    ImVector<ImWchar> buf;
#endif
    // Modification of passed file_name allows us to avoid extra temporary memory allocation.
    // strtok() pokes \0 into places where slashes are, we create a directory using directory_name and restore slash.
    for (char* token = strtok(path_local, "\\/"); token != NULL; token = strtok(NULL, "\\/"))
    {
        // strtok() replaces slashes with NULLs. Overwrite removed slashes here with the type of slashes the OS needs (win32 functions need backslashes).
        if (token != path_local)
            *(token - 1) = IM_DIR_SEPARATOR;

#if defined(_WIN32)
        // Use ::CreateDirectoryW() because ::CreateDirectoryA() treat filenames in the local code-page instead of UTF-8.
        const int filename_wsize = ImTextCountCharsFromUtf8(path_local, NULL) + 1;
        buf.resize(filename_wsize);
        ImTextStrFromUtf8(&buf[0], filename_wsize, path_local, NULL);
        if (!::CreateDirectoryW((wchar_t*)&buf[0], NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
#else
        if (mkdir(path_local, S_IRWXU) != 0 && errno != EEXIST)
#endif
        {
            IM_FREE(path_local);
            return false;
        }
    }
    IM_FREE(path_local);
    return true;
}

bool ImFileFindInParents(const char* sub_path, int max_parent_count, Str* output)
{
    IM_ASSERT(sub_path != NULL);
    IM_ASSERT(output != NULL);
    for (int parent_level = 0; parent_level < max_parent_count; parent_level++)
    {
        output->clear();
        for (int j = 0; j < parent_level; j++)
            output->append("../");
        output->append(sub_path);
        if (ImFileExist(output->c_str()))
            return true;
    }
    output->clear();
    return false;
}

bool ImFileLoadSourceBlurb(const char* file_name, int line_no_start, int line_no_end, ImGuiTextBuffer* out_buf)
{
    size_t file_size = 0;
    char* file_begin = (char*)ImFileLoadToMemory(file_name, "rb", &file_size, 1);
    if (file_begin == NULL)
        return false;

    char* file_end = file_begin + file_size;
    int line_no = 0;
    const char* test_src_begin = NULL;
    const char* test_src_end = NULL;
    for (const char* p = file_begin; p < file_end; )
    {
        line_no++;
        const char* line_begin = p;
        const char* line_end = ImStrchrRange(line_begin + 1, file_end, '\n');
        if (line_end == NULL)
            line_end = file_end;
        if (line_no >= line_no_start && line_no <= line_no_end)
        {
            if (test_src_begin == NULL)
                test_src_begin = line_begin;
            test_src_end = ImMax(test_src_end, line_end);
        }
        p = line_end + 1;
    }

    if (test_src_begin != NULL)
        out_buf->append(test_src_begin, test_src_end);
    else
        out_buf->clear();

    ImGui::MemFree(file_begin);
    return true;
}

//-----------------------------------------------------------------------------
// Path Helpers
//-----------------------------------------------------------------------------
// - ImPathFindFilename()
// - ImPathFindFileExt()
// - ImPathFixSeparatorsForCurrentOS()
//-----------------------------------------------------------------------------

const char* ImPathFindFilename(const char* path, const char* path_end)
{
    IM_ASSERT(path != NULL);
    if (!path_end)
        path_end = path + strlen(path);
    const char* p = path_end;
    while (p > path)
    {
        if (p[-1] == '/' || p[-1] == '\\')
            break;
        p--;
    }
    return p;
}

// "folder/filename" -> return pointer to "" (end of string)
// "folder/filename.png" -> return pointer to ".png"
// "folder/filename.png.bak" -> return pointer to ".png.bak"
const char* ImPathFindExtension(const char* path, const char* path_end)
{
    if (!path_end)
        path_end = path + strlen(path);
    const char* filename = ImPathFindFilename(path, path_end);
    const char* p = filename;
    while (p < path_end)
    {
        if (p[0] == '.')
            break;
        p++;
    }
    return p;
}

void ImPathFixSeparatorsForCurrentOS(char* buf)
{
#ifdef _WIN32
    for (char* p = buf; *p != 0; p++)
        if (*p == '/')
            *p = '\\';
#else
    for (char* p = buf; *p != 0; p++)
        if (*p == '\\')
            *p = '/';
#endif
}

//-----------------------------------------------------------------------------
// String Helpers
//-----------------------------------------------------------------------------

static const char* ImStrStr(const char* haystack, size_t hlen, const char* needle, int nlen)
{
    const char* end = haystack + hlen;
    const char* p = haystack;
    while ((p = (const char*)memchr(p, *needle, end - p)) != NULL)
    {
        if (end - p < nlen)
            return NULL;
        if (memcmp(p, needle, nlen) == 0)
            return p;
        p++;
    }
    return NULL;
}

void ImStrReplace(Str* s, const char* find, const char* repl)
{
    IM_ASSERT(find != NULL && *find);
    IM_ASSERT(repl != NULL);
    int find_len = (int)strlen(find);
    int repl_len = (int)strlen(repl);
    int repl_diff = repl_len - find_len;

    // Estimate required length of new buffer if string size increases.
    int need_capacity = s->capacity();
    int num_matches = INT_MAX;
    if (repl_diff > 0)
    {
        num_matches = 0;
        need_capacity = s->length() + 1;
        for (char* p = s->c_str(), *end = s->c_str() + s->length(); p != NULL && p < end;)
        {
            p = (char*)ImStrStr(p, end - p, find, find_len);
            if (p)
            {
                need_capacity += repl_diff;
                p += find_len;
                num_matches++;
            }
        }
    }

    if (num_matches == 0)
        return;

    const char* not_owned_data = s->owned() ? NULL : s->c_str();
    if (!s->owned() || need_capacity > s->capacity())
        s->reserve(need_capacity);
    if (not_owned_data != NULL)
        s->set(not_owned_data);

    // Replace data.
    for (char* p = s->c_str(), *end = s->c_str() + s->length(); p != NULL && p < end && num_matches--;)
    {
        p = (char*)ImStrStr(p, end - p, find, find_len);
        if (p)
        {
            memmove(p + repl_len, p + find_len, end - p - find_len + 1);
            memcpy(p, repl, repl_len);
            p += repl_len;
            end += repl_diff;
        }
    }
}

const char* ImStrchrRangeWithEscaping(const char* str, const char* str_end, char find_c)
{
    while (str < str_end)
    {
        const char c = *str;
        if (c == '\\')
        {
            str += 2;
            continue;
        }
        if (c == find_c)
            return str;
        str++;
    }
    return NULL;
}

// Suboptimal but ok for the data size we are dealing with (see commit on 2022/08/22 for a faster and more complicated version)
void ImStrXmlEscape(Str* s)
{
    ImStrReplace(s, "&", "&amp;");
    ImStrReplace(s, "<", "&lt;");
    ImStrReplace(s, ">", "&gt;");
    ImStrReplace(s, "\"", "&quot;");
    ImStrReplace(s, "\'", "&apos;");
}

// Based on code from https://github.com/EddieBreeg/C_b64 by @EddieBreeg.
int ImStrBase64Encode(const unsigned char* src, char* dst, int length)
{
    static const char* b64Table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";
    int i, j, k, l, encoded_len = 0;

    while (length > 0)
    {
        switch (length)
        {
        case 1:
            i = src[0] >> 2;
            j = (src[0] & 3) << 4;
            k = 64;
            l = 64;
            break;
        case 2:
            i = src[0] >> 2;
            j = ((src[0] & 3) << 4) | (src[1] >> 4);
            k = (src[1] & 15) << 2;
            l = 64;
            break;
        default:
            i = src[0] >> 2;
            j = ((src[0] & 3) << 4) | (src[1] >> 4);
            k = ((src[1] & 0xf) << 2) | (src[2] >> 6 & 3);
            l = src[2] & 0x3f;
            break;
        }
        dst[0] = b64Table[i];
        dst[1] = b64Table[j];
        dst[2] = b64Table[k];
        dst[3] = b64Table[l];
        src += 3;
        dst += 4;
        length -= 3;
        encoded_len += 4;
    }
    return encoded_len;
}

//-----------------------------------------------------------------------------
// Parsing Helpers
//-----------------------------------------------------------------------------
// - ImParseSplitCommandLine()
// - ImParseFindIniSection()
//-----------------------------------------------------------------------------

void    ImParseExtractArgcArgvFromCommandLine(int* out_argc, char const*** out_argv, const char* cmd_line)
{
    size_t cmd_line_len = strlen(cmd_line);

    int n = 1;
    {
        const char* p = cmd_line;
        while (*p != 0)
        {
            const char* arg = p;
            while (*arg == ' ')
                arg++;
            const char* arg_end = strchr(arg, ' ');
            if (arg_end == NULL)
                p = arg_end = cmd_line + cmd_line_len;
            else
                p = arg_end + 1;
            n++;
        }
    }

    int argc = n;
    char const** argv = (char const**)malloc(sizeof(char*) * ((size_t)argc + 1) + (cmd_line_len + 1));
    IM_ASSERT(argv != NULL);
    char* cmd_line_dup = (char*)argv + sizeof(char*) * ((size_t)argc + 1);
    strcpy(cmd_line_dup, cmd_line);

    {
        argv[0] = "main.exe";
        argv[argc] = NULL;

        char* p = cmd_line_dup;
        for (n = 1; n < argc; n++)
        {
            char* arg = p;
            char* arg_end = strchr(arg, ' ');
            if (arg_end == NULL)
                p = arg_end = cmd_line_dup + cmd_line_len;
            else
                p = arg_end + 1;
            argv[n] = arg;
            arg_end[0] = 0;
        }
    }

    *out_argc = argc;
    *out_argv = argv;
}

bool    ImParseFindIniSection(const char* ini_config, const char* header, ImVector<char>* result)
{
    IM_ASSERT(ini_config != NULL);
    IM_ASSERT(header != NULL);
    IM_ASSERT(result != NULL);

    size_t ini_len = strlen(ini_config);
    size_t header_len = strlen(header);

    IM_ASSERT(header_len > 0);

    if (ini_len == 0)
        return false;

    const char* section_start = strstr(ini_config, header);
    if (section_start == NULL)
        return false;

    const char* section_end = strstr(section_start + header_len, "\n[");
    if (section_end == NULL)
        section_end = section_start + ini_len;

    // "\n[" matches next header start on all platforms, but it cuts new line marker in half on windows.
    if (*(section_end - 1) == '\r')
        --section_end;

    size_t section_len = (size_t)(section_end - section_start);
    result->resize((int)section_len + 1);
    ImStrncpy(result->Data, section_start, section_len);

    return true;
}

//-----------------------------------------------------------------------------
// Time Helpers
//-----------------------------------------------------------------------------
// - ImTimeGetInMicroseconds()
// - ImTimestampToISO8601()
//-----------------------------------------------------------------------------

uint64_t ImTimeGetInMicroseconds()
{
    // Trying std::chrono out of unfettered optimism that it may actually work..
    using namespace std;
    chrono::microseconds ms = chrono::duration_cast<chrono::microseconds>(chrono::high_resolution_clock::now().time_since_epoch());
    return (uint64_t)ms.count();
}

void ImTimestampToISO8601(uint64_t timestamp, Str* out_date)
{
    time_t unix_time = (time_t)(timestamp / 1000000); // Convert to seconds.
    tm* time = gmtime(&unix_time);
    const char* time_format = "%Y-%m-%dT%H:%M:%S";
    size_t size_req = strftime(out_date->c_str(), out_date->capacity(), time_format, time);
    if (size_req >= (size_t)out_date->capacity())
    {
        out_date->reserve((int)size_req);
        strftime(out_date->c_str(), out_date->capacity(), time_format, time);
    }
}

//-----------------------------------------------------------------------------
// Threading Helpers
//-----------------------------------------------------------------------------
// - ImThreadSleepInMilliseconds()
// - ImThreadSetCurrentThreadDescription()
//-----------------------------------------------------------------------------

void ImThreadSleepInMilliseconds(int ms)
{
    using namespace std;
    this_thread::sleep_for(chrono::milliseconds(ms));
}

#if defined(_MSC_VER)
// Helper function for setting thread name on Win32
// This is a separate function because __try cannot coexist with local objects that need destructors called on stack unwind
static void ImThreadSetCurrentThreadDescriptionWin32OldStyle(const char* description)
{
    // Old-style Win32 thread name setting method
    // See https://docs.microsoft.com/en-us/visualstudio/debugger/how-to-set-a-thread-name-in-native-code
    const DWORD MS_VC_EXCEPTION = 0x406D1388;
#pragma pack(push,8)
    typedef struct tagTHREADNAME_INFO
    {
        DWORD dwType; // Must be 0x1000.
        LPCSTR szName; // Pointer to name (in user addr space).
        DWORD dwThreadID; // Thread ID (-1=caller thread).
        DWORD dwFlags; // Reserved for future use, must be zero.
    } THREADNAME_INFO;
#pragma pack(pop)

    THREADNAME_INFO info;
    info.dwType = 0x1000;
    info.szName = description;
    info.dwThreadID = (DWORD)-1;
    info.dwFlags = 0;
#pragma warning(push)
#pragma warning(disable: 6320 6322)
    __try
    {
        RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
#pragma warning(pop)
}
#endif // #ifdef _WIN32

// Set the description (name) of the current thread for debugging purposes
void ImThreadSetCurrentThreadDescription(const char* description)
{
#if defined(_MSC_VER) // Windows + Visual Studio
    // New-style thread name setting
    // Only supported from Win 10 version 1607/Server 2016 onwards, hence the need for dynamic linking

    typedef HRESULT(WINAPI* SetThreadDescriptionFunc)(HANDLE hThread, PCWSTR lpThreadDescription);

    SetThreadDescriptionFunc set_thread_description = (SetThreadDescriptionFunc)::GetProcAddress(GetModuleHandleA("Kernel32.dll"), "SetThreadDescription");
    if (set_thread_description)
    {
        ImVector<ImWchar> buf;
        const int description_wsize = ImTextCountCharsFromUtf8(description, NULL) + 1;
        buf.resize(description_wsize);
        ImTextStrFromUtf8(&buf[0], description_wsize, description, NULL);
        set_thread_description(::GetCurrentThread(), (wchar_t*)&buf[0]);
    }

    // Also do the old-style method too even if the new-style one worked, as the two work in slightly different sets of circumstances
    ImThreadSetCurrentThreadDescriptionWin32OldStyle(description);
#elif defined(__linux) || defined(__linux__) || defined(__MINGW32__) // Linux or MingW
    pthread_setname_np(pthread_self(), description);
#elif defined(__MACH__) || defined(__MSL__) // OSX
    pthread_setname_np(description);
#else
    // This is a nice-to-have rather than critical functionality, so fail silently if we don't support this platform
#endif
}

//-----------------------------------------------------------------------------
// Build info helpers
//-----------------------------------------------------------------------------
// - ImBuildGetCompilationInfo()
// - ImBuildGetGitBranchName()
//-----------------------------------------------------------------------------

// Turn __DATE__ "Jan 10 2019" into "2019-01-10"
static void ImBuildParseDateFromCompilerIntoYMD(const char* in_date, char* out_buf, size_t out_buf_size)
{
    char month_str[5];
    int year, month, day;
    sscanf(in_date, "%3s %d %d", month_str, &day, &year);
    const char month_names[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char* p = strstr(month_names, month_str);
    month = p ? (int)(1 + (p - month_names) / 3) : 0;
    ImFormatString(out_buf, out_buf_size, "%04d-%02d-%02d", year, month, day);
}

// Those strings are used to output easily identifiable markers in compare logs. We only need to support what we use for testing.
// We can probably grab info in eaplatform.h/eacompiler.h etc. in EASTL
const ImBuildInfo* ImBuildGetCompilationInfo()
{
    static ImBuildInfo build_info;

    if (build_info.Type[0] == '\0')
    {
        // Build Type
#if defined(DEBUG) || defined(_DEBUG)
        build_info.Type = "Debug";
#else
        build_info.Type = "Release";
#endif

        // CPU
#if defined(_M_X86) || defined(_M_IX86) || defined(__i386) || defined(__i386__) || defined(_X86_) || defined(_M_AMD64) || defined(_AMD64_) || defined(__x86_64__)
        build_info.Cpu = (sizeof(size_t) == 4) ? "X86" : "X64";
#elif defined(__aarch64__)
        build_info.Cpu = "ARM64";
#else
#error
        build_info.Cpu = (sizeof(size_t) == 4) ? "Unknown32" : "Unknown64";
#endif

        // Platform/OS
#if defined(_WIN32)
        build_info.OS = "Windows";
#elif defined(__linux) || defined(__linux__)
        build_info.OS = "Linux";
#elif defined(__MACH__) || defined(__MSL__)
        build_info.OS = "OSX";
#elif defined(__ORBIS__)
        build_info.OS = "PS4";
#elif defined(_DURANGO)
        build_info.OS = "XboxOne";
#else
        build_info.OS = "Unknown";
#endif

        // Compiler
#if defined(_MSC_VER)
        build_info.Compiler = "MSVC";
#elif defined(__clang__)
        build_info.Compiler = "Clang";
#elif defined(__GNUC__)
        build_info.Compiler = "GCC";
#else
        build_info.Compiler = "Unknown";
#endif

        // Date/Time
        ImBuildParseDateFromCompilerIntoYMD(__DATE__, build_info.Date, IM_ARRAYSIZE(build_info.Date));
        build_info.Time = __TIME__;
    }

    return &build_info;
}

bool ImBuildFindGitBranchName(const char* git_repo_path, Str* branch_name)
{
    IM_ASSERT(git_repo_path != NULL);
    IM_ASSERT(branch_name != NULL);
    Str256f head_path("%s/.git/HEAD", git_repo_path);
    size_t head_size = 0;
    bool result = false;
    if (char* git_head = (char*)ImFileLoadToMemory(head_path.c_str(), "r", &head_size, 1))
    {
        const char prefix[] = "ref: refs/heads/";       // Branch name is prefixed with this in HEAD file.
        const int prefix_length = IM_ARRAYSIZE(prefix) - 1;
        strtok(git_head, "\r\n");                       // Trim new line
        if (head_size > prefix_length && strncmp(git_head, prefix, prefix_length) == 0)
        {
            strcpy(branch_name->c_str(), git_head + prefix_length);
            result = true;
        }
        IM_FREE(git_head);
    }
    return result;
}

//-----------------------------------------------------------------------------
// Operating System Helpers
//-----------------------------------------------------------------------------
// - ImOsCreateProcess()
// - ImOsPOpen()
// - ImOsPClose()
// - ImOsOpenInShell()
// - ImOsConsoleSetTextColor()
// - ImOsIsDebuggerPresent()
//-----------------------------------------------------------------------------

bool    ImOsCreateProcess(const char* cmd_line)
{
#ifdef _WIN32
    STARTUPINFOA siStartInfo;
    PROCESS_INFORMATION piProcInfo;
    ZeroMemory(&siStartInfo, sizeof(STARTUPINFOA));
    char* cmd_line_copy = ImStrdup(cmd_line);
    BOOL ret = ::CreateProcessA(NULL, cmd_line_copy, NULL, NULL, FALSE, 0, NULL, NULL, &siStartInfo, &piProcInfo);
    free(cmd_line_copy);
    ::CloseHandle(siStartInfo.hStdInput);
    ::CloseHandle(siStartInfo.hStdOutput);
    ::CloseHandle(siStartInfo.hStdError);
    ::CloseHandle(piProcInfo.hProcess);
    ::CloseHandle(piProcInfo.hThread);
    return ret != 0;
#else
    IM_UNUSED(cmd_line);
    return false;
#endif
}

FILE*       ImOsPOpen(const char* cmd_line, const char* mode)
{
    IM_ASSERT(cmd_line != NULL && *cmd_line);
    IM_ASSERT(mode != NULL && *mode);
#if _WIN32
    ImVector<wchar_t> w_cmd_line;
    ImVector<wchar_t> w_mode;
    ImUtf8ToWideChar(cmd_line, &w_cmd_line);
    ImUtf8ToWideChar(mode, &w_mode);
    w_mode.resize(w_mode.Size + 1);
    wcscat(w_mode.Data, L"b");   // Windows requires 'b' mode while unixes do not support it and default to binary.
    return _wpopen(w_cmd_line.Data, w_mode.Data);
#else
    return popen(cmd_line, mode);
#endif
}

void        ImOsPClose(FILE* fp)
{
    IM_ASSERT(fp != NULL);
#if _WIN32
    _pclose(fp);
#else
    pclose(fp);
#endif
}

void    ImOsOpenInShell(const char* path)
{
    Str256 command(path);
#ifdef _WIN32
    ImPathFixSeparatorsForCurrentOS(command.c_str());
    ::ShellExecuteA(NULL, "open", command.c_str(), NULL, NULL, SW_SHOWDEFAULT);
#else
#if __APPLE__
    const char* open_executable = "open";
#else
    const char* open_executable = "xdg-open";
#endif
    command.setf("%s \"%s\"", open_executable, path);
    ImPathFixSeparatorsForCurrentOS(command.c_str());
    system(command.c_str());
#endif
}

void    ImOsConsoleSetTextColor(ImOsConsoleStream stream, ImOsConsoleTextColor color)
{
#ifdef _WIN32
    HANDLE hConsole = 0;
    switch (stream)
    {
    case ImOsConsoleStream_StandardOutput: hConsole = ::GetStdHandle(STD_OUTPUT_HANDLE); break;
    case ImOsConsoleStream_StandardError:  hConsole = ::GetStdHandle(STD_ERROR_HANDLE);  break;
    }
    WORD wAttributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    switch (color)
    {
    case ImOsConsoleTextColor_Black:        wAttributes = 0x00; break;
    case ImOsConsoleTextColor_White:        wAttributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE; break;
    case ImOsConsoleTextColor_BrightWhite:  wAttributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY; break;
    case ImOsConsoleTextColor_BrightRed:    wAttributes = FOREGROUND_RED | FOREGROUND_INTENSITY; break;
    case ImOsConsoleTextColor_BrightGreen:  wAttributes = FOREGROUND_GREEN | FOREGROUND_INTENSITY; break;
    case ImOsConsoleTextColor_BrightBlue:   wAttributes = FOREGROUND_BLUE | FOREGROUND_INTENSITY; break;
    case ImOsConsoleTextColor_BrightYellow: wAttributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY; break;
    default: IM_ASSERT(0);
    }
    ::SetConsoleTextAttribute(hConsole, wAttributes);
#elif defined(__linux) || defined(__linux__) || defined(__MACH__) || defined(__MSL__)
    // FIXME: check system capabilities (with environment variable TERM)
    FILE* handle = 0;
    switch (stream)
    {
    case ImOsConsoleStream_StandardOutput: handle = stdout; break;
    case ImOsConsoleStream_StandardError:  handle = stderr; break;
    }

    const char* modifier = "";
    switch (color)
    {
    case ImOsConsoleTextColor_Black:        modifier = "\033[30m";   break;
    case ImOsConsoleTextColor_White:        modifier = "\033[0m";    break;
    case ImOsConsoleTextColor_BrightWhite:  modifier = "\033[1;37m"; break;
    case ImOsConsoleTextColor_BrightRed:    modifier = "\033[1;31m"; break;
    case ImOsConsoleTextColor_BrightGreen:  modifier = "\033[1;32m"; break;
    case ImOsConsoleTextColor_BrightBlue:   modifier = "\033[1;34m"; break;
    case ImOsConsoleTextColor_BrightYellow: modifier = "\033[1;33m"; break;
    default: IM_ASSERT(0);
    }

    fprintf(handle, "%s", modifier);
#endif
}

bool    ImOsIsDebuggerPresent()
{
#ifdef _WIN32
    return ::IsDebuggerPresent() != 0;
#elif defined(__linux__)
    int debugger_pid = 0;
    char buf[2048];                                 // TracerPid is located near the start of the file. If end of the buffer gets cut off thats fine.
    FILE* fp = fopen("/proc/self/status", "rb");    // Can not use ImFileLoadToMemory because size detection of /proc/self/status would fail.
    if (fp == NULL)
        return false;
    fread(buf, 1, IM_ARRAYSIZE(buf), fp);
    fclose(fp);
    buf[IM_ARRAYSIZE(buf) - 1] = 0;
    if (char* tracer_pid = strstr(buf, "TracerPid:"))
    {
        tracer_pid += 10;   // Skip label
        while (isspace(*tracer_pid))
            tracer_pid++;
        debugger_pid = atoi(tracer_pid);
    }
    return debugger_pid != 0;
#else
    // FIXME
    return false;
#endif
}

void    ImOsOutputDebugString(const char* message)
{
#ifdef _WIN32
    OutputDebugStringA(message);
#else
    IM_UNUSED(message);
#endif
}

//-----------------------------------------------------------------------------
// Str.h + InputText bindings
//-----------------------------------------------------------------------------

struct InputTextCallbackStr_UserData
{
    Str*                    StrObj;
    ImGuiInputTextCallback  ChainCallback;
    void*                   ChainCallbackUserData;
};

static int InputTextCallbackStr(ImGuiInputTextCallbackData* data)
{
    InputTextCallbackStr_UserData* user_data = (InputTextCallbackStr_UserData*)data->UserData;
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
    {
        // Resize string callback
        // If for some reason we refuse the new length (BufTextLen) and/or capacity (BufSize) we need to set them back to what we want.
        Str* str = user_data->StrObj;
        IM_ASSERT(data->Buf == str->c_str());
        str->reserve(data->BufTextLen + 1);
        data->Buf = (char*)str->c_str();
    }
    else if (user_data->ChainCallback)
    {
        // Forward to user callback, if any
        data->UserData = user_data->ChainCallbackUserData;
        return user_data->ChainCallback(data);
    }
    return 0;
}

// Draw an extra colored frame over the previous item
// Similar to DebugDrawItemRect() but use Max(1.0f, FrameBorderSize)
void ImGui::ItemErrorFrame(ImU32 col)
{
    ImGuiContext& g = *GetCurrentContext();
    ImDrawList* drawlist = GetWindowDrawList();
    ImGuiStyle& style = GetStyle();
    // FIXME: GetItemRectMin() / GetItemRectMax() will include label. NavRect is not probably defined :(
    drawlist->AddRect(g.LastItemData.NavRect.Min, g.LastItemData.NavRect.Max, GetColorU32(col), style.FrameRounding, ImDrawFlags_None, ImMax(1.0f, style.FrameBorderSize));
}

bool ImGui::InputText(const char* label, Str* str, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* user_data)
{
    IM_ASSERT((flags & ImGuiInputTextFlags_CallbackResize) == 0);
    flags |= ImGuiInputTextFlags_CallbackResize;

    InputTextCallbackStr_UserData cb_user_data;
    cb_user_data.StrObj = str;
    cb_user_data.ChainCallback = callback;
    cb_user_data.ChainCallbackUserData = user_data;
    return InputText(label, (char*)str->c_str(), (size_t)str->capacity() + 1, flags, InputTextCallbackStr, &cb_user_data);
}

bool ImGui::InputTextWithHint(const char* label, const char* hint, Str* str, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* user_data)
{
    IM_ASSERT((flags & ImGuiInputTextFlags_CallbackResize) == 0);
    flags |= ImGuiInputTextFlags_CallbackResize;

    InputTextCallbackStr_UserData cb_user_data;
    cb_user_data.StrObj = str;
    cb_user_data.ChainCallback = callback;
    cb_user_data.ChainCallbackUserData = user_data;
    return InputTextWithHint(label, hint, (char*)str->c_str(), (size_t)str->capacity() + 1, flags, InputTextCallbackStr, &cb_user_data);
}

bool ImGui::InputTextMultiline(const char* label, Str* str, const ImVec2& size, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* user_data)
{
    IM_ASSERT((flags & ImGuiInputTextFlags_CallbackResize) == 0);
    flags |= ImGuiInputTextFlags_CallbackResize;

    InputTextCallbackStr_UserData cb_user_data;
    cb_user_data.StrObj = str;
    cb_user_data.ChainCallback = callback;
    cb_user_data.ChainCallbackUserData = user_data;
    return InputTextMultiline(label, (char*)str->c_str(), (size_t)str->capacity() + 1, size, flags, InputTextCallbackStr, &cb_user_data);
}

// anchor parameter indicates which split would retain it's constant size.
// anchor = 0 - both splits resize when parent container size changes. Both value_1 and value_2 should be persistent.
// anchor = -1 - top/left split would have a constant size. bottom/right split would resize when parent container size changes. value_1 should be persistent, value_2 will always be recalculated from value_1.
// anchor = +1 - bottom/right split would have a constant size. top/left split would resize when parent container size changes. value_2 should be persistent, value_1 will always be recalculated from value_2.
bool ImGui::Splitter(const char* id, float* value_1, float* value_2, int axis, int anchor, float min_size_0, float min_size_1)
{
    // FIXME-DOGFOODING: This needs further refining.
    // FIXME-SCROLL: When resizing either we'd like to keep scroll focus on something (e.g. last clicked item for list, bottom for log)
    // See https://github.com/ocornut/imgui/issues/319
    ImGuiContext& g = *GImGui;
    ImGuiStyle& style = g.Style;
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (min_size_0 < 0)
        min_size_0 = ImGui::GetFrameHeight();
    if (min_size_1)
        min_size_1 = ImGui::GetFrameHeight();

    IM_ASSERT(axis == ImGuiAxis_X || axis == ImGuiAxis_Y);

    float& v_1 = *value_1;
    float& v_2 = *value_2;
    ImRect splitter_bb;
    const float avail = axis == ImGuiAxis_X ? ImGui::GetContentRegionAvail().x - style.ItemSpacing.x : ImGui::GetContentRegionAvail().y - style.ItemSpacing.y;
    if (anchor < 0)
    {
        v_2 = ImMax(avail - v_1, min_size_1);   // First split is constant size.
    }
    else if (anchor > 0)
    {
        v_1 = ImMax(avail - v_2, min_size_0);   // Second split is constant size.
    }
    else
    {
        float r = v_1 / (v_1 + v_2);            // Both splits maintain same relative size to parent.
        v_1 = IM_ROUND(avail * r) - 1;
        v_2 = IM_ROUND(avail * (1.0f - r)) - 1;
    }
    if (axis == ImGuiAxis_X)
    {
        float x = window->DC.CursorPos.x + v_1 + IM_ROUND(style.ItemSpacing.x * 0.5f);
        splitter_bb = ImRect(x - 1, window->WorkRect.Min.y, x + 1, window->WorkRect.Max.y);
    }
    else if (axis == ImGuiAxis_Y)
    {
        float y = window->DC.CursorPos.y + v_1 + IM_ROUND(style.ItemSpacing.y * 0.5f);
        splitter_bb = ImRect(window->WorkRect.Min.x, y - 1, window->WorkRect.Max.x, y + 1);
    }
    return ImGui::SplitterBehavior(splitter_bb, ImGui::GetID(id), (ImGuiAxis)axis, &v_1, &v_2, min_size_0, min_size_1, 3.0f);
}

// FIXME-TESTS: Should eventually remove.
ImFont* ImGui::FindFontByPrefix(const char* prefix)
{
    ImGuiContext& g = *GImGui;
    for (ImFont* font : g.IO.Fonts->Fonts)
        if (strncmp(font->ConfigData->Name, prefix, strlen(prefix)) == 0)
            return font;
    return NULL;
}

// Legacy version support
#if IMGUI_VERSION_NUM < 18924
const char* ImGui::TabBarGetTabName(ImGuiTabBar* tab_bar, ImGuiTabItem* tab)
{
    return tab_bar->GetTabName(tab);
}
#endif

#if IMGUI_VERSION_NUM < 18927
ImGuiID ImGui::TableGetInstanceID(ImGuiTable* table, int instance_no)
{
    // Changed in #6140
    return table->ID + instance_no;
}
#endif

ImGuiID TableGetHeaderID(ImGuiTable* table, const char* column, int instance_no)
{
    IM_ASSERT(table != NULL);
    int column_n = -1;
    for (int n = 0; n < table->Columns.size() && column_n < 0; n++)
        if (strcmp(ImGui::TableGetColumnName(table, n), column) == 0)
            column_n = n;
    IM_ASSERT(column_n != -1);
    return TableGetHeaderID(table, column_n, instance_no);
}

ImGuiID TableGetHeaderID(ImGuiTable* table, int column_n, int instance_no)
{
    IM_ASSERT(column_n >= 0 && column_n < table->ColumnsCount);
    const ImGuiID table_instance_id = ImGui::TableGetInstanceID(table, instance_no);
    const char* column_name = ImGui::TableGetColumnName(table, column_n);
#if IMGUI_VERSION_NUM >= 18927
    const int column_id_differencier = column_n;
#else
    const int column_id_differencier = instance_no * table->ColumnsCount + column_n;
#endif
    const int column_id = ImHashData(&column_id_differencier, sizeof(column_id_differencier), table_instance_id);
    return ImHashData(column_name, strlen(column_name), column_id);
}

// FIXME: Could be moved to core as an internal function?
void TableDiscardInstanceAndSettings(ImGuiID table_id)
{
    ImGuiContext& g = *GImGui;
    IM_ASSERT(g.CurrentTable == NULL);
    if (ImGuiTableSettings* settings = ImGui::TableSettingsFindByID(table_id))
        settings->ID = 0;

    if (ImGuiTable* table = ImGui::TableFindByID(table_id))
        ImGui::TableRemove(table);
    // FIXME-TABLE: We should be able to use TableResetSettings() instead of TableRemove()! Maybe less of a clean slate but would be good to check that it does the job
    //ImGui::TableResetSettings(table);
}

// Helper to verify ImDrawData integrity of buffer count (broke before e.g. #6716)
void DrawDataVerifyMatchingBufferCount(ImDrawData* draw_data)
{
    int total_vtx_count = 0;
    int total_idx_count = 0;
    for (ImDrawList* draw_list : draw_data->CmdLists)
    {
        total_vtx_count += draw_list->VtxBuffer.Size;
        total_idx_count += draw_list->IdxBuffer.Size;
    }
    IM_UNUSED(total_vtx_count);
    IM_UNUSED(total_idx_count);
    IM_ASSERT(total_vtx_count == draw_data->TotalVtxCount);
    IM_ASSERT(total_idx_count == draw_data->TotalIdxCount);
}

//-----------------------------------------------------------------------------
// Simple CSV parser
//-----------------------------------------------------------------------------

void ImGuiCsvParser::Clear()
{
    Rows = Columns = 0;
    if (_Data != NULL)
        IM_FREE(_Data);
    _Data = NULL;
    _Index.clear();
}

bool ImGuiCsvParser::Load(const char* filename)
{
    size_t len = 0;
    _Data = (char*)ImFileLoadToMemory(filename, "rb", &len, 1);
    if (_Data == NULL)
        return false;

    int columns = 1;
    if (Columns > 0)
    {
        columns = Columns;                                          // User-provided expected column count.
    }
    else
    {
        for (const char* c = _Data; *c != '\n' && *c != '\0'; c++)  // Count columns. Quoted columns with commas are not supported.
            if (*c == ',')
                columns++;
    }

    // Count rows. Extra new lines anywhere in the file are ignored.
    int max_rows = 0;
    for (const char* c = _Data, *end = c + len; c < end; c++)
        if ((*c == '\n' && c[1] != '\r' && c[1] != '\n') || *c == '\0')
            max_rows++;

    if (columns == 0 || max_rows == 0)
        return false;

    // Create index
    _Index.resize(columns * max_rows);

    int col = 0;
    char* col_data = _Data;
    for (char* c = _Data; *c != '\0'; c++)
    {
        const bool is_comma = (*c == ',');
        const bool is_eol = (*c == '\n' || *c == '\r');
        const bool is_eof = (*c == '\0');
        if (is_comma || is_eol || is_eof)
        {
            _Index[Rows * columns + col] = col_data;
            col_data = c + 1;
            if (is_comma)
            {
                col++;
            }
            else
            {
                if (col + 1 == columns)
                    Rows++;
                else
                    fprintf(stderr, "%s: Unexpected number of columns on line %d, ignoring.\n", filename, Rows + 1); // FIXME
                col = 0;
            }
            *c = 0;
            if (is_eol)
                while (c[1] == '\r' || c[1] == '\n')
                    c++;
        }
    }

    Columns = columns;
    return true;
}

//-----------------------------------------------------------------------------
