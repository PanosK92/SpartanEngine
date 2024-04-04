/*
Copyright(c) 2016-2024 Panos Karabelas

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

#include "Editor.h"
#include <vector>
#include <string>

#ifdef _MSC_VER // windows
#include <Windows.h>
#include <shellapi.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // convert command line to argv-like format
    int argc;
    LPWSTR* argv_w = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::string> args;
    args.reserve(argc);
    for (int i = 0; i < argc; ++i)
    {
        // convert wide characters to normal string
        char arg[1024];
        WideCharToMultiByte(CP_UTF8, 0, argv_w[i], -1, arg, sizeof(arg), nullptr, nullptr);
        args.push_back(std::string(arg));
    }
    LocalFree(argv_w);
#else // linux
int main(int argc, char** argv)
{
    std::vector<std::string> args(argv, argv + argc);
#endif
    Editor editor = Editor(args);
    editor.Tick();
    return 0;
}
