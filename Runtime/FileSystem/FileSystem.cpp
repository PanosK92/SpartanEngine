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
#include "../Logging/Log.h"
#include <Windows.h>
#include <shellapi.h>
//=========================

//= NAMESPACES =================
using namespace std;
using namespace std::filesystem;
//==============================

namespace Directus
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
		bool result;
		try
		{
			result = create_directories(path);
		}
		catch (filesystem_error& e)
		{
			LOGF_ERROR("FileSystem::CreateDirectory: %s, %s", e.what(), path.c_str());
		}

		return result;
	}

	bool FileSystem::DeleteDirectory(const string& directory)
	{
		bool result;
		try
		{
			result = remove_all(directory);
		}
		catch (filesystem_error& e)
		{
			LOGF_ERROR("FileSystem::DeleteDirectory: %s, %s", e.what(), directory.c_str());
		}

		return result;
	}

	bool FileSystem::DirectoryExists(const string& directory)
	{
		bool result;
		try
		{
			result = exists(directory);
		}
		catch (filesystem_error& e)
		{
			LOGF_ERROR("FileSystem::DirectoryExists: %s, %s", e.what(), directory.c_str());
		}

		return result;
	}

	bool FileSystem::IsDirectory(const string& directory)
	{
		bool result;
		try
		{
			result = is_directory(directory);
		}
		catch (filesystem_error& e)
		{
			LOGF_ERROR("FileSystem::IsDirectory: %s, %s", e.what(), directory.c_str());
		}

		return result;
	}

	void FileSystem::OpenDirectoryWindow(const std::string& directory)
	{
		ShellExecute(nullptr, nullptr, StringToWString(directory).c_str(), nullptr, nullptr, SW_SHOW);
	}

	bool FileSystem::FileExists(const string& filePath)
	{
		bool result;
		try
		{
			result = exists(filePath);
		}
		catch (filesystem_error& e)
		{
			LOGF_ERROR("FileSystem::FileExists: %s, %s", e.what(), filePath.c_str());
		}

		return result;
	}

	bool FileSystem::DeleteFile_(const string& filePath)
	{
		// If this is a directory path, return
		if (is_directory(filePath))
			return false;

		bool result = false;
		try
		{
			result = remove(filePath.c_str()) == 0;
		}
		catch (filesystem_error& e)
		{
			LOGF_ERROR("FileSystem::DeleteFile: %s, %s", e.what(), filePath.c_str());
		}

		return result;
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

		bool result = false;
		try 
		{
			result = copy_file(source, destination, copy_options::overwrite_existing);
		}
		catch (filesystem_error& e) 
		{
			LOG_ERROR("FileSystem: Could not copy \"" + source + "\". " + string(e.what()));
		}

		return result;
	}

	string FileSystem::GetFileNameFromFilePath(const string& path)
	{
		auto lastindex	= path.find_last_of("\\/");
		auto fileName	= path.substr(lastindex + 1, path.length());

		return fileName;
	}

	string FileSystem::GetFileNameNoExtensionFromFilePath(const string& filepath)
	{
		auto fileName		= GetFileNameFromFilePath(filepath);
		auto lastindex		= fileName.find_last_of('.');
		auto fileNameNoExt	= fileName.substr(0, lastindex);

		return fileNameNoExt;
	}

	string FileSystem::GetDirectoryFromFilePath(const string& filePath)
	{
		auto lastindex = filePath.find_last_of("\\/");
		auto directory = filePath.substr(0, lastindex + 1);

		return directory;
	}

	string FileSystem::GetFilePathWithoutExtension(const string& filePath)
	{
		auto directory		= GetDirectoryFromFilePath(filePath);
		auto fileNameNoExt	= GetFileNameNoExtensionFromFilePath(filePath);

		return directory + fileNameNoExt;
	}

	string FileSystem::GetExtensionFromFilePath(const string& filePath)
	{
		if (filePath.empty() || filePath == NOT_ASSIGNED)
			return NOT_ASSIGNED;

		auto lastindex = filePath.find_last_of('.');
		if (string::npos != lastindex)
		{
			// extension with dot included
			return filePath.substr(lastindex, filePath.length());
		}

		return NOT_ASSIGNED;
	}

	vector<string> FileSystem::GetDirectoriesInDirectory(const string& directory)
	{
		vector<string> subDirs;
		directory_iterator end_itr; // default construction yields past-the-end
		for (directory_iterator itr(directory); itr != end_itr; ++itr)
		{
			if (!is_directory(itr->status()))
				continue;

			subDirs.emplace_back(itr->path().generic_string());
		}

		return subDirs;
	}

	vector<string> FileSystem::GetFilesInDirectory(const string& directory)
	{
		vector<string> filePaths;
		directory_iterator end_itr; // default construction yields past-the-end
		for (directory_iterator itr(directory); itr != end_itr; ++itr)
		{
			if (!is_regular_file(itr->status()))
				continue;

			filePaths.emplace_back(itr->path().generic_string());
		}

		return filePaths;
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

	bool FileSystem::IsEngineShaderFile(const string& filePath)
	{
		return GetExtensionFromFilePath(filePath) == EXTENSION_SHADER;
	}

	bool FileSystem::IsEngineMetadataFile(const string& filePath)
	{
		return GetExtensionFromFilePath(filePath) == METADATA_EXTENSION;
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

	wstring FileSystem::StringToWString(const string& str)
	{
		auto slength	= int(str.length()) + 1;
		auto len		= MultiByteToWideChar(CP_ACP, 0, str.c_str(), slength, nullptr, 0);
		auto buf		= new wchar_t[len];
		MultiByteToWideChar(CP_ACP, 0, str.c_str(), slength, buf, len);
		wstring result(buf);
		delete[] buf;
		return result;
	}
}