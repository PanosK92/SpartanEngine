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

#pragma once

//= INCLUDES ==============
#include <vector>
#include "../Core/Helper.h"
//=========================

#define DATA_NOT_ASSIGNED "N/A"

//= METADATA ===================================
#define METADATA_EXTENSION ".meta"
#define METADATA_TYPE_TEXTURE "Texture"
#define METADATA_TYPE_AUDIOCLIP "Audio_Clip"
//==============================================

//= CUSTOM EXTENSIONS ==========================
#define SCENE_EXTENSION ".directus"
#define MATERIAL_EXTENSION ".mat"
#define MESH_EXTENSION ".msh"
#define PREFAB_EXTENSION ".prefab"
//==============================================

namespace Directus
{
	class DllExport FileSystem
	{
	public:
		//= DIRECTORIES ==================================================
		static bool CreateDirectory_(const std::string& path);
		static bool OpenDirectoryInExplorer(const std::string& directory);
		static bool DeleteDirectory(const std::string& directory);
		//================================================================

		//= FILES ========================================================
		static bool FileExists(const std::string& path);
		static bool DeleteFile_(const std::string& filePath);
		static bool CopyFileFromTo(const std::string& source, const std::string& destination);

		static std::string GetFileNameFromPath(const std::string& path);
		static std::string GetFileNameNoExtensionFromPath(const std::string& path);
		static std::string GetPathWithoutFileName(const std::string& path);
		static std::string GetPathWithoutFileNameExtension(const std::string& path);
		static std::string GetExtensionFromPath(const std::string& path);
		static std::string GetRelativePathFromAbsolutePath(const std::string& path);

		static std::vector<std::string> GetSupportedImageFileFormats(bool includeUppercase);
		static std::vector<std::string> GetSupportedAudioFileFormats(bool includeUppercase);

		static std::vector<std::string> GetDirectoriesInDirectory(const std::string& directory);
		static std::vector<std::string> GetFilesInDirectory(const std::string& directory);

		//= SUPPORTED FILES IN DIRECTORY ======================================================================
		static std::vector<std::string> GetSupportedFilesInDirectory(const std::string& directory);
		static std::vector<std::string> GetSupportedImageFilesFromPaths(const std::vector<std::string>& paths);
		static std::vector<std::string> GetSupportedAudioFilesFromPaths(const std::vector<std::string>& paths);
		static std::vector<std::string> GetSupportedScriptFilesFromPaths(const std::vector<std::string>& paths);
		static std::vector<std::string> GetSupportedModelFilesFromPaths(const std::vector<std::string>& paths);
		static std::vector<std::string> GetSupportedModelFilesInDirectory(const std::string& directory);
		static std::vector<std::string> GetSupportedSceneFilesInDirectory(const std::string& directory);
		//======================================================================================================

		//= SUPPORTED FILE CHECKS =====================================
		static bool IsSupportedPrefabFile(const std::string& filePath);
		static bool IsSupportedAudioFile(const std::string& filePath);
		static bool IsSupportedImageFile(const std::string& filePath);
		static bool IsSupportedScriptFile(const std::string& filePath);
		static bool IsSupportedModelFile(const std::string& filePath);
		static bool IsSupportedShaderFile(const std::string& filePath);
		static bool IsSupportedMeshFile(const std::string& filePath);
		static bool IsSupportedMaterialFile(const std::string& filePath);
		static bool IsSupportedSceneFile(const std::string& filePath);
		static bool IsMetadataFile(const std::string& filePath);
		//=============================================================

		// string
		static std::string ConvertToUppercase(const std::string& lower);
		static std::wstring ToWString(const std::string& str);
	};
}