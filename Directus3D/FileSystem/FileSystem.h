/*
Copyright(c) 2016 Panos Karabelas

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

//= INCLUDES ====
#include <vector>
//===============

#define SCENE_EXTENSION ".directus"
#define MATERIAL_EXTENSION ".mat"
#define METADATA_EXTENSION ".meta"
#define MESH_EXTENSION ".msh"
#define DATA_NOT_ASSIGNED "N/A"
#define PATH_NOT_ASSIGNED "PATH_NOT_ASSIGNED"

class __declspec(dllexport) FileSystem
{
public:
	static bool FileExists(const std::string& path);
	static bool RemoveFile(const std::string& filePath);
	static void CreateFolder(const std::string& path);
	static void CopyFileFromTo(const std::string& source, const std::string& destination);

	static std::string GetFileNameFromPath(const std::string& path);
	static std::string GetFileNameNoExtensionFromPath(const std::string& path);
	static std::string GetPathWithoutFileName(const std::string& path);
	static std::string GetPathWithoutFileNameExtension(const std::string& path);
	static std::string GetExtensionFromPath(const std::string& path);
	static std::string GetRelativePathFromAbsolutePath(const std::string& path);

	static std::vector<std::string> GetSupportedImageFormats(bool includeUppercase);
	static bool IsMetadataFile(const std::string& filePath);
	static bool IsMaterialFile(const std::string& filePath);
	static bool IsSceneFile(const std::string& filePath);

	static std::vector<std::string> GetFoldersInDirectory(const std::string& directory);
	static std::vector<std::string> GetFilesInDirectory(const std::string& directory);
	static std::vector<std::string> GetSupportedFilesInDirectory(const std::string& directory);
	static std::vector<std::string> GetImagesFromFilePaths(const std::vector<std::string>& paths);
	static std::vector<std::string> GetScriptsFromFilePaths(const std::vector<std::string>& paths);
	static std::vector<std::string> GetModelsFromFilePaths(const std::vector<std::string>& paths);
	static std::vector<std::string> GetSupportedModelsInDirectory(const std::string& directory);

	static bool IsSupportedImage(const std::string& path);
	static bool IsSupportedScript(const std::string& path);
	static bool IsSupportedModel(const std::string& path);
	static bool IsSupportedShader(const std::string& path);

	// string
	static std::string ConvertToUppercase(const std::string& lower);
};
