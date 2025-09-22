/*
Copyright(c) 2015-2025 Panos Karabelas

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

//= INCLUDES =============
#include "pch.h"
#include "httplib.h"
SP_WARNINGS_OFF
#include <SDL3/SDL_misc.h> // required for SDL_OpenURLWithApp
SP_WARNINGS_ON
//========================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        static const vector<string> supported_formats_image
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

        static const vector<string> supported_formats_audio
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

        static const vector<string> supported_formats_model
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

        static const vector<string> supported_formats_shader
        {
            ".hlsl"
        };

        static const vector<string> supported_formats_font
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

        string compute_sha256(const string& input)
        {
        #ifdef _WIN32
            #pragma comment(lib, "bcrypt.lib")

            BCRYPT_ALG_HANDLE hAlg = NULL;
            BCRYPT_HASH_HANDLE hHash = NULL;
            DWORD hashObjSize = 0, hashSize = 0;
            PBYTE hashObject = NULL, hashBuffer = NULL;
        
            // open an algorithm handle
            if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0)))
            {
                // get the size of the hash object and the hash
                if (BCRYPT_SUCCESS(BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PBYTE)&hashObjSize, sizeof(DWORD), NULL, 0)) && 
                    BCRYPT_SUCCESS(BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PBYTE)&hashSize, sizeof(DWORD), NULL, 0)))
                {
                    // allocate memory for hash object and hash buffer
                    hashObject = (PBYTE)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, hashObjSize);
                    hashBuffer = (PBYTE)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, hashSize);
        
                    if (hashObject && hashBuffer)
                    {
                        // create a hash
                        if (BCRYPT_SUCCESS(BCryptCreateHash(hAlg, &hHash, hashObject, hashObjSize, NULL, 0, 0)))
                        {
                            // hash the data
                            if (BCRYPT_SUCCESS(BCryptHashData(hHash, (PBYTE)input.c_str(), (ULONG)input.length(), 0)))
                            {
                                // finish the hash and get the result
                                if (BCRYPT_SUCCESS(BCryptFinishHash(hHash, hashBuffer, hashSize, 0)))
                                {
                                    stringstream ss;
                                    for (DWORD i = 0; i < hashSize; i++)
                                    {
                                        ss << hex << setw(2) << setfill('0') << (int)hashBuffer[i];
                                    }

                                    // clean up
                                    BCryptDestroyHash(hHash);
                                    BCryptCloseAlgorithmProvider(hAlg, 0);
                                    HeapFree(GetProcessHeap(), 0, hashObject);
                                    HeapFree(GetProcessHeap(), 0, hashBuffer);
                                    return ss.str();
                                }
                            }
                            BCryptDestroyHash(hHash);
                        }
                    }
                }
                BCryptCloseAlgorithmProvider(hAlg, 0);
            }

            // free resources in case of early exit
            if (hashObject) HeapFree(GetProcessHeap(), 0, hashObject);
            if (hashBuffer) HeapFree(GetProcessHeap(), 0, hashBuffer);
        #else
            SP_LOG_ERROR("SHA256 is not implemented for this platform.");
        #endif
            return "";
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
            namespace fs         = filesystem;
            auto last_write_time = fs::last_write_time(path);
            auto time_point      = chrono::time_point_cast<chrono::system_clock::duration>(last_write_time - fs::file_time_type::clock::now() + chrono::system_clock::now());
            time_t time          = chrono::system_clock::to_time_t(time_point);

            stringstream ss;
            ss << put_time(localtime(&time), "%Y-%m-%d %H:%M:%S");
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
            (void) system(command.c_str());
            
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
        httplib::Client cli(url.substr(0, url.find("/", 8))); // extract base URL
        bool success = false;
    
        if (fs::exists(destination))
        {
            // read the existing file content
            ifstream file(destination, ios::binary);
            if (!file)
            {
                SP_LOG_ERROR("Failed to open existing file for reading.");
            }
            else
            {
                string file_content((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
                string existing_hash = compute_sha256(file_content);
    
                // fetch the remote file to compute its hash
                auto res = cli.Get(url.substr(url.find("/", 8)));
                if (!res || res->status != 200)
                {
                    SP_LOG_ERROR("Failed to fetch remote file for hash comparison.");
                }
                else
                {
                    string remote_content = res->body;
                    string new_hash = compute_sha256(remote_content);
    
                    // hash mismatch, download new file
                    if (existing_hash != new_hash)
                    {
                        ofstream new_file(destination, ios::binary);
                        if (!new_file)
                        {
                            SP_LOG_ERROR("Failed to open file for writing new content.");
                        }
                        else
                        {
                            new_file.write(remote_content.data(), remote_content.length());
                            success = true;
                        }
                    }
                    else
                    {
                        success = true; // no need to download, hashes match
                    }
                }
            }
        }
        else
        {
            // file doesn't exist, download it
            auto res = cli.Get(url.substr(url.find("/", 8)));
            if (!res || res->status != 200)
            {
                SP_LOG_ERROR("Failed to download file.");
            }
            else
            {
                size_t total_size = stoll(res->get_header_value("Content-Length", "0"));
                ofstream file(destination, ios::binary);
                if (!file)
                {
                    SP_LOG_ERROR("Failed to open file for writing new download.");
                }
                else
                {
                    size_t downloaded_size   = 0;
                    const size_t buffer_size = 1024 * 1024; // 1MB buffer
                    char buffer[buffer_size];
                    
                    while (!res->body.empty())
                    {
                        size_t read_size = min(buffer_size, res->body.size());
                        memcpy(buffer, res->body.data(), read_size);
                        file.write(buffer, read_size);
                        res->body.erase(0, read_size);
                        
                        downloaded_size += read_size;
                        float progress = (total_size > 0) ? (float)downloaded_size / total_size : 0.0f;
                        progress_callback(progress);
                    }
                    success = true;
                }
            }
        }
    
        progress_callback(1.0f);
        return success;
    }

   bool FileSystem::IsExecutableInPath(const string& executable)
    {
        string path_str;
    
    #ifdef _WIN32
        char* buffer = nullptr;
        size_t size = 0;
        if (_dupenv_s(&buffer, &size, "PATH") != 0 || buffer == nullptr)
            return false;
        path_str = buffer;
        free(buffer);
    #else
        const char* path_env = getenv("PATH");
        if (!path_env)
            return false;
        path_str = path_env;
    #endif
    
        vector<string> paths;
    #ifdef _WIN32
        char delimiter = ';';
        string exe_suffix = ".exe";
    #else
        char delimiter = ':';
        string exe_suffix = "";
    #endif
    
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
