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

//= INCLUDES =============
#include "FileSystem.h"
#include "dirent.h"
#include <locale>
#include "../Core/Scene.h"
#include "../Graphics/Material.h"
#include <direct.h>
#include "../Logging/Log.h"
//========================

//= NAMESPACES =====
using namespace std;
//==================

//= DIRECTORIES ================================================================================================
bool FileSystem::CreateDirectory_(const string& path)
{
	if (!CreateDirectory(ToWString(path).c_str(), nullptr))
	{
		//DWORD err = GetLastError();
		//if (err != ERROR_ALREADY_EXISTS)
		return false;
	}

	return true;
}

bool FileSystem::OpenDirectoryInExplorer(const string& directory)
{
	HINSTANCE result = ShellExecute(nullptr, L"open", ToWString(directory).c_str(), nullptr, nullptr, SW_SHOWDEFAULT);
	return FAILED(result) ? false : true;
}

bool FileSystem::DeleteDirectory(const string& directory)
{
	bool deleted = _rmdir(directory.c_str()) != 0 ? false : true;

	// Deletion failure is usually caused by the fact 
	// that the directory to be deleted must be empty.
	if (!deleted)
	{
		// Delete all the files contained in the directory
		auto files = GetFilesInDirectory(directory);
		for (const auto& file : files)
			DeleteFile_(file);

		// Delete all the directories contained in the directory
		//auto directories = GetDirectoriesInDirectory(directory);
		//for (const auto& dir : directories)
			//DeleteDirectory(dir);
		// MUST FIX THIS

		// Try deleting now
		deleted = _rmdir(directory.c_str()) != 0 ? false : true;
	}

	return deleted;
}
//========================================================================================================

//= FILES ================================================================================================
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
	//DWORD err = GetLastError();
	return !CopyFile(ToWString(source).c_str(), ToWString(destination).c_str(), true);
}

string FileSystem::GetFileNameFromPath(const string& path)
{
	int lastindex = path.find_last_of("\\/");
	return path.substr(lastindex + 1, path.length());
}

string FileSystem::GetFileNameNoExtensionFromPath(const string& path)
{
	string fileName = GetFileNameFromPath(path);

	int lastindex = fileName.find_last_of(".");
	string rawName = fileName.substr(0, lastindex);

	return rawName;
}

string FileSystem::GetPathWithoutFileName(const string& path)
{
	int lastindex = path.find_last_of("\\/");
	return path.substr(0, lastindex + 1);
}

string FileSystem::GetPathWithoutFileNameExtension(const string& path)
{
	string rawPath = GetPathWithoutFileName(path);
	string fileName = GetFileNameNoExtensionFromPath(path);

	return rawPath + fileName;
}

string FileSystem::GetExtensionFromPath(const string& path)
{
	size_t lastindex = path.find_last_of(".");
	if (string::npos != lastindex)
		return path.substr(lastindex, path.length());

	// returns extension with dot
	return path;
}

string FileSystem::GetRelativePathFromAbsolutePath(const string& filePath)
{
	// NOTE: This function assumes that the path resolves somewhere 
	// inside the folder "Assets" (The default engine folder).
	// D:\Projects\Directus3D\Build\Assets\Models\cube\tex.jpg --> Assets\Models\cube\tex.jpg
	// It removes everything before the folder "Assets", making the path relative to the engine

	size_t position = filePath.find("Assets");

	if (position == string::npos)
		return filePath;

	return filePath.substr(position);
}

vector<string> FileSystem::GetSupportedImageFileFormats(bool includeUppercase)
{
	vector<string> supportedFormats;
	supportedFormats.push_back(".jpg");
	supportedFormats.push_back(".png");
	supportedFormats.push_back(".bmp");
	supportedFormats.push_back(".tga");
	supportedFormats.push_back(".dds");
	supportedFormats.push_back(".exr");
	supportedFormats.push_back(".raw");
	supportedFormats.push_back(".gif");
	supportedFormats.push_back(".hdr");
	supportedFormats.push_back(".ico");
	supportedFormats.push_back(".iff");
	supportedFormats.push_back(".jng");
	supportedFormats.push_back(".jpeg");
	supportedFormats.push_back(".koala");
	supportedFormats.push_back(".kodak");
	supportedFormats.push_back(".mng");
	supportedFormats.push_back(".pcx");
	supportedFormats.push_back(".pbm");
	supportedFormats.push_back(".pgm");
	supportedFormats.push_back(".ppm");
	supportedFormats.push_back(".pfm");
	supportedFormats.push_back(".pict");
	supportedFormats.push_back(".psd");
	supportedFormats.push_back(".raw");
	supportedFormats.push_back(".sgi");
	supportedFormats.push_back(".targa");
	supportedFormats.push_back(".tiff");
	supportedFormats.push_back(".wbmp");
	supportedFormats.push_back(".webp");
	supportedFormats.push_back(".xbm");
	supportedFormats.push_back(".xpm");

	if (includeUppercase)
	{
		int extCount = supportedFormats.size();
		for (auto i = 0; i < extCount; i++)
			supportedFormats.push_back(ConvertToUppercase(supportedFormats[i]));
	}

	return supportedFormats;
}

