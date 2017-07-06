/*
Copyright(c) 2016-2017 Panos Karabelas

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

//= INCLUDES ====================
#include "FileSystem.h"
#include <filesystem>
#include <locale>
#include <regex>
#include "../Core/Scene.h"
#include "../Graphics/Material.h"
//===============================

//= NAMESPACES =========================
using namespace std;
namespace fs = experimental::filesystem;
//======================================

namespace Directus
{
	vector<string> FileSystem::m_supportedImageFormats;
	vector<string> FileSystem::m_supportedAudioFormats;
	vector<string> FileSystem::m_supportedModelFormats;
	vector<string> FileSystem::m_supportedShaderFormats;
	vector<string> FileSystem::m_supportedScriptFormats;

	void FileSystem::Initialize()
	{
		// Set supported image formats
		{
			m_supportedImageFormats.push_back(".jpg");
			m_supportedImageFormats.push_back(".png");
			m_supportedImageFormats.push_back(".bmp");
			m_supportedImageFormats.push_back(".tga");
			m_supportedImageFormats.push_back(".dds");
			m_supportedImageFormats.push_back(".exr");
			m_supportedImageFormats.push_back(".raw");
			m_supportedImageFormats.push_back(".gif");
			m_supportedImageFormats.push_back(".hdr");
			m_supportedImageFormats.push_back(".ico");
			m_supportedImageFormats.push_back(".iff");
			m_supportedImageFormats.push_back(".jng");
			m_supportedImageFormats.push_back(".jpeg");
			m_supportedImageFormats.push_back(".koala");
			m_supportedImageFormats.push_back(".kodak");
			m_supportedImageFormats.push_back(".mng");
			m_supportedImageFormats.push_back(".pcx");
			m_supportedImageFormats.push_back(".pbm");
			m_supportedImageFormats.push_back(".pgm");
			m_supportedImageFormats.push_back(".ppm");
			m_supportedImageFormats.push_back(".pfm");
			m_supportedImageFormats.push_back(".pict");
			m_supportedImageFormats.push_back(".psd");
			m_supportedImageFormats.push_back(".raw");
			m_supportedImageFormats.push_back(".sgi");
			m_supportedImageFormats.push_back(".targa");
			m_supportedImageFormats.push_back(".tiff");
			m_supportedImageFormats.push_back(".wbmp");
			m_supportedImageFormats.push_back(".webp");
			m_supportedImageFormats.push_back(".xbm");
			m_supportedImageFormats.push_back(".xpm");
		}

		// Set supported audio formats
		{
			m_supportedAudioFormats.push_back(".aiff");
			m_supportedAudioFormats.push_back(".asf");
			m_supportedAudioFormats.push_back(".asx");
			m_supportedAudioFormats.push_back(".dls");
			m_supportedAudioFormats.push_back(".flac");
			m_supportedAudioFormats.push_back(".fsb");
			m_supportedAudioFormats.push_back(".it");
			m_supportedAudioFormats.push_back(".m3u");
			m_supportedAudioFormats.push_back(".midi");
			m_supportedAudioFormats.push_back(".mod");
			m_supportedAudioFormats.push_back(".mp2");
			m_supportedAudioFormats.push_back(".mp3");
			m_supportedAudioFormats.push_back(".ogg");
			m_supportedAudioFormats.push_back(".pls");
			m_supportedAudioFormats.push_back(".s3m");
			m_supportedAudioFormats.push_back(".vag"); // PS2/PSP
			m_supportedAudioFormats.push_back(".wav");
			m_supportedAudioFormats.push_back(".wax");
			m_supportedAudioFormats.push_back(".wma");
			m_supportedAudioFormats.push_back(".xm");
			m_supportedAudioFormats.push_back(".xma"); // XBOX 360
		}

		// Set supported model formats
		{
			m_supportedModelFormats.push_back(".3ds");
			m_supportedModelFormats.push_back(".obj");
			m_supportedModelFormats.push_back(".fbx");
			m_supportedModelFormats.push_back(".blend");
			m_supportedModelFormats.push_back(".dae");
			m_supportedModelFormats.push_back(".lwo");
			m_supportedModelFormats.push_back(".c4d");
			m_supportedModelFormats.push_back(".ase");
			m_supportedModelFormats.push_back(".dxf");
			m_supportedModelFormats.push_back(".hmp");
			m_supportedModelFormats.push_back(".md2");
			m_supportedModelFormats.push_back(".md3");
			m_supportedModelFormats.push_back(".md5");
			m_supportedModelFormats.push_back(".mdc");
			m_supportedModelFormats.push_back(".mdl");
			m_supportedModelFormats.push_back(".nff");
			m_supportedModelFormats.push_back(".ply");
			m_supportedModelFormats.push_back(".stl");
			m_supportedModelFormats.push_back(".x");
			m_supportedModelFormats.push_back(".smd");
			m_supportedModelFormats.push_back(".lxo");
			m_supportedModelFormats.push_back(".lws");
			m_supportedModelFormats.push_back(".ter");
			m_supportedModelFormats.push_back(".ac3d");
			m_supportedModelFormats.push_back(".ms3d");
			m_supportedModelFormats.push_back(".cob");
			m_supportedModelFormats.push_back(".q3bsp");
			m_supportedModelFormats.push_back(".xgl");
			m_supportedModelFormats.push_back(".csm");
			m_supportedModelFormats.push_back(".bvh");
			m_supportedModelFormats.push_back(".b3d");
			m_supportedModelFormats.push_back(".ndo");
		}

		// Set supported shader formats
		{
			m_supportedShaderFormats.push_back(".hlsl");
		}

		// Set supported script formats
		{
			m_supportedScriptFormats.push_back(".as");
		}
	}

	//= DIRECTORY MANAEMENT ==============================================================
	bool FileSystem::CreateDirectory_(const string& path)
	{
		return fs::create_directories(path);
	}

	bool FileSystem::OpenDirectoryInExplorer(const string& directory)
	{
		HINSTANCE result = ShellExecute(nullptr, L"open", ToWString(directory).c_str(), nullptr, nullptr, SW_SHOWDEFAULT);
		return !FAILED(result);
	}

	bool FileSystem::DeleteDirectory(const string& directory)
	{
		return fs::remove_all(directory);
	}
	//====================================================================================

	//= FILES ============================================================================
	bool FileSystem::FileExists(const string& path)
	{
		struct stat buffer;
		return (stat(path.c_str(), &buffer) == 0);
	}

	bool FileSystem::DeleteFile_(const string& filePath)
	{
		return remove(filePath.c_str()) != 0 ? false : true;
	}

	bool FileSystem::CopyFileFromTo(const string& source, const string& destination)
	{
		return !CopyFile(ToWString(source).c_str(), ToWString(destination).c_str(), true);
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

		size_t lastindex = fileName.find_last_of(".");
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
		string extension = DATA_NOT_ASSIGNED;

		size_t lastindex = filePath.find_last_of(".");
		if (string::npos != lastindex)
		{
			// extension with dot included
			extension = filePath.substr(lastindex, filePath.length());
		}

		return extension;
	}

	vector<string> FileSystem::GetDirectoriesInDirectory(const string& directory)
	{
		vector<string> subDirs;
		fs::directory_iterator end_itr; // default construction yields past-the-end
		for (fs::directory_iterator itr(directory); itr != end_itr; ++itr)
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
		fs::directory_iterator end_itr; // default construction yields past-the-end
		for (fs::directory_iterator itr(directory); itr != end_itr; ++itr)
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

	bool FileSystem::IsEngineSceneFile(const string& filePath)
	{
		return GetExtensionFromFilePath(filePath) == SCENE_EXTENSION;
	}

	bool FileSystem::IsEngineMetadataFile(const string& filePath)
	{
		return GetExtensionFromFilePath(filePath) == METADATA_EXTENSION;
	}
	//============================================================================================

	//= STRING PARSING =====================================================================
	string ToString(WCHAR* txt)
	{
		wstring ws(txt);
		return string(ws.begin(), ws.end());
	}

	// Returns a file path which is relative to the engine
	string FileSystem::GetRelativeFilePath(const string& absoluteFilePath)
	{
		string currentDir = GetWorkingDirectory();
		string absoluteDir = absoluteFilePath;

		currentDir = ReplaceExpression(currentDir, "\"", "\\");
		absoluteDir = ReplaceExpression(absoluteDir, "\"", "\\");

		currentDir = ReplaceExpression(currentDir, "/", "\\");
		absoluteDir = ReplaceExpression(absoluteDir, "/", "\\");

		const int MAX_FILENAME_LEN = 512;
		const int ABSOLUTE_NAME_START = 3;
		const char SLASH = '\\';

		int afMarker = 0, rfMarker = 0;
		int cdLen = 0, afLen = 0;
		int i = 0;
		int levels = 0;
		static char relativeFilename[MAX_FILENAME_LEN + 1];
		cdLen = strlen(currentDir.c_str());
		afLen = strlen(absoluteDir.c_str());

		// Make sure the paths are not too long or too short
		if (cdLen > MAX_FILENAME_LEN || cdLen < ABSOLUTE_NAME_START + 1 ||
			afLen > MAX_FILENAME_LEN || afLen < ABSOLUTE_NAME_START + 1)
		{
			return DATA_NOT_ASSIGNED;
		}

		// Make sure the paths are not on different drives
		if (currentDir[0] != absoluteDir[0])
		{
			return absoluteDir;
		}

		// Find out how much of the current directory is in the absolute filename
		i = ABSOLUTE_NAME_START;
		while (i < afLen && i < cdLen && currentDir[i] == absoluteDir[i])
		{
			i++;
		}

		if (i == cdLen && (absoluteDir[i] == SLASH || absoluteDir[i - 1] == SLASH))
		{
			// the whole current directory name is in the file name,
			// so we just trim off the current directory name to get the
			// current file name.
			if (absoluteDir[i] == SLASH)
			{
				// a directory name might have a trailing slash but a relative
				// file name should not have a leading one...
				i++;
			}

			strcpy(relativeFilename, &absoluteDir[i]);
			return relativeFilename;
		}

		// The file is not in a child directory of the current directory, so we
		// need to step back the appropriate number of parent directories by
		// using "..\"s.  First find out how many levels deeper we are than the
		// common directory
		afMarker = i;
		levels = 1;
		// count the number of directory levels we have to go up to get to the
		// common directory
		while (i < cdLen)
		{
			i++;
			if (currentDir[i] == SLASH)
			{
				// make sure it's not a trailing slash
				i++;
				if (currentDir[i] != '\0')
				{
					levels++;
				}
			}
		}

		// Move the absolute filename marker back to the 
		// start of the directory name that it has stopped in.
		while (afMarker > 0 && absoluteDir[afMarker - 1] != SLASH)
		{
			afMarker--;
		}

		// Check that the result will not be too long
		if (levels * 3 + afLen - afMarker > MAX_FILENAME_LEN)
		{
			return DATA_NOT_ASSIGNED;
		}

		// Add the appropriate number of "..\"s.
		rfMarker = 0;
		for (i = 0; i < levels; i++)
		{
			relativeFilename[rfMarker++] = '.';
			relativeFilename[rfMarker++] = '.';
			relativeFilename[rfMarker++] = SLASH;
		}

		// Copy the rest of the filename into the result string
		strcpy(&relativeFilename[rfMarker], &absoluteDir[afMarker]);

		return relativeFilename;
	}

	string FileSystem::GetWorkingDirectory()
	{
		return fs::current_path().generic_string();
	}

	string FileSystem::GetStringAfterExpression(const string& str, const string& expression)
	{
		// ("The quick brown fox", "brown") -> "brown fox"
		size_t position = str.find(expression);
		string remaining = position != string::npos ? str.substr(position) : str;

		return remaining;
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

	wstring FileSystem::ToWString(const string& str)
	{
		return wstring(str.begin(), str.end());
	}
	//=====================================================================================

	//= SUPPORTED ASSET FILE FORMATS ==========================================================
	vector<string> FileSystem::GetSupportedImageFormats() { return m_supportedImageFormats; }
	vector<string> FileSystem::GetSupportedAudioFormats() { return m_supportedAudioFormats; }
	vector<string> FileSystem::GetSupportedModelFormats() { return m_supportedModelFormats; }
	vector<string> FileSystem::GetSupportedShaderFormats() { return m_supportedShaderFormats; }
	vector<string> FileSystem::GetSupportedScriptFormats() { return m_supportedScriptFormats; }
	//=========================================================================================
}