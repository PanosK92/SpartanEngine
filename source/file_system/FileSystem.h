/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
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
        static bool IsEngineLuaFile(const std::string& path);
        static bool IsEngineMeshFile(const std::string& path);
        static bool IsEngineSceneFile(const std::string& path);
        static bool IsEngineAudioFile(const std::string& path);
        static bool IsEngineShaderFile(const std::string& path);
        static bool IsEngineTextureFile(const std::string& path);
        static bool IsEngineWorldFile(const std::string& path);
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
        static std::string ToSnakeCase(const std::string& text);
        static std::string GetRelativePath(const std::string& path);
        static std::string GetWorkingDirectory();
        // directory containing the running exe, independent of cwd
        static std::string GetExecutableDirectory();
        static std::string GetRootDirectory(const std::string& path);
        static std::string GetParentDirectory(const std::string& path);
        static std::vector<std::string> GetDirectoriesInDirectory(const std::string& path);
        static std::vector<std::string> GetFilesInDirectory(const std::string& path);
        static std::vector<std::string> SplitPath(const std::string& path);
        static std::string GetLastWriteTime(const std::string& path);
        static void Rename(const std::string& old_name, const std::string& new_name);
        static bool Exists(const std::string& path);
        static bool IsDirectoryEmpty(const std::string& path);
        static bool IsDirectory(const std::string& path);
        static bool IsFile(const std::string& path);
        static void OpenUrl(const std::string& url);
        static void Command(const std::string& command, std::function<void()> callback = nullptr, const bool blocking = false);
        static bool Delete(const std::string& path);
        static bool CreateDirectory_(const std::string& path);
        static bool CopyFileFromTo(const std::string& source, const std::string& destination);
        static bool WriteFile(std::string_view path, std::string_view data);
        static bool ReadFile(std::string_view path, std::string& data);

        // internet & archives
        static bool DownloadFile(const std::string& url, const std::string& destination, std::function<void(float)> progress_callback);
        static bool DownloadToString(const std::string& url, std::string& contents);
        static bool ExtractArchive(const std::string& archive_path, const std::string& destination_path);
        static bool CreateArchive(const std::string& archive_path, const std::vector<std::string>& paths_to_include);
        static std::string ComputeFileSha256(const std::string& path);
        static bool IsExecutableInPath(const std::string& executable);
    };

    static const char* EXTENSION_WORLD    = ".world";
    static const char* EXTENSION_MATERIAL = ".xml";
    static const char* EXTENSION_MESH     = ".mesh";
    static const char* EXTENSION_LUA      = ".lua";
    static const char* EXTENSION_PREFAB   = ".prefab";
    static const char* EXTENSION_SHADER   = ".shader";
    static const char* EXTENSION_FONT     = ".font";
    static const char* EXTENSION_AUDIO    = ".audio";
    static const char* EXTENSION_TEXTURE  = ".texture";
}
