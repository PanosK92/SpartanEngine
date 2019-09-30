/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES ==============
#include "FileSystem.h"
#include <filesystem>
#include <regex>
#include <fstream>
#include <sstream> 
#include "../Logging/Log.h"
#include <Windows.h>
#include <shellapi.h>
//=========================

//= NAMESPACES =================
using namespace std;
using namespace std::filesystem;
//==============================

namespace Spartan
{
	vector<string> FileSystem::m_supportedImageFormats;
	vector<string> FileSystem::m_supportedAudioFormats;
	vector<string> FileSystem::m_supportedModelFormats;
	vector<string> FileSystem::m_supportedShaderFormats;
	vector<string> FileSystem::m_supportedScriptFormats;
	vector<string> FileSystem::m_supportedFontFormats;

	void FileSystem::Initialize()
	{
		// Supported image formats
		m_supportedImageFormats =
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

		// Supported audio formats
		m_supportedAudioFormats =
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

		// Supported model formats
		m_supportedModelFormats =
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

		// Supported shader formats
		m_supportedShaderFormats =
		{
			".hlsl"
		};

		// Supported script formats
		m_supportedScriptFormats = 
		{
			".as"
		};

		// Supported font formats
		m_supportedFontFormats =
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
	}

	bool FileSystem::CreateDirectory_(const string& path)
	{
		try
		{
			return create_directories(path);
		}
		catch (filesystem_error& e)
		{
			LOGF_ERROR("%s, %s", e.what(), path.c_str());
			return true;
		}
	}

	bool FileSystem::DeleteDirectory(const string& directory)
	{
		try
		{
			return remove_all(directory);
		}
		catch (filesystem_error& e)
		{
			LOGF_ERROR("s, %s", e.what(), directory.c_str());
			return true;
		}
	}

	bool FileSystem::DirectoryExists(const string& directory)
	{
		try
		{
			return exists(directory);
		}
		catch (filesystem_error& e)
		{
			LOGF_ERROR("%s, %s", e.what(), directory.c_str());
			return true;
		}
	}

	bool FileSystem::IsDirectory(const string& directory)
	{
		try
		{
			return is_directory(directory);
		}
		catch (filesystem_error& e)
		{
			LOGF_ERROR("%s, %s", e.what(), directory.c_str());
			return false;
		}
	}

	void FileSystem::OpenDirectoryWindow(const std::string& directory)
	{
		ShellExecute(nullptr, nullptr, StringToWstring(directory).c_str(), nullptr, nullptr, SW_SHOW);
	}

	bool FileSystem::FileExists(const string& file_path)
	{
		try
		{
			return exists(file_path);
		}
		catch (filesystem_error& e)
		{
			LOGF_ERROR("%s, %s", e.what(), file_path.c_str());
			return true;
		}
	}

	bool FileSystem::DeleteFile_(const string& file_path)
	{
		// If this is a directory path, return
		if (is_directory(file_path))
			return false;

		try
		{
			return remove(file_path.c_str()) == 0;
		}
		catch (filesystem_error& e)
		{
			LOGF_ERROR("%s, %s", e.what(), file_path.c_str());
			return true;
		}
	}

	bool FileSystem::CopyFileFromTo(const string& source, const string& destination)
	{
		if (source == destination)
			return true;

		// In case the destination path doesn't exist, create it
		if (!DirectoryExists(GetDirectoryFromFilePath(destination)))
		{
			CreateDirectory_(GetDirectoryFromFilePath(destination));
		}

		try 
		{
			return copy_file(source, destination, copy_options::overwrite_existing);
		}
		catch (filesystem_error& e) 
		{
			LOGF_ERROR("%s", e.what());
			return true;
		}
	}

	string FileSystem::GetFileNameFromFilePath(const string& path)
	{
		auto last_index	= path.find_last_of("\\/");
		auto fileName	= path.substr(last_index + 1, path.length());

		return fileName;
	}

	string FileSystem::GetFileNameNoExtensionFromFilePath(const string& file_path)
	{
		auto file_name		= GetFileNameFromFilePath(file_path);
		auto last_index		= file_name.find_last_of('.');
		auto fileNameNoExt	= file_name.substr(0, last_index);

		return fileNameNoExt;
	}

