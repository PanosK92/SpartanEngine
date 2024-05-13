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

//= INCLUDES ============
#include "pch.h"
#include <SDL/SDL_misc.h>
//=======================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    bool FileSystem::IsEmptyOrWhitespace(const string& var)
    {
        // Check if it's empty
        if (var.empty())
            return true;

        // Check if it's made out of whitespace characters
        for (char _char : var)
        {
            if (!isspace(_char))
                return false;
        }

        return true;
    }

    bool FileSystem::IsAlphanumeric(const string& var)
    {
        if (IsEmptyOrWhitespace(var))
            return false;

        for (char _char : var)
        {
            if (!isalnum(_char))
                return false;
        }

        return true;
    }

    string FileSystem::RemoveIllegalCharacters(const string& text)
    {
        string text_legal = text;

        // Remove characters which are illegal for both names and paths
        string illegal = ":?\"<>|";
        for (auto it = text_legal.begin(); it < text_legal.end(); ++it)
        {
            if (illegal.find(*it) != string::npos)
            {
                *it = '_';
            }
        }

        // If this is a valid path, return it (otherwise it's a name)
        if (IsDirectory(text_legal))
            return text_legal;

        // Remove slashes which are illegal characters for names
        illegal = "\\/";
        for (auto it = text_legal.begin(); it < text_legal.end(); ++it)
        {
            if (illegal.find(*it) != string::npos)
            {
                *it = '_';
            }
        }

        return text_legal;
    }

    string FileSystem::GetStringBeforeExpression(const string& str, const string& exp)
    {
        // ("The quick brown fox", "brown") -> "The quick "
        const size_t position = str.find(exp);
        return position != string::npos ? str.substr(0, position) : "";
    }

    string FileSystem::GetStringAfterExpression(const string& str, const string& exp)
    {
        // ("The quick brown fox", "brown") -> "fox"
        const size_t position = str.find(exp);
        return position != string::npos ? str.substr(position + exp.length()) : "";
    }

    string FileSystem::GetStringBetweenExpressions(const string& str, const string& exp_a, const string& exp_b)
    {
        // ("The quick brown fox", "The ", " brown") -> "quick"

        const regex base_regex(exp_a + "(.*)" + exp_b);

        smatch base_match;
        if (regex_search(str, base_match, base_regex))
        {
            // The first sub_match is the whole string; the next
            // sub_match is the first parenthesized expression.
            if (base_match.size() == 2)
            {
                return base_match[1].str();
            }
        }

        return str;
    }

    string FileSystem::ConvertToUppercase(const string& lower)
    {
        const locale loc;
        string upper;
        for (const auto& character : lower)
        {
            upper += toupper(character, loc);
        }

        return upper;
    }

    string FileSystem::ReplaceExpression(const string& str, const string& from, const string& to)
    {
        return regex_replace(str, regex(from), to);
    }

    wstring FileSystem::StringToWstring(const string& str)
    {
        SP_WARNINGS_OFF
        wstring_convert<codecvt_utf8_utf16<wchar_t>> converter;
        return converter.from_bytes(str);
        SP_WARNINGS_ON
    }

    bool FileSystem::Exists(const string& path)
    {
        try
        {
            if (filesystem::exists(path))
                return true;
        }
        catch (filesystem::filesystem_error& e)
        {
            SP_LOG_WARNING("%s, %s", e.what(), path.c_str());
        }

        return false;
    }

    bool FileSystem::IsDirectory(const string& path)
    {
        try
        {
            if (filesystem::exists(path) && filesystem::is_directory(path))
                return true;
        }
        catch (filesystem::filesystem_error& e)
        {
            SP_LOG_WARNING("%s, %s", e.what(), path.c_str());
        }

        return false;
    }

    bool FileSystem::IsFile(const string& path)
    {
        if (path.empty())
            return false;

        try
        {
            if (filesystem::exists(path) && filesystem::is_regular_file(path))
                return true;
        }
        catch (filesystem::filesystem_error& e)
        {
            SP_LOG_WARNING("%s, %s", e.what(), path.c_str());
        }

        return false;
    }

    string FileSystem::GetFileNameFromFilePath(const string& path)
    {
        return filesystem::path(path).filename().generic_string();
    }

    string FileSystem::GetFileNameWithoutExtensionFromFilePath(const string& path)
    {
        const auto file_name    = GetFileNameFromFilePath(path);
        const size_t last_index = file_name.find_last_of('.');

        if (last_index != string::npos)
            return file_name.substr(0, last_index);

        return "";
    }

    string FileSystem::GetDirectoryFromFilePath(const string& path)
    {
        const size_t last_index = path.find_last_of("\\/");

        if (last_index != string::npos)
            return path.substr(0, last_index + 1);

        return "";
    }

    string FileSystem::GetFilePathWithoutExtension(const string& path)
    {
        return GetDirectoryFromFilePath(path) + GetFileNameWithoutExtensionFromFilePath(path);
    }

    string FileSystem::ReplaceExtension(const string& path, const string& extension)
    {
        return GetDirectoryFromFilePath(path) + GetFileNameWithoutExtensionFromFilePath(path) + extension;
    }

    string FileSystem::GetExtensionFromFilePath(const string& path)
    {
        string extension;

        // A system_error is possible if the characters are
        // something that can't be converted, like Russian.
        try
        {
            extension = filesystem::path(path).extension().generic_string();
        }
        catch (system_error & e)
        {
            SP_LOG_WARNING("Failed. %s", e.what());
            
        }

        return extension;
    }

    string FileSystem::NativizeFilePath(const string& path)
    {
        const string file_path_no_ext = GetFilePathWithoutExtension(path);

        if (IsSupportedAudioFile(path))  return file_path_no_ext + EXTENSION_AUDIO;
        if (IsSupportedImageFile(path))  return file_path_no_ext + EXTENSION_TEXTURE;
        if (IsSupportedModelFile(path))  return file_path_no_ext + EXTENSION_MODEL;
        if (IsSupportedFontFile(path))   return file_path_no_ext + EXTENSION_FONT;
        if (IsSupportedShaderFile(path)) return file_path_no_ext + EXTENSION_SHADER;

        SP_LOG_WARNING("Failed to nativize file path");
        return path;
    }

    vector<string> FileSystem::GetDirectoriesInDirectory(const string& path)
    {
        vector<string> directories;
        const filesystem::directory_iterator it_end; // default construction yields past-the-end
        for (filesystem::directory_iterator it(path); it != it_end; ++it)
        {
            if (!filesystem::is_directory(it->status()))
                continue;

            string path;

            // A system_error is possible if the characters are
            // something that can't be converted, like Russian.
            try
            {
                path = it->path().string();
            }
            catch (system_error& e)
            {
                SP_LOG_WARNING("Failed to read a directory path. %s", e.what());
            }

            if (!path.empty())
            {
                // finally, save
                directories.emplace_back(path);
            }
        }


        return directories;
    }

    vector<string> FileSystem::GetFilesInDirectory(const string& path)
    {
        vector<string> file_paths;
        const filesystem::directory_iterator it_end; // default construction yields past-the-end
        for (filesystem::directory_iterator it(path); it != it_end; ++it)
        {
            if (!filesystem::is_regular_file(it->status()))
                continue;

            try
            {
                // a crash is possible if the characters are
                // something that can't be converted, like Russian.
                file_paths.emplace_back(it->path().string());
            }
            catch (system_error& e)
            {
                SP_LOG_WARNING("Failed to read a file path. %s", e.what());
            }
        }

        return file_paths;
    }

    bool FileSystem::IsSupportedAudioFile(const string& path)
    {
        const string extension = GetExtensionFromFilePath(path);

        for (const auto& format : supported_formats_audio)
        {
            if (extension == format || extension == ConvertToUppercase(format))
                return true;
        }

        return false;
    }

    bool FileSystem::IsSupportedImageFile(const string& path)
    {
        const string extension = GetExtensionFromFilePath(path);

        for (const auto& format : supported_formats_image)
        {
            if (extension == format || extension == ConvertToUppercase(format))
                return true;
        }
        
        if (GetExtensionFromFilePath(path) == EXTENSION_TEXTURE)
            return true;

        return false;
    }

    bool FileSystem::IsSupportedModelFile(const string& path)
    {
        const string extension = GetExtensionFromFilePath(path);

        for (const auto& format : supported_formats_model)
        {
            if (extension == format || extension == ConvertToUppercase(format))
                return true;
        }

        return false;
    }

    bool FileSystem::IsSupportedShaderFile(const string& path)
    {
        const string extension = GetExtensionFromFilePath(path);

        for (const auto& format : supported_formats_shader)
        {
            if (extension == format || extension == ConvertToUppercase(format))
                return true;
        }

        return false;
    }

    bool FileSystem::IsSupportedFontFile(const string& path)
    {
        const string extension = GetExtensionFromFilePath(path);

        for (const auto& format : supported_formats_font)
        {
            if (extension == format || extension == ConvertToUppercase(format))
                return true;
        }

        return false;
    }

    bool FileSystem::IsEnginePrefabFile(const string& path)
    {
        return GetExtensionFromFilePath(path) == EXTENSION_PREFAB;
    }

    bool FileSystem::IsEngineModelFile(const string& path)
    {
        return GetExtensionFromFilePath(path) == EXTENSION_MODEL;
    }

    bool FileSystem::IsEngineMaterialFile(const string& path)
    {
        return GetExtensionFromFilePath(path) == EXTENSION_MATERIAL;
    }

    bool FileSystem::IsEngineMeshFile(const string& path)
    {
        return GetExtensionFromFilePath(path) == EXTENSION_MESH;
    }

    bool FileSystem::IsEngineSceneFile(const string& path)
    {
        return GetExtensionFromFilePath(path) == EXTENSION_WORLD;
    }

    bool FileSystem::IsEngineTextureFile(const string& path)
    {
        return GetExtensionFromFilePath(path) == EXTENSION_TEXTURE;
    }

    bool FileSystem::IsEngineAudioFile(const string& path)
    {
        return GetExtensionFromFilePath(path) == EXTENSION_AUDIO;
    }

    bool FileSystem::IsEngineShaderFile(const string& path)
    {
        return GetExtensionFromFilePath(path) == EXTENSION_SHADER;
    }

    bool FileSystem::IsEngineFile(const string& path)
    {
        return
                IsEnginePrefabFile(path)   ||
                IsEngineModelFile(path)    ||
                IsEngineMaterialFile(path) ||
                IsEngineMeshFile(path)     ||
                IsEngineSceneFile(path)    ||
                IsEngineTextureFile(path)  ||
                IsEngineAudioFile(path)    ||
                IsEngineShaderFile(path);
    }

    vector<string> FileSystem::GetSupportedFilesInDirectory(const string& path)
    {
        const vector<string> filesInDirectory = GetFilesInDirectory(path);
        vector<string> imagesInDirectory      = GetSupportedImageFilesFromPaths(filesInDirectory);  // get all the images
        vector<string> modelsInDirectory      = GetSupportedModelFilesFromPaths(filesInDirectory);  // get all the models
        vector<string> supportedFiles;

        // get supported images
        for (const auto& imageInDirectory : imagesInDirectory)
        {
            supportedFiles.emplace_back(imageInDirectory);
        }

        // get supported models
        for (const auto& modelInDirectory : modelsInDirectory)
        {
            supportedFiles.emplace_back(modelInDirectory);
        }

        return supportedFiles;
    }

    vector<string> FileSystem::GetSupportedImageFilesFromPaths(const vector<string>& paths)
    {
        vector<string> files;
        for (const auto& path : paths)
        {
            if (!IsSupportedImageFile(path))
                continue;

            files.emplace_back(path);
        }

        return files;
    }

    vector<string> FileSystem::GetSupportedAudioFilesFromPaths(const vector<string>& paths)
    {
        vector<string> files;
        for (const auto& path : paths)
        {
            if (!IsSupportedAudioFile(path))
                continue;

            files.emplace_back(path);
        }

        return files;
    }

    vector<string> FileSystem::GetSupportedModelFilesFromPaths(const vector<string>& paths)
    {
        vector<string> files;
        for (const auto& path : paths)
        {
            if (!IsSupportedModelFile(path))
                continue;

            files.emplace_back(path);
        }

        return files;
    }

    vector<string> FileSystem::GetSupportedModelFilesInDirectory(const string& path)
    {
        return GetSupportedModelFilesFromPaths(GetFilesInDirectory(path));
    }

    vector<string> FileSystem::GetSupportedSceneFilesInDirectory(const string& path)
    {
        vector<string> sceneFiles;

        auto files = GetFilesInDirectory(path);
        for (const auto& file : files)
        {
            if (!IsEngineSceneFile(file))
                continue;

            sceneFiles.emplace_back(file);
        }

        return sceneFiles;
    }

    string FileSystem::GetRelativePath(const string& path)
    {
        if (filesystem::path(path).is_relative())
            return path;

        // create absolute paths
        const filesystem::path p = filesystem::absolute(path);
        const filesystem::path r = filesystem::absolute(GetWorkingDirectory());

        // if root paths are different, return absolute path
        if( p.root_path() != r.root_path())
            return p.generic_string();

        // initialize relative path
        filesystem::path result;

        // find out where the two paths diverge
        filesystem::path::const_iterator itr_path = p.begin();
        filesystem::path::const_iterator itr_relative_to = r.begin();
        while( *itr_path == *itr_relative_to && itr_path != p.end() && itr_relative_to != r.end() ) 
        {
            ++itr_path;
            ++itr_relative_to;
        }

        // add "../" for each remaining token in relative_to
        if( itr_relative_to != r.end() ) 
        {
            ++itr_relative_to;
            while( itr_relative_to != r.end() ) 
            {
                result /= "..";
                ++itr_relative_to;
            }
        }

        // add remaining path
        while( itr_path != p.end() ) 
        {
            result /= *itr_path;
            ++itr_path;
        }

        return result.generic_string();
    }

    string FileSystem::GetWorkingDirectory()
    {
        return filesystem::current_path().generic_string();
    }

    string FileSystem::GetParentDirectory(const string& path)
    {
        auto parent_path = filesystem::path(path).parent_path();

        // If there is not parent path, return path as is
        if (parent_path.empty())
            return path;

        return parent_path.generic_string();
    }

    string FileSystem::GetRootDirectory(const string& path)
    {
        return filesystem::path(path).root_directory().generic_string();
    }

    void FileSystem::OpenUrl(const string& url)
    {
        SDL_OpenURL(url.c_str());
    }

    bool FileSystem::Delete(const string& path)
    {
        try
        {
            if (filesystem::exists(path) && filesystem::remove_all(path))
                return true;
        }
        catch (filesystem::filesystem_error& e)
        {
            SP_LOG_ERROR("s, %s", e.what(), path.c_str());
        }

        return false;
    }

    bool FileSystem::CreateDirectory(const string& path)
    {
        try
        {
            if (filesystem::create_directories(path))
                return true;
        }
        catch (filesystem::filesystem_error& e)
        {
            SP_LOG_ERROR("%s, %s", e.what(), path.c_str());
        }

        return false;
    }

    bool FileSystem::CopyFileFromTo(const string& source, const string& destination)
    {
        if (source == destination)
            return true;

        // In case the destination path doesn't exist, create it
        if (!Exists(GetDirectoryFromFilePath(destination)))
        {
            CreateDirectory(GetDirectoryFromFilePath(destination));
        }

        try
        {
            return filesystem::copy_file(source, destination, filesystem::copy_options::overwrite_existing);
        }
        catch (filesystem::filesystem_error& e)
        {
            SP_LOG_ERROR("%s", e.what());
            return true;
        }
    }
}
