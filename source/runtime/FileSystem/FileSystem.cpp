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

//= INCLUDES ================
#include "pch.h"
SP_WARNINGS_OFF
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_misc.h>
#include <SDL3/SDL_process.h>
SP_WARNINGS_ON
//===========================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        const vector<string> supported_formats_image
        {
            ".jpg",
            ".png",
            ".bmp",
            ".tga",
            ".dds",
            ".exr",
            ".raw",
            ".gif",
            ".hdr",
            ".ico",
            ".iff",
            ".jng",
            ".jpeg",
            ".koala",
            ".kodak",
            ".mng",
            ".pcx",
            ".pbm",
            ".pgm",
            ".ppm",
            ".pfm",
            ".pict",
            ".psd",
            ".raw",
            ".sgi",
            ".targa",
            ".tiff",
            ".tif", // tiff can also be tif
            ".wbmp",
            ".webp",
            ".xbm",
            ".xpm"
        };

        const vector<string> supported_formats_audio
        {
            ".aiff",
            ".asf",
            ".asx",
            ".dls",
            ".flac",
            ".fsb",
            ".it",
            ".m3u",
            ".midi",
            ".mod",
            ".mp2",
            ".mp3",
            ".ogg",
            ".pls",
            ".s3m",
            ".vag", // PS2/PSP
            ".wav",
            ".wax",
            ".wma",
            ".xm",
            ".xma" // XBOX 360
        };

        const vector<string> supported_formats_model
        {
            ".3ds",
            ".obj",
            ".fbx",
            ".blend",
            ".dae",
            ".gltf",
            ".lwo",
            ".c4d",
            ".ase",
            ".dxf",
            ".hmp",
            ".md2",
            ".md3",
            ".md5",
            ".mdc",
            ".mdl",
            ".nff",
            ".ply",
            ".stl",
            ".x",
            ".smd",
            ".lxo",
            ".lws",
            ".ter",
            ".ac3d",
            ".ms3d",
            ".cob",
            ".q3bsp",
            ".xgl",
            ".csm",
            ".bvh",
            ".b3d",
            ".ndo"
        };

        const vector<string> supported_formats_shader
        {
            ".hlsl"
        };

        const vector<string> supported_formats_font
        {
            ".ttf",
            ".ttc",
            ".cff",
            ".woff",
            ".otf",
            ".otc",
            ".pfa",
            ".pfb",
            ".fnt",
            ".bdf",
            ".pfr"
        };

        // create a silent process (no visible console window) without waiting
        // caller is responsible for calling SDL_WaitProcess and SDL_DestroyProcess
        // note: using STDIO_APP for stdout/stderr helps ensure no console window appears
        SDL_Process* create_silent_process(const vector<string>& args)
        {
            vector<const char*> c_args;
            for (const auto& arg : args)
                c_args.push_back(arg.c_str());
            c_args.push_back(nullptr);

            SDL_PropertiesID props = SDL_CreateProperties();
            SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, const_cast<char**>(c_args.data()));
            SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
            SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
            SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_APP);
            SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);

            SDL_Process* process = SDL_CreateProcessWithProperties(props);
            SDL_DestroyProperties(props);
            return process;
        }

        // run a process silently (no visible console window) and wait for completion
        // note: always using STDIO_APP ensures no console window appears on any platform
        void run_silent_process(const vector<string>& args, string* output = nullptr)
        {
            vector<const char*> c_args;
            for (const auto& arg : args)
                c_args.push_back(arg.c_str());
            c_args.push_back(nullptr);

            SDL_PropertiesID props = SDL_CreateProperties();
            SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, const_cast<char**>(c_args.data()));
            SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
            SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
            SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_APP);
            SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);

            SDL_Process* process = SDL_CreateProcessWithProperties(props);
            SDL_DestroyProperties(props);

            if (process)
            {
                // read and wait - this drains stdout/stderr and waits for completion
                size_t data_size = 0;
                int exit_code = 0;
                char* data = static_cast<char*>(SDL_ReadProcess(process, &data_size, &exit_code));
                if (output && data && data_size > 0)
                {
                    *output = string(data, data_size);
                }
                if (data)
                    SDL_free(data);
                    
                SDL_DestroyProcess(process);
            }
        }
    }

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

    bool FileSystem::IsDirectoryEmpty(const string& path)
    {
        try
        {
            if (!filesystem::exists(path))
                return false; // directory doesn't exist

            if (!filesystem::is_directory(path))
                return false; // path exists but is not a directory

            // check if the directory is empty
            return filesystem::directory_iterator(path) == filesystem::directory_iterator();
        }
        catch (const filesystem::filesystem_error& e)
        {
            SP_LOG_WARNING("%s, %s", e.what(), path.c_str());
            return false;
        }
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

    vector<string> FileSystem::GetDirectoriesInDirectory(const string& path)
    {
        vector<string> directories;
        const filesystem::directory_iterator it_end; // default construction yields past-the-end
        for (filesystem::directory_iterator it(path); it != it_end; ++it)
        {
            if (!filesystem::is_directory(it->status()))
                continue;

            string path_it;

            // A system_error is possible if the characters are
            // something that can't be converted, like Russian.
            try
            {
                path_it = it->path().string();
            }
            catch (system_error& e)
            {
                SP_LOG_WARNING("Failed to read a directory path. %s", e.what());
            }

            if (!path_it.empty())
            {
                // finally, save
                directories.emplace_back(path_it);
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

    vector<string> FileSystem::SplitPath(const string& path)
    {
        vector<string> components;
        stringstream ss(path);
        string item;

        // Split by both forward and backward slashes
        while (getline(ss, item, '/'))
        {
            if (!item.empty())
            {
                stringstream item_ss(item);
                string sub_item;
                while (getline(item_ss, sub_item, '\\'))
                {
                    if (!sub_item.empty())
                    {
                        components.push_back(sub_item);
                    }
                }
            }
        }

        // Handle root directory (e.g., "C:" on Windows)
        if (path.size() > 1 && path[1] == ':')
        {
            components.insert(components.begin(), path.substr(0, 2));
        }

        return components;
    }

    string FileSystem::GetLastWriteTime(const string& path)
    {
        try
        {
            namespace fs = filesystem;
            auto last_write_time = fs::last_write_time(path);
            auto time_point = chrono::time_point_cast<chrono::system_clock::duration>(last_write_time - fs::file_time_type::clock::now() + chrono::system_clock::now());
            time_t time = chrono::system_clock::to_time_t(time_point);
            std::tm tm_local{};
            localtime_s(&tm_local, &time);

            stringstream ss;
            ss << put_time(&tm_local, "%Y-%m-%d %H:%M:%S");
            return ss.str();
        }
        catch (const filesystem::filesystem_error& e)
        {
            SP_LOG_ERROR("Failed to get last write time for %s: %s", path.c_str(), e.what());
            return "Unknown";
        }
    }

    void FileSystem::Rename(const std::string& old_name, const std::string& new_name)
    {
        try
        {
            filesystem::rename(old_name, new_name);
        }
        catch (const filesystem::filesystem_error& e)
        {
            SP_LOG_ERROR("Failed to rename %s to %s: %s", old_name.c_str(), new_name.c_str(), e.what());
        }
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

    bool FileSystem::IsEngineAudioFile(const string& path)
    {
        return GetExtensionFromFilePath(path) == EXTENSION_AUDIO;
    }

    bool FileSystem::IsEngineShaderFile(const string& path)
    {
        return GetExtensionFromFilePath(path) == EXTENSION_SHADER;
    }

    bool FileSystem::IsEngineTextureFile(const string& path)
    {
        return GetExtensionFromFilePath(path) == EXTENSION_TEXTURE;
    }

    bool FileSystem::IsEngineWorldFile(const string& path)
    {
        return GetExtensionFromFilePath(path) == EXTENSION_WORLD;
    }

    bool FileSystem::IsEngineFile(const string& path)
    {
        return
                IsEnginePrefabFile(path)   ||
                IsEngineMeshFile(path)     ||
                IsEngineMaterialFile(path) ||
                IsEngineMeshFile(path)     ||
                IsEngineSceneFile(path)    ||
                IsEngineAudioFile(path)    ||
                IsEngineShaderFile(path)   ||
                IsEngineTextureFile(path)  ||
                IsEngineWorldFile(path);
    }

    const vector<string>& FileSystem::GetSupportedImageFormats()
    {
        return supported_formats_image;
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

    void FileSystem::Command(const string& command, function<void()> callback, bool blocking)
    {
        auto execute_command = [command, callback]()
        {
            // detect which shell to use at runtime
            const char* comspec = SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "COMSPEC");
            if (comspec)
            {
                // windows: COMSPEC points to cmd.exe
                run_silent_process({comspec, "/c", command});
            }
            else
            {
                // unix: use sh
                run_silent_process({"sh", "-c", command});
            }
            
            if (callback)
            {
                callback();
            }
        };

        if (blocking)
        {
            execute_command();
        }
        else
        {
            thread(execute_command).detach();
        }
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

    bool FileSystem::CreateDirectory_(const string& path)
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
            CreateDirectory_(GetDirectoryFromFilePath(destination));
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

    bool FileSystem::DownloadFile(const string& url, const string& destination, function<void(float)> progress_callback)
    {
        namespace fs = filesystem;
        error_code ec;

        // ensure destination directory exists
        string dest_dir = GetDirectoryFromFilePath(destination);
        if (!dest_dir.empty() && !Exists(dest_dir))
        {
            CreateDirectory_(dest_dir);
        }

        // check if partial download exists (for resume support)
        size_t existing_size = 0;
        if (fs::exists(destination, ec))
        {
            existing_size = fs::file_size(destination, ec);
            if (existing_size > 0)
            {
                SP_LOG_INFO("Resuming download from %zu bytes", existing_size);
            }
        }

        SP_LOG_INFO("Downloading: %s", destination.c_str());

        // first, get file size with a head request
        string size_file = destination + ".size";
        run_silent_process({"curl", "-sI", "-L", "-o", size_file, url});

        // parse content-length from headers
        size_t expected_size = 0;
        if (fs::exists(size_file, ec))
        {
            ifstream header_file(size_file);
            if (header_file.is_open())
            {
                string line;
                while (getline(header_file, line))
                {
                    string lower_line = line;
                    transform(lower_line.begin(), lower_line.end(), lower_line.begin(), ::tolower);
                    if (lower_line.find("content-length:") != string::npos)
                    {
                        size_t pos = line.find(':');
                        if (pos != string::npos)
                        {
                            string size_str = line.substr(pos + 1);
                            size_str.erase(0, size_str.find_first_not_of(" \t\r\n"));
                            size_str.erase(size_str.find_last_not_of(" \t\r\n") + 1);
                            try { expected_size = stoull(size_str); } catch (...) {}
                        }
                    }
                }
                header_file.close();
            }
            fs::remove(size_file, ec);
        }

        // start download process with resume support for unreliable connections
        // -C - : auto-resume from where it left off
        // --retry 10 : retry up to 10 times
        // --retry-delay 3 : wait 3 seconds between retries
        // --retry-all-errors : retry on all errors, not just transient ones
        SDL_Process* process = create_silent_process({
            "curl", "-L", "-s", "-f",
            "-C", "-",
            "--retry", "10",
            "--retry-delay", "3",
            "--retry-all-errors",
            "-o", destination, url
        });

        if (!process)
        {
            SP_LOG_ERROR("Failed to start download: %s", SDL_GetError());
            return false;
        }

        // get stdout stream to drain any output (prevents buffer blocking)
        SDL_IOStream* stdout_stream = SDL_GetProcessOutput(process);

        // poll file size for progress while process runs
        while (!SDL_WaitProcess(process, false, nullptr))
        {
            // drain any stdout data to prevent pipe buffer from filling
            if (stdout_stream)
            {
                char drain_buffer[1024];
                while (SDL_ReadIO(stdout_stream, drain_buffer, sizeof(drain_buffer)) > 0) {}
            }

            if (progress_callback && expected_size > 0 && fs::exists(destination, ec))
            {
                size_t current_size = fs::file_size(destination, ec);
                if (!ec)
                {
                    float progress = static_cast<float>(current_size) / static_cast<float>(expected_size);
                    progress_callback(progress > 1.0f ? 1.0f : progress);
                }
            }
            this_thread::sleep_for(chrono::milliseconds(100));
        }
        SDL_DestroyProcess(process);

        // verify download succeeded by checking file size matches expected (if known)
        size_t final_size = fs::exists(destination, ec) ? fs::file_size(destination, ec) : 0;
        bool success = final_size > 0 && (expected_size == 0 || final_size >= expected_size);
        
        if (success)
        {
            if (progress_callback)
                progress_callback(1.0f);
            SP_LOG_INFO("Downloaded: %s (%zu bytes)", destination.c_str(), final_size);
            return true;
        }
        else
        {
            if (fs::exists(destination, ec))
                fs::remove(destination, ec);
            SP_LOG_ERROR("Failed to download: %s", url.c_str());
            return false;
        }
    }

    bool FileSystem::ExtractArchive(const string& archive_path, const string& destination_path)
    {
        // find 7z executable - check all possible locations and names at runtime
        string seven_zip_exe;
        vector<string> candidates = {"7z.exe", "build_scripts/7z.exe", "7z", "7za"};
        
        for (const auto& candidate : candidates)
        {
            if (Exists(candidate) || IsExecutableInPath(candidate))
            {
                seven_zip_exe = candidate;
                break;
            }
        }
        
        if (seven_zip_exe.empty())
        {
            SP_LOG_ERROR("7z not found. Please ensure it exists in the current directory, build_scripts/, or PATH.");
            return false;
        }

        // ensure destination exists
        if (!Exists(destination_path))
        {
            CreateDirectory_(destination_path);
        }

        SP_LOG_INFO("Extracting: %s", archive_path.c_str());
        
        // run 7z silently
        run_silent_process({
            seven_zip_exe, "x", archive_path,
            "-o" + destination_path, "-aoa", "-bso0", "-bsp0"
        });
        
        // verify extraction by checking destination has content
        int result = IsDirectoryEmpty(destination_path) ? 1 : 0;

        if (result != 0)
        {
            SP_LOG_ERROR("Failed to extract archive: %s (exit code: %d)", archive_path.c_str(), result);
            return false;
        }

        SP_LOG_INFO("Extracted to: %s", destination_path.c_str());
        return true;
    }

    string FileSystem::ComputeFileSha256(const string& path)
    {
        namespace fs = filesystem;
        error_code ec;

        if (!fs::exists(path, ec))
            return "";

        // detect which hash utility is available at runtime
        bool use_certutil = IsExecutableInPath("certutil");
        vector<string> args;
        if (use_certutil)
            args = {"certutil", "-hashfile", path, "SHA256"};
        else
            args = {"sha256sum", path};

        // run process and capture output
        string result;
        run_silent_process(args, &result);

        if (result.empty())
            return "";

        // parse hash from output - handle both certutil and sha256sum formats
        string hash;
        istringstream stream(result);
        string line;
        int line_num = 0;
        
        while (getline(stream, line))
        {
            line_num++;
            
            // certutil outputs hash on the second line
            if (use_certutil && line_num == 2)
            {
                // remove spaces and carriage returns
                line.erase(remove(line.begin(), line.end(), ' '), line.end());
                line.erase(remove(line.begin(), line.end(), '\r'), line.end());
                hash = line;
                break;
            }
            // sha256sum outputs: <hash>  <filename>
            else if (!use_certutil)
            {
                size_t space = line.find(' ');
                if (space != string::npos)
                    hash = line.substr(0, space);
                else
                    hash = line;
                break;
            }
        }

        // convert to lowercase for consistency
        transform(hash.begin(), hash.end(), hash.begin(), ::tolower);
        
        return hash;
    }

    bool FileSystem::IsExecutableInPath(const string& executable)
    {
        // get PATH using sdl's cross-platform environment api
        const char* path_env = SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "PATH");
        if (!path_env)
            return false;
        
        string path_str = path_env;
    
        // detect delimiter and suffix based on COMSPEC presence (runtime detection)
        const char* comspec = SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "COMSPEC");
        char delimiter      = comspec ? ';' : ':';
        string exe_suffix   = comspec ? ".exe" : "";
    
        // split PATH and search for executable
        vector<string> paths;
        size_t start = 0;
        size_t end;
        while ((end = path_str.find(delimiter, start)) != string::npos)
        {
            paths.emplace_back(path_str.substr(start, end - start));
            start = end + 1;
        }
        paths.emplace_back(path_str.substr(start));
    
        for (const auto& dir : paths)
        {
            filesystem::path exe_path = filesystem::path(dir) / (executable + exe_suffix);
            error_code ec;
            if (filesystem::exists(exe_path, ec) && filesystem::is_regular_file(exe_path, ec))
                return true;
        }
    
        return false;
    }
}
