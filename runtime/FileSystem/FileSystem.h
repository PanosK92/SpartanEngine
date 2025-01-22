/*
Copyright(c) 2016-2025 Panos Karabelas

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

//= INCLUDES ========
#include <vector>
#include <string>
#include <functional>
//===================

namespace spartan
{
    class FileSystem
    {
    public:
        // strings
        static bool IsEmptyOrWhitespace(const std::string& var);
        static bool IsAlphanumeric(const std::string& var);
        static std::string RemoveIllegalCharacters(const std::string& text);
        static std::string GetStringBeforeExpression(const std::string& str, const std::string& exp);
        static std::string GetStringAfterExpression(const std::string& str, const std::string& exp);
        static std::string GetStringBetweenExpressions(const std::string& str, const std::string& exp_a, const std::string& exp_b);
        static std::string ConvertToUppercase(const std::string& lower);
        static std::string ReplaceExpression(const std::string& str, const std::string& from, const std::string& to);
        static std::wstring StringToWstring(const std::string& str);

        // supported files
        static bool IsSupportedAudioFile(const std::string& path);
        static bool IsSupportedImageFile(const std::string& path);
        static bool IsSupportedModelFile(const std::string& path);
        static bool IsSupportedShaderFile(const std::string& path);
        static bool IsSupportedFontFile(const std::string& path);
        static bool IsEnginePrefabFile(const std::string& path);
        static bool IsEngineMaterialFile(const std::string& path);
        static bool IsEngineMeshFile(const std::string& path);
        static bool IsEngineModelFile(const std::string& path);
        static bool IsEngineSceneFile(const std::string& path);
        static bool IsEngineTextureFile(const std::string& path);
        static bool IsEngineAudioFile(const std::string& path);
        static bool IsEngineShaderFile(const std::string& path);
        static bool IsEngineFile(const std::string& path);
        static const std::vector<std::string>& GetSupportedImageFormats();

        // supported files in directory
        static std::vector<std::string> GetSupportedFilesInDirectory(const std::string& path);
        static std::vector<std::string> GetSupportedImageFilesFromPaths(const std::vector<std::string>& paths);
        static std::vector<std::string> GetSupportedAudioFilesFromPaths(const std::vector<std::string>& paths);
        static std::vector<std::string> GetSupportedModelFilesFromPaths(const std::vector<std::string>& paths);
        static std::vector<std::string> GetSupportedModelFilesInDirectory(const std::string& path);
        static std::vector<std::string> GetSupportedSceneFilesInDirectory(const std::string& path);

        // directories & files
        static std::string GetFileNameFromFilePath(const std::string& path);
        static std::string GetFileNameWithoutExtensionFromFilePath(const std::string& path);
        static std::string GetDirectoryFromFilePath(const std::string& path);
        static std::string GetFilePathWithoutExtension(const std::string& path);
        static std::string ReplaceExtension(const std::string& path, const std::string& extension);
        static std::string GetExtensionFromFilePath(const std::string& path);
        static std::string GetRelativePath(const std::string& path);
        static std::string GetWorkingDirectory();
        static std::string GetRootDirectory(const std::string& path);
        static std::string GetParentDirectory(const std::string& path);
        static std::vector<std::string> GetDirectoriesInDirectory(const std::string& path);
        static std::vector<std::string> GetFilesInDirectory(const std::string& path);
        static bool Exists(const std::string& path);
        static bool IsDirectoryEmpty(const std::string& path);
        static bool IsDirectory(const std::string& path);
        static bool IsFile(const std::string& path);
        static void OpenUrl(const std::string& url);
        static void Command(const std::string& command, std::function<void()> callback = nullptr, const bool blocking = false);
        static bool Delete(const std::string& path);
        static bool CreateDirectory_(const std::string& path);
        static bool CopyFileFromTo(const std::string& source, const std::string& destination);

        // internet
        static bool DownloadFile(const std::string& url, const std::string& destination);
    };

    static const char* EXTENSION_WORLD    = ".world";
    static const char* EXTENSION_MATERIAL = ".xml";
    static const char* EXTENSION_MODEL    = ".model";
    static const char* EXTENSION_PREFAB   = ".prefab";
    static const char* EXTENSION_SHADER   = ".shader";
    static const char* EXTENSION_FONT     = ".font";
    static const char* EXTENSION_TEXTURE  = ".texture";
    static const char* EXTENSION_MESH     = ".mesh";
    static const char* EXTENSION_AUDIO    = ".audio";
}
