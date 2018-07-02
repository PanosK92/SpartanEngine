/*
Copyright(c) 2016-2018 Panos Karabelas

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
#include <locale>
#include <regex>
#include "../Logging/Log.h"
#include <Windows.h>
#include <shellapi.h>
//=========================

//= NAMESPACES ==========================
using namespace std;
using namespace experimental::filesystem;
//=======================================

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
		// Set supported image formats
		{
			m_supportedImageFormats.emplace_back(".jpg");
			m_supportedImageFormats.emplace_back(".png");
			m_supportedImageFormats.emplace_back(".bmp");
			m_supportedImageFormats.emplace_back(".tga");
			m_supportedImageFormats.emplace_back(".dds");
			m_supportedImageFormats.emplace_back(".exr");
			m_supportedImageFormats.emplace_back(".raw");
			m_supportedImageFormats.emplace_back(".gif");
			m_supportedImageFormats.emplace_back(".hdr");
			m_supportedImageFormats.emplace_back(".ico");
			m_supportedImageFormats.emplace_back(".iff");
			m_supportedImageFormats.emplace_back(".jng");
			m_supportedImageFormats.emplace_back(".jpeg");
			m_supportedImageFormats.emplace_back(".koala");
			m_supportedImageFormats.emplace_back(".kodak");
			m_supportedImageFormats.emplace_back(".mng");
			m_supportedImageFormats.emplace_back(".pcx");
			m_supportedImageFormats.emplace_back(".pbm");
			m_supportedImageFormats.emplace_back(".pgm");
			m_supportedImageFormats.emplace_back(".ppm");
			m_supportedImageFormats.emplace_back(".pfm");
			m_supportedImageFormats.emplace_back(".pict");
			m_supportedImageFormats.emplace_back(".psd");
			m_supportedImageFormats.emplace_back(".raw");
			m_supportedImageFormats.emplace_back(".sgi");
			m_supportedImageFormats.emplace_back(".targa");
			m_supportedImageFormats.emplace_back(".tiff");
			m_supportedImageFormats.emplace_back(".tif"); // tiff can also be tif
			m_supportedImageFormats.emplace_back(".wbmp");
			m_supportedImageFormats.emplace_back(".webp");
			m_supportedImageFormats.emplace_back(".xbm");
			m_supportedImageFormats.emplace_back(".xpm");
		}

		// Set supported audio formats
		{
			m_supportedAudioFormats.emplace_back(".aiff");
			m_supportedAudioFormats.emplace_back(".asf");
			m_supportedAudioFormats.emplace_back(".asx");
			m_supportedAudioFormats.emplace_back(".dls");
			m_supportedAudioFormats.emplace_back(".flac");
			m_supportedAudioFormats.emplace_back(".fsb");
			m_supportedAudioFormats.emplace_back(".it");
			m_supportedAudioFormats.emplace_back(".m3u");
			m_supportedAudioFormats.emplace_back(".midi");
			m_supportedAudioFormats.emplace_back(".mod");
			m_supportedAudioFormats.emplace_back(".mp2");
			m_supportedAudioFormats.emplace_back(".mp3");
			m_supportedAudioFormats.emplace_back(".ogg");
			m_supportedAudioFormats.emplace_back(".pls");
			m_supportedAudioFormats.emplace_back(".s3m");
			m_supportedAudioFormats.emplace_back(".vag"); // PS2/PSP
			m_supportedAudioFormats.emplace_back(".wav");
			m_supportedAudioFormats.emplace_back(".wax");
			m_supportedAudioFormats.emplace_back(".wma");
			m_supportedAudioFormats.emplace_back(".xm");
			m_supportedAudioFormats.emplace_back(".xma"); // XBOX 360
		}

		// Set supported model formats
		{
			// Foreign formats
			m_supportedModelFormats.emplace_back(".3ds");
			m_supportedModelFormats.emplace_back(".obj");
			m_supportedModelFormats.emplace_back(".fbx");
			m_supportedModelFormats.emplace_back(".blend");
			m_supportedModelFormats.emplace_back(".dae");
			m_supportedModelFormats.emplace_back(".lwo");
			m_supportedModelFormats.emplace_back(".c4d");
			m_supportedModelFormats.emplace_back(".ase");
			m_supportedModelFormats.emplace_back(".dxf");
			m_supportedModelFormats.emplace_back(".hmp");
			m_supportedModelFormats.emplace_back(".md2");
			m_supportedModelFormats.emplace_back(".md3");
			m_supportedModelFormats.emplace_back(".md5");
			m_supportedModelFormats.emplace_back(".mdc");
			m_supportedModelFormats.emplace_back(".mdl");
			m_supportedModelFormats.emplace_back(".nff");
			m_supportedModelFormats.emplace_back(".ply");
			m_supportedModelFormats.emplace_back(".stl");
			m_supportedModelFormats.emplace_back(".x");
			m_supportedModelFormats.emplace_back(".smd");
			m_supportedModelFormats.emplace_back(".lxo");
			m_supportedModelFormats.emplace_back(".lws");
			m_supportedModelFormats.emplace_back(".ter");
			m_supportedModelFormats.emplace_back(".ac3d");
			m_supportedModelFormats.emplace_back(".ms3d");
			m_supportedModelFormats.emplace_back(".cob");
			m_supportedModelFormats.emplace_back(".q3bsp");
			m_supportedModelFormats.emplace_back(".xgl");
			m_supportedModelFormats.emplace_back(".csm");
			m_supportedModelFormats.emplace_back(".bvh");
			m_supportedModelFormats.emplace_back(".b3d");
			m_supportedModelFormats.emplace_back(".ndo");
		}

		// Set supported shader formats
		{
			m_supportedShaderFormats.emplace_back(".hlsl");
		}

		// Set supported script formats
		{
			m_supportedScriptFormats.emplace_back(".as");
		}

		// Set supported font formats
		{
			m_supportedFontFormats.emplace_back(".ttf");
			m_supportedFontFormats.emplace_back(".ttc");
			m_supportedFontFormats.emplace_back(".cff");
			m_supportedFontFormats.emplace_back(".woff");
			m_supportedFontFormats.emplace_back(".otf");
			m_supportedFontFormats.emplace_back(".otc");
			m_supportedFontFormats.emplace_back(".pfa");
			m_supportedFontFormats.emplace_back(".pfb");
			m_supportedFontFormats.emplace_back(".fnt");
			m_supportedFontFormats.emplace_back(".bdf");
			m_supportedFontFormats.emplace_back(".pfr");
		}
	}

	//= DIRECTORIES ======================================================================
	bool FileSystem::CreateDirectory_(const string& path)
	{
		return create_directories(path);
	}

	bool FileSystem::DeleteDirectory(const string& directory)
	{
		return remove_all(directory);
	}

	bool FileSystem::DirectoryExists(const string& directory)
	{
		return exists(directory);
	}

	bool FileSystem::IsDirectory(const string& directory)
	{
		return is_directory(directory);
	}

	void FileSystem::OpenDirectoryWindow(const std::string& directory)
	{
		ShellExecute(nullptr, nullptr, StringToWString(directory).c_str(), nullptr, nullptr, SW_SHOW);
	}

	//====================================================================================

	//= FILES ============================================================================
	bool FileSystem::FileExists(const string& filePath)
	{
		return exists(filePath);
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
			LOG_ERROR("FileSystem: Could not delete \"" + filePath + "\". " + string(e.what()));
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
	//====================================================================================

	//= DIRECTORY PARSING ================================================================
	string FileSystem::GetFileNameFromFilePath(const string& path)
	{
		size_t lastindex = path.find_last_of("\\/");
		string fileName = path.substr(lastindex + 1, path.length());

		return fileName;
	}

	string FileSystem::GetFileNameNoExtensionFromFilePath(const string& filepath)
	{
		string fileName = GetFileNameFromFilePath(filepath);

		size_t lastindex = fileName.find_last_of('.');
		string fileNameNoExt = fileName.substr(0, lastindex);

		return fileNameNoExt;
	}

	string FileSystem::GetDirectoryFromFilePath(const string& filePath)
	{
		size_t lastindex = filePath.find_last_of("\\/");
		string directory = filePath.substr(0, lastindex + 1);

		return directory;
	}

	string FileSystem::GetFilePathWithoutExtension(const string& filePath)
	{
		string directory = GetDirectoryFromFilePath(filePath);
		string fileNameNoExt = GetFileNameNoExtensionFromFilePath(filePath);

		return directory + fileNameNoExt;
	}

	string FileSystem::GetExtensionFromFilePath(const string& filePath)
	{
		if (filePath.empty() || filePath == NOT_ASSIGNED)
		{
			return NOT_ASSIGNED;
		}

		size_t lastindex = filePath.find_last_of('.');
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

			subDirs.push_back(itr->path().generic_string());
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

			filePaths.push_back(itr->path().generic_string());
		}

		return filePaths;
	}
	//====================================================================================

	//= SUPPORTED FILES IN DIRECTORY ========================================================================================
	vector<string> FileSystem::GetSupportedFilesInDirectory(const string& directory)
	{
		vector<string> filesInDirectory = GetFilesInDirectory(directory);
		vector<string> imagesInDirectory = GetSupportedImageFilesFromPaths(filesInDirectory); // get all the images
		vector<string> scriptsInDirectory = GetSupportedScriptFilesFromPaths(filesInDirectory); // get all the scripts
		vector<string> modelsInDirectory = GetSupportedModelFilesFromPaths(filesInDirectory); // get all the models
		vector<string> supportedFiles;

		// get supported images
		for (const auto& imageInDirectory : imagesInDirectory)
		{
			supportedFiles.push_back(imageInDirectory);
		}

		// get supported scripts
		for (const auto& scriptInDirectory : scriptsInDirectory)
		{
			supportedFiles.push_back(scriptInDirectory);
		}

		// get supported models
		for (const auto& modelInDirectory : modelsInDirectory)
		{
			supportedFiles.push_back(modelInDirectory);
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

			imageFiles.push_back(path);
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

			audioFiles.push_back(path);
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

			scripts.push_back(path);
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

			images.push_back(path);
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

			sceneFiles.push_back(file);
		}

		return sceneFiles;
	}
	//===========================================================================================

	//= SUPPORTED FILE CHECKS ===================================================================
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
		return GetExtensionFromFilePath(filePath) == PREFAB_EXTENSION;
	}

	bool FileSystem::IsEngineModelFile(const string& filePath)
	{
		return GetExtensionFromFilePath(filePath) == MODEL_EXTENSION;
	}

	bool FileSystem::IsEngineMaterialFile(const string& filePath)
	{
		return GetExtensionFromFilePath(filePath) == MATERIAL_EXTENSION;
	}

	bool FileSystem::IsEngineMeshFile(const string& filePath)
	{
		return GetExtensionFromFilePath(filePath) == MESH_EXTENSION;
	}

	bool FileSystem::IsEngineSceneFile(const string& filePath)
	{
		return GetExtensionFromFilePath(filePath) == SCENE_EXTENSION;
	}

	bool FileSystem::IsEngineTextureFile(const string& filePath)
	{
		return GetExtensionFromFilePath(filePath) == TEXTURE_EXTENSION;
	}

	bool FileSystem::IsEngineShaderFile(const string& filePath)
	{
		return GetExtensionFromFilePath(filePath) == SHADER_EXTENSION;
	}

	bool FileSystem::IsEngineMetadataFile(const string& filePath)
	{
		return GetExtensionFromFilePath(filePath) == METADATA_EXTENSION;
	}
	//============================================================================================

	//= STRING PARSING =====================================================================
	// Returns a file path which is relative to the engine
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

	string FileSystem::GetWorkingDirectory()
	{
		return current_path().generic_string() + "/";
	}

	string FileSystem::GetParentDirectory(const string& directory)
	{
		size_t found	= directory.find_last_of("/\\");
		string result	= directory;

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
		size_t position = str.find(expression);
		string remaining = position != string::npos ? str.substr(position + expression.length()) : str;

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
		int slength = int(str.length()) + 1;
		int len = MultiByteToWideChar(CP_ACP, 0, str.c_str(), slength, nullptr, 0);
		wchar_t* buf = new wchar_t[len];
		MultiByteToWideChar(CP_ACP, 0, str.c_str(), slength, buf, len);
		wstring result(buf);
		delete[] buf;
		return result;
	}
	//=====================================================================================

	//= SUPPORTED ASSET FILE FORMATS ===========================================================
	vector<string> FileSystem::GetSupportedImageFormats()	{ return m_supportedImageFormats; }
	vector<string> FileSystem::GetSupportedAudioFormats()	{ return m_supportedAudioFormats; }
	vector<string> FileSystem::GetSupportedModelFormats()	{ return m_supportedModelFormats; }
	vector<string> FileSystem::GetSupportedShaderFormats()	{ return m_supportedShaderFormats; }
	vector<string> FileSystem::GetSupportedScriptFormats()	{ return m_supportedScriptFormats; }
	vector<string> FileSystem::GetSupportedFontFormats()	{ return m_supportedFontFormats; }
	//==========================================================================================
}