vector<string> FileSystem::GetSupportedAudioFileFormats(bool includeUppercase)
{
	vector<string> supportedFormats;
	supportedFormats.push_back(".aiff");
	supportedFormats.push_back(".asf");
	supportedFormats.push_back(".asx");
	supportedFormats.push_back(".dls");
	supportedFormats.push_back(".flac");
	supportedFormats.push_back(".fsb");
	supportedFormats.push_back(".it");
	supportedFormats.push_back(".m3u");
	supportedFormats.push_back(".midi");
	supportedFormats.push_back(".mod");
	supportedFormats.push_back(".mp2");
	supportedFormats.push_back(".mp3");
	supportedFormats.push_back(".ogg");
	supportedFormats.push_back(".pls");
	supportedFormats.push_back(".s3m");
	supportedFormats.push_back(".vag"); // PS2/PSP
	supportedFormats.push_back(".wav");
	supportedFormats.push_back(".wax");
	supportedFormats.push_back(".wma");
	supportedFormats.push_back(".xm");
	supportedFormats.push_back(".xma"); // XBOX 360

	if (includeUppercase)
	{
		int extCount = supportedFormats.size();
		for (auto i = 0; i < extCount; i++)
			supportedFormats.push_back(ConvertToUppercase(supportedFormats[i]));
	}

	return supportedFormats;
}

vector<string> FileSystem::GetDirectoriesInDirectory(const string& directory)
{
	vector<string> directoryPaths;

	DIR* dir = opendir(directory.c_str());
	struct dirent* entry = readdir(dir);
	while (entry != nullptr)
	{
		if (entry->d_type == DT_DIR)
			directoryPaths.push_back(entry->d_name);

		entry = readdir(dir);
	}
	closedir(dir);

	// erase the first 2 elements which are "." and ".."
	directoryPaths.erase(directoryPaths.begin(), directoryPaths.begin() + 2);

	return directoryPaths;
}

vector<string> FileSystem::GetFilesInDirectory(const string& directory)
{
	DIR* dir;
	struct dirent* ent;
	vector<string> filePaths;

	if ((dir = opendir(directory.c_str())) == nullptr)
		return filePaths;

	while ((ent = readdir(dir)) != nullptr)
		filePaths.push_back(directory + "\\" + ent->d_name);

	closedir(dir);

	return filePaths;
}

//= SUPPORTED FILES IN DIRECTORY ========================================================================================
vector<string> FileSystem::GetSupportedFilesInDirectory(const string& directory)
{
	vector<string> filesInDirectory = GetFilesInDirectory(directory);

	vector<string> imagesInDirectory = GetSupportedImageFilesFromPaths(filesInDirectory); // get all the images
	vector<string> scriptsInDirectory = GetSupportedScriptFilesFromPaths(filesInDirectory); // get all the scripts
	vector<string> modelsInDirectory = GetSupportedModelFilesFromPaths(filesInDirectory); // get all the models
	vector<string> supportedFiles;

	// get supported images
	for (const string& imageInDirectory : imagesInDirectory)
		supportedFiles.push_back(imageInDirectory);

	// get supported scripts
	for (const string& scriptInDirectory : scriptsInDirectory)
		supportedFiles.push_back(scriptInDirectory);

	// get supported models
	for (const string& modelInDirectory : modelsInDirectory)
		supportedFiles.push_back(modelInDirectory);

	return supportedFiles;
}

vector<string> FileSystem::GetSupportedImageFilesFromPaths(const vector<string>& paths)
{
	vector<string> imageFiles;
	for (int i = 0; i < paths.size(); i++)
	{
		string filePath = paths[i];

		if (IsSupportedImageFile(filePath))
			imageFiles.push_back(filePath);
	}

	return imageFiles;
}

vector<string> FileSystem::GetSupportedAudioFilesFromPaths(const vector<string>& paths)
{
	vector<string> audioFiles;
	for (int i = 0; i < paths.size(); i++)
	{
		string filePath = paths[i];

		if (IsSupportedAudioFile(filePath))
			audioFiles.push_back(filePath);
	}

	return audioFiles;
}

vector<string> FileSystem::GetSupportedScriptFilesFromPaths(const vector<string>& paths)
{
	vector<string> scripts;
	for (int i = 0; i < paths.size(); i++)
	{
		string filePath = paths[i];

		if (IsSupportedScriptFile(filePath))
			scripts.push_back(filePath);
	}

	return scripts;
}