    string FileSystem::GetFileFormatFromFilePath(const string& file_path)
    {
        auto last_index     = file_path.find_last_of('.');
        auto fileNameNoExt  = file_path.substr(last_index, file_path.length());

        return fileNameNoExt;
    }

    string FileSystem::GetDirectoryFromFilePath(const string& file_path)
	{
		auto last_index = file_path.find_last_of("\\/");
		auto directory  = file_path.substr(0, last_index + 1);

		return directory;
	}

	string FileSystem::GetFilePathWithoutExtension(const string& file_path)
	{
		auto directory		= GetDirectoryFromFilePath(file_path);
		auto fileNameNoExt	= GetFileNameNoExtensionFromFilePath(file_path);

		return directory + fileNameNoExt;
	}

	string FileSystem::GetExtensionFromFilePath(const string& file_path)
	{
		if (file_path.empty())
			return "";

		auto last_index = file_path.find_last_of('.');
		if (string::npos != last_index)
		{
			// extension with dot included
			return file_path.substr(last_index, file_path.length());
		}

		return "";
	}

    string FileSystem::ReplaceFileExtension(const string& file_path, const string& extension)
    {
        return GetFilePathWithoutExtension(file_path) + extension;
    }

    vector<string> FileSystem::GetDirectoriesInDirectory(const string& directory)
	{
		vector<string> directories;
		directory_iterator it_end; // default construction yields past-the-end
		for (directory_iterator it(directory); it != it_end; ++it)
		{
			if (!is_directory(it->status()))
				continue;

            try
            {
                // a crash is possible if the characters are
                // something that can't be converted, like Russian.
                directories.emplace_back(it->path().string());
            }
            catch (system_error& e)
            {
                LOGF_ERROR("Failed to read a directory path. %s", e.what());
            }
		}

		return directories;
	}

	vector<string> FileSystem::GetFilesInDirectory(const string& directory)
	{
		vector<string> file_paths;
		directory_iterator it_end; // default construction yields past-the-end
		for (directory_iterator it(directory); it != it_end; ++it)
		{
			if (!is_regular_file(it->status()))
				continue;

            try
            {
                // a crash is possible if the characters are
                // something that can't be converted, like Russian.
                file_paths.emplace_back(it->path().string());
            }
            catch (system_error& e)
            {
                LOGF_ERROR("Failed to read a file path. %s", e.what());
            }
		}

		return file_paths;
	}

