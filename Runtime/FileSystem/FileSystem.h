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

#define NOT_ASSIGNED "N/A"
#define NOT_ASSIGNED_HASH -1

//= METADATA ===============================
#define METADATA_EXTENSION ".xml"
#define METADATA_TYPE_TEXTURE "Texture"
#define METADATA_TYPE_AUDIOCLIP "Audio_Clip"
//==========================================

//= ENGINE EXTENSIONS ==============
#define SCENE_EXTENSION ".directus"
#define MATERIAL_EXTENSION ".mat"
#define MODEL_EXTENSION ".model"
#define PREFAB_EXTENSION ".prefab"
#define SHADER_EXTENSION ".shader"
#define TEXTURE_EXTENSION ".texture"
//==================================

namespace Directus
{
	class DLL_API FileSystem
	{
	public:
		static void Initialize();

		//= DIRECTORIES ==================================================
		static bool CreateDirectory_(const std::string& directory);
		static bool DeleteDirectory(const std::string& directory);
		static bool DirectoryExists(const std::string& directory);
		static bool IsDirectory(const std::string& directory);
		//================================================================

		//= FILES ============================================================================
		static bool FileExists(const std::string& path);
		static bool DeleteFile_(const std::string& filePath);
		static bool CopyFileFromTo(const std::string& source, const std::string& destination);
		//====================================================================================

		//= DIRECTORY PARSING  =================================================================
		static std::string GetFileNameFromFilePath(const std::string& path);
		static std::string GetFileNameNoExtensionFromFilePath(const std::string& path);
		static std::string GetDirectoryFromFilePath(const std::string& path);
		static std::string GetFilePathWithoutExtension(const std::string& path);
		static std::string GetExtensionFromFilePath(const std::string& path);
		static std::string GetRelativeFilePath(const std::string& filePath);
		static std::string GetWorkingDirectory();
		static std::vector<std::string> GetDirectoriesInDirectory(const std::string& directory);
		static std::vector<std::string> GetFilesInDirectory(const std::string& directory);
		//======================================================================================

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
		static bool IsSupportedAudioFile(const std::string& filePath);
		static bool IsSupportedImageFile(const std::string& filePath);	
		static bool IsSupportedModelFile(const std::string& filePath);
		static bool IsSupportedShaderFile(const std::string& filePath);
		static bool IsSupportedFontFile(const std::string& filePath);
		static bool IsEngineScriptFile(const std::string& filePath);
		static bool IsEnginePrefabFile(const std::string& filePath);		
		static bool IsEngineMaterialFile(const std::string& filePath);
		static bool IsEngineModelFile(const std::string& filePath);
		static bool IsEngineSceneFile(const std::string& filePath);
		static bool IsEngineTextureFile(const std::string& filePath);
		static bool IsEngineShaderFile(const std::string& filePath);
		static bool IsEngineMetadataFile(const std::string& filePath);
		//=============================================================

		//= STRING PARSING =============================================================================================================================
		static std::string GetStringAfterExpression(const std::string& str, const std::string& expression);
		static std::string GetStringBetweenExpressions(const std::string& str, const std::string& firstExpression, const std::string& secondExpression);
		static std::string ConvertToUppercase(const std::string& lower);
		
		static std::string ReplaceExpression(const std::string& str, const std::string& from, const std::string& to);
		//==============================================================================================================================================

		//= SUPPORTED ASSET FILE FORMATS ===========================
		static std::vector<std::string> GetSupportedImageFormats();
		static std::vector<std::string> GetSupportedAudioFormats();
		static std::vector<std::string> GetSupportedModelFormats();
		static std::vector<std::string> GetSupportedShaderFormats();
		static std::vector<std::string> GetSupportedScriptFormats();
		static std::vector<std::string> GetSupportedFontFormats();
		//==========================================================

	private:
		static std::vector<std::string> m_supportedImageFormats;
		static std::vector<std::string> m_supportedAudioFormats;
		static std::vector<std::string> m_supportedModelFormats;
		static std::vector<std::string> m_supportedShaderFormats;
		static std::vector<std::string> m_supportedScriptFormats;
		static std::vector<std::string> m_supportedFontFormats;
	};
}