vector<string> FileSystem::GetSupportedModelFilesFromPaths(const vector<string>& paths)
{
	vector<string> images;
	for (int i = 0; i < paths.size(); i++)
	{
		string filePath = paths[i];

		if (IsSupportedModelFile(filePath))
			images.push_back(filePath);
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
	for (auto file : files)
		if (IsSupportedSceneFile(file))
			sceneFiles.push_back(file);

	return sceneFiles;
}
//===========================================================================================

//= SUPPORTED FILE CHECKS ===================================================================
bool FileSystem::IsSupportedPrefabFile(const string& filePath)
{
	return GetExtensionFromPath(filePath) == PREFAB_EXTENSION ? true : false;
}

bool FileSystem::IsSupportedAudioFile(const string& path)
{
	string fileExt = GetExtensionFromPath(path);
	vector<string> supportedAudioFilesExt = GetSupportedAudioFileFormats(true);
	for (int i = 0; i < supportedAudioFilesExt.size(); i++)
		if (fileExt == supportedAudioFilesExt[i])
			return true;

	return false;
}

bool FileSystem::IsSupportedImageFile(const string& path)
{
	string fileExt = GetExtensionFromPath(path);
	vector<string> supportedImageExt = GetSupportedImageFileFormats(true);

	for (int i = 0; i < supportedImageExt.size(); i++)
		if (fileExt == supportedImageExt[i])
			return true;

	return false;
}

bool FileSystem::IsSupportedScriptFile(const string& path)
{
	string fileExt = GetExtensionFromPath(path);
	vector<string> supportedExt;
	supportedExt.push_back(".as");

	for (int i = 0; i < supportedExt.size(); i++)
	{
		if (fileExt == supportedExt[i] || fileExt == ConvertToUppercase(supportedExt[i]))
			return true;
	}

	return false;
}

bool FileSystem::IsSupportedModelFile(const string& path)
{
	string fileExt = GetExtensionFromPath(path);
	vector<string> supportedExt;
	supportedExt.push_back(".3ds");
	supportedExt.push_back(".obj");
	supportedExt.push_back(".fbx");
	supportedExt.push_back(".blend");
	supportedExt.push_back(".dae");
	supportedExt.push_back(".lwo");
	supportedExt.push_back(".c4d");
	supportedExt.push_back(".ase");
	supportedExt.push_back(".dxf");
	supportedExt.push_back(".hmp");
	supportedExt.push_back(".md2");
	supportedExt.push_back(".md3");
	supportedExt.push_back(".md5");
	supportedExt.push_back(".mdc");
	supportedExt.push_back(".mdl");
	supportedExt.push_back(".nff");
	supportedExt.push_back(".ply");
	supportedExt.push_back(".stl");
	supportedExt.push_back(".x");
	supportedExt.push_back(".smd");
	supportedExt.push_back(".lxo");
	supportedExt.push_back(".lws");
	supportedExt.push_back(".ter");
	supportedExt.push_back(".ac3d");
	supportedExt.push_back(".ms3d");
	supportedExt.push_back(".cob");
	supportedExt.push_back(".q3bsp");
	supportedExt.push_back(".xgl");
	supportedExt.push_back(".csm");
	supportedExt.push_back(".bvh");
	supportedExt.push_back(".b3d");
	supportedExt.push_back(".ndo");

	for (int i = 0; i < supportedExt.size(); i++)
	{
		if (fileExt == supportedExt[i] || fileExt == ConvertToUppercase(supportedExt[i]))
			return true;
	}

	return false;
}

bool FileSystem::IsSupportedShaderFile(const string& path)
{
	string fileExt = GetExtensionFromPath(path);
	vector<string> supportedExtentions;
	supportedExtentions.push_back(".hlsl");

	for (const string& supportedExt : supportedExtentions)
	{
		if (fileExt == supportedExt || fileExt == ConvertToUppercase(supportedExt))
			return true;
	}

	return false;
}

bool FileSystem::IsSupportedMeshFile(const string& filePath)
{
	return GetExtensionFromPath(filePath) == MESH_EXTENSION ? true : false;
}

bool FileSystem::IsSupportedMaterialFile(const string& filePath)
{
	return GetExtensionFromPath(filePath) == MATERIAL_EXTENSION ? true : false;
}

bool FileSystem::IsSupportedSceneFile(const string& filePath)
{
	return GetExtensionFromPath(filePath) == SCENE_EXTENSION ? true : false;
}

bool FileSystem::IsMetadataFile(const string& filePath)
{
	return GetExtensionFromPath(filePath) == METADATA_EXTENSION ? true : false;
}
//============================================================================================

string FileSystem::ConvertToUppercase(const string& lower)
{
	locale loc;
	string upper;
	for (string::size_type i = 0; i < lower.length(); ++i)
		upper += std::toupper(lower[i], loc);

	return upper;
}

std::wstring FileSystem::ToWString(const std::string& str)
{
	return std::wstring(str.begin(), str.end());
}