	vector<string> FileSystem::GetSupportedFilesInDirectory(const string& directory)
	{
		vector<string> filesInDirectory		= GetFilesInDirectory(directory);
		vector<string> imagesInDirectory	= GetSupportedImageFilesFromPaths(filesInDirectory); // get all the images
		vector<string> scriptsInDirectory	= GetSupportedScriptFilesFromPaths(filesInDirectory); // get all the scripts
		vector<string> modelsInDirectory	= GetSupportedModelFilesFromPaths(filesInDirectory); // get all the models
		vector<string> supportedFiles;

		// get supported images
		for (const auto& imageInDirectory : imagesInDirectory)
		{
			supportedFiles.emplace_back(imageInDirectory);
		}

		// get supported scripts
		for (const auto& scriptInDirectory : scriptsInDirectory)
		{
			supportedFiles.emplace_back(scriptInDirectory);
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
		vector<string> imageFiles;
		for (const auto& path : paths)
		{
			if (!IsSupportedImageFile(path))
				continue;

			imageFiles.emplace_back(path);
		}

		return imageFiles;
	}

	vector<string> FileSystem::GetSupportedAudioFilesFromPaths(const vector<string>& paths)
	{
		vector<string> audioFiles;
		for (const auto& path : paths)
		{
			if (!IsSupportedAudioFile(path))
				continue;

			audioFiles.emplace_back(path);
		}

		return audioFiles;
	}

	vector<string> FileSystem::GetSupportedScriptFilesFromPaths(const vector<string>& paths)
	{
		vector<string> scripts;
		for (const auto& path : paths)
		{
			if (!IsEngineScriptFile(path))
				continue;

			scripts.emplace_back(path);
		}

		return scripts;
	}

	vector<string> FileSystem::GetSupportedModelFilesFromPaths(const vector<string>& paths)
	{
		vector<string> images;
		for (const auto& path : paths)
		{
			if (!IsSupportedModelFile(path))
				continue;

			images.emplace_back(path);
		}

		return images;
	}

	vector<string> FileSystem::GetSupportedModelFilesInDirectory(const string& directory)
	{
		return GetSupportedModelFilesFromPaths(GetFilesInDirectory(directory));
	}

	vector<string> FileSystem::GetSupportedSceneFilesInDirectory(const string& directory)
	{
		vector<string> sceneFiles;

		auto files = GetFilesInDirectory(directory);
		for (const auto& file : files)
		{
			if (!IsEngineSceneFile(file))
				continue;

			sceneFiles.emplace_back(file);
		}

		return sceneFiles;
	}

	bool FileSystem::IsSupportedAudioFile(const string& path)
	{
		string fileExt = GetExtensionFromFilePath(path);

		auto supportedFormats = GetSupportedAudioFormats();
		for (const auto& format : supportedFormats)
		{
			if (fileExt == format || fileExt == ConvertToUppercase(format))
				return true;
		}

		return false;
	}

	bool FileSystem::IsSupportedImageFile(const string& path)
	{
		string fileExt = GetExtensionFromFilePath(path);

		auto supportedFormats = GetSupportedImageFormats();
		for (const auto& format : supportedFormats)
		{
			if (fileExt == format || fileExt == ConvertToUppercase(format))
				return true;
		}
		
		if (GetExtensionFromFilePath(path) == EXTENSION_TEXTURE)
			return true;

		return false;
	}

	bool FileSystem::IsSupportedModelFile(const string& path)
	{
		string fileExt = GetExtensionFromFilePath(path);

		auto supportedFormats = GetSupportedModelFormats();
		for (const auto& format : supportedFormats)
		{
			if (fileExt == format || fileExt == ConvertToUppercase(format))
				return true;
		}

		return false;
	}

	bool FileSystem::IsSupportedShaderFile(const string& path)
	{
		string fileExt = GetExtensionFromFilePath(path);

		auto supportedFormats = GetSupportedShaderFormats();
		for (const auto& format : supportedFormats)
		{
			if (fileExt == format || fileExt == ConvertToUppercase(format))
				return true;
		}

		return false;
	}

	bool FileSystem::IsSupportedFontFile(const string& path)
	{
		string fileExt = GetExtensionFromFilePath(path);

		auto supportedFormats = GetSupportedFontFormats();
		for (const auto& format : supportedFormats)
		{
			if (fileExt == format || fileExt == ConvertToUppercase(format))
				return true;
		}

		return false;
	}

	bool FileSystem::IsEngineScriptFile(const string& path)
	{
		string fileExt = GetExtensionFromFilePath(path);

		auto supportedFormats = GetSupportedScriptFormats();
		for (const auto& format : supportedFormats)
		{
			if (fileExt == format || fileExt == ConvertToUppercase(format))
				return true;
		}

		return false;
	}

	bool FileSystem::IsEnginePrefabFile(const string& filePath)
	{
		return GetExtensionFromFilePath(filePath) == EXTENSION_PREFAB;
	}

	bool FileSystem::IsEngineModelFile(const string& filePath)
	{
		return GetExtensionFromFilePath(filePath) == EXTENSION_MODEL;
	}

	bool FileSystem::IsEngineMaterialFile(const string& filePath)
	{
		return GetExtensionFromFilePath(filePath) == EXTENSION_MATERIAL;
	}

	bool FileSystem::IsEngineMeshFile(const string& filePath)
	{
		return GetExtensionFromFilePath(filePath) == EXTENSION_MESH;
	}

	bool FileSystem::IsEngineSceneFile(const string& filePath)
	{
		return GetExtensionFromFilePath(filePath) == EXTENSION_WORLD;
	}

	bool FileSystem::IsEngineTextureFile(const string& filePath)
	{
		return GetExtensionFromFilePath(filePath) == EXTENSION_TEXTURE;
	}

    bool FileSystem::IsEngineAudioFile(const std::string& filePath)
    {
        return GetExtensionFromFilePath(filePath) == EXTENSION_AUDIO;
    }

    bool FileSystem::IsEngineShaderFile(const string& filePath)
	{
		return GetExtensionFromFilePath(filePath) == EXTENSION_SHADER;
	}

    bool FileSystem::IsEngineFile(const string& file_path)
    {
        return  IsEngineScriptFile(file_path)   ||
                IsEnginePrefabFile(file_path)   ||
                IsEngineModelFile(file_path)    ||
                IsEngineMaterialFile(file_path) ||
                IsEngineMeshFile(file_path)     ||
                IsEngineSceneFile(file_path)    ||
                IsEngineTextureFile(file_path)  ||
                IsEngineAudioFile(file_path)    ||
                IsEngineShaderFile(file_path);
    }

    // Returns a file path which is relative to the engine's executable
	string FileSystem::GetRelativeFilePath(const string& absoluteFilePath)
	{		
		// create absolute paths
		path p = absolute(absoluteFilePath);
		path r = absolute(GetWorkingDirectory());

		// if root paths are different, return absolute path
		if( p.root_path() != r.root_path())
		    return p.generic_string();

		// initialize relative path
		path result;

		// find out where the two paths diverge
		path::const_iterator itr_path = p.begin();
		path::const_iterator itr_relative_to = r.begin();
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

	// Returns a file path which is where the engine's executable is located
	string FileSystem::GetWorkingDirectory()
	{
		return current_path().generic_string() + "/";
	}

	string FileSystem::GetParentDirectory(const string& directory)
	{
		auto found	= directory.find_last_of("/\\");
		auto result	= directory;

		// If no slash was found, return provided string
		if (found == string::npos)
			return directory;

		// If the slash was find at the last position, remove it and try again
		if (found == directory.length() - 1)
		{
			result = result.substr(0, found - 1);
			return GetParentDirectory(result);
		}

		// Return parent directory including a slash at the end
		return result.substr(0, found) + "/";
	}

	string FileSystem::GetStringAfterExpression(const string& str, const string& expression)
	{
		// ("The quick brown fox", "brown") -> "brown fox"
		auto position	= str.find(expression);
		auto remaining	= position != string::npos ? str.substr(position + expression.length()) : str;

		return remaining;
	}

	string FileSystem::GetStringBetweenExpressions(const string& str, const string& firstExpression, const string& secondExpression)
	{
		// ("The quick brown fox", "The ", " brown") -> "quick"

		regex base_regex(firstExpression + "(.*)" + secondExpression);

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
		locale loc;
		string upper;
		for (const auto& character : lower)
		{
			upper += std::toupper(character, loc);
		}
	
		return upper;
	}

	string FileSystem::ReplaceExpression(const string& str, const string& from, const string& to)
	{
		return regex_replace(str, regex(from), to);
	}

	wstring FileSystem::StringToWstring(const string& str)
	{
		const auto slength =	 static_cast<int>(str.length()) + 1;
		const auto len		= MultiByteToWideChar(CP_ACP, 0, str.c_str(), slength, nullptr, 0);
		const auto buf		= new wchar_t[len];
		MultiByteToWideChar(CP_ACP, 0, str.c_str(), slength, buf, len);
		std::wstring result(buf);
		delete[] buf;
		return result;
	}

    vector<string> FileSystem::GetIncludedFiles(const std::string& file_path)
    {
        // Read the file
        ifstream in(file_path);
        stringstream buffer;
        buffer << in.rdbuf();

        string source           = buffer.str();
        string directory        = GetDirectoryFromFilePath(file_path);
        string directive_exp    = "#include \"";
        vector<string> file_paths;

		// Early exit if there is no include directive
		if (source.find(directive_exp) == string::npos)
			return file_paths;

		// Scan for include directives
		istringstream stream(source);
        string include_directive;
		while (std::getline(stream, include_directive))
		{
			if (include_directive.find(directive_exp) != string::npos)
            {
                // Construct file path and save it
                string file_name = GetStringBetweenExpressions(include_directive, directive_exp, "\"");
				file_paths.emplace_back(directory + file_name);
            }
		}

		// If any file path contains more file paths inside, start resolving them recursively
        auto file_paths_copy = file_paths; // copy the file paths to avoid modification while iterating
        for (const auto& _file_path : file_paths_copy)
        {
            // Read the file
            ifstream _in(_file_path);
            stringstream _buffer;
            _buffer << _in.rdbuf();

            // Check for include directive
            string source = _buffer.str();
		    if (source.find(directive_exp) != string::npos)
		    {
		    	auto new_includes = GetIncludedFiles(_file_path);
                file_paths.insert(file_paths.end(), new_includes.begin(), new_includes.end());
		    }
        }
	
		// At this point, everything should be resolved
		return file_paths;
	}
}
