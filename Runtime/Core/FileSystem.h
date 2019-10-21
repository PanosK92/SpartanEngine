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

#pragma once

//= INCLUDES ==================
#include <vector>
#include <string>
#include "../Core/EngineDefs.h"
//=============================

//========================================================
static const char* EXTENSION_WORLD			= ".world";
static const char* EXTENSION_MATERIAL		= ".material";
static const char* EXTENSION_MODEL			= ".model";
static const char* EXTENSION_PREFAB			= ".prefab";
static const char* EXTENSION_SHADER			= ".shader";
static const char* EXTENSION_FONT           = ".font";
static const char* EXTENSION_TEXTURE		= ".texture";
static const char* EXTENSION_MESH			= ".mesh";
static const char* EXTENSION_AUDIO          = ".audio";
//========================================================

namespace Spartan
{
	class SPARTAN_CLASS FileSystem
	{
	public:
		static void Initialize();

        // String operations
        static bool IsEmptyOrWhitespace(const std::string& var);
        static bool IsAlphanumeric(const std::string& var);
        static std::string GetStringBeforeExpression(const std::string& str, const std::string& exp);
        static std::string GetStringAfterExpression(const std::string& str, const std::string& exp);
        static std::string GetStringBetweenExpressions(const std::string& str, const std::string& exp_a, const std::string& exp_b);
        static std::string ConvertToUppercase(const std::string& lower);
        static std::string ReplaceExpression(const std::string& str, const std::string& from, const std::string& to);
        static std::wstring StringToWstring(const std::string& str);
        static std::vector<std::string> GetIncludedFiles(const std::string& file_path);

		// Directories
		static bool CreateDirectory_(const std::string& path);
		static bool DeleteDirectory(const std::string& directory);
		static bool DirectoryExists(const std::string& directory);
		static bool IsDirectory(const std::string& directory);
		static void OpenDirectoryWindow(const std::string& directory);

		// Files
		static bool FileExists(const std::string& file_path);
        static bool IsFile(const std::string& file_path);
		static bool DeleteFile_(const std::string& file_path);
		static bool CopyFileFromTo(const std::string& source, const std::string& destination);

		// Directory manipulation
		static std::string GetFileNameFromFilePath(const std::string& file_path);
		static std::string GetFileNameNoExtensionFromFilePath(const std::string& file_path);
		static std::string GetDirectoryFromFilePath(const std::string& file_path);
		static std::string GetFilePathWithoutExtension(const std::string& file_path);
		static std::string GetExtensionFromFilePath(const std::string& file_path);
        static std::string NativizeFilePath(const std::string& file_path);
		static std::string GetRelativeFilePath(const std::string& file_path_absolute);
		static std::string GetWorkingDirectory();
		static std::string GetParentDirectory(const std::string& directory);
		static std::vector<std::string> GetDirectoriesInDirectory(const std::string& directory);
		static std::vector<std::string> GetFilesInDirectory(const std::string& directory);

		// Get supported files in directory
		static std::vector<std::string> GetSupportedFilesInDirectory(const std::string& directory);
		static std::vector<std::string> GetSupportedImageFilesFromPaths(const std::vector<std::string>& paths);
		static std::vector<std::string> GetSupportedAudioFilesFromPaths(const std::vector<std::string>& paths);
		static std::vector<std::string> GetSupportedScriptFilesFromPaths(const std::vector<std::string>& paths);
		static std::vector<std::string> GetSupportedModelFilesFromPaths(const std::vector<std::string>& paths);
		static std::vector<std::string> GetSupportedModelFilesInDirectory(const std::string& directory);
		static std::vector<std::string> GetSupportedSceneFilesInDirectory(const std::string& directory);

		// Check if file is supported
		static bool IsSupportedAudioFile(const std::string& file_path);
		static bool IsSupportedImageFile(const std::string& file_path);
		static bool IsSupportedModelFile(const std::string& file_path);
		static bool IsSupportedShaderFile(const std::string& file_path);
		static bool IsSupportedFontFile(const std::string& file_path);
		static bool IsEngineScriptFile(const std::string& file_path);
		static bool IsEnginePrefabFile(const std::string& file_path);
		static bool IsEngineMaterialFile(const std::string& file_path);
		static bool IsEngineMeshFile(const std::string& file_path);
		static bool IsEngineModelFile(const std::string& file_path);
		static bool IsEngineSceneFile(const std::string& file_path);
		static bool IsEngineTextureFile(const std::string& file_path);
        static bool IsEngineAudioFile(const std::string& file_path);
		static bool IsEngineShaderFile(const std::string& file_path);
        static bool IsEngineFile(const std::string& file_path);

		// Lists of supported file formats
		static const auto& GetSupportedImageFormats()	{ return m_supportedImageFormats; }
		static const auto& GetSupportedAudioFormats()	{ return m_supportedAudioFormats; }
		static const auto& GetSupportedModelFormats()	{ return m_supportedModelFormats; }
		static const auto& GetSupportedShaderFormats()	{ return m_supportedShaderFormats; }
		static const auto& GetSupportedScriptFormats()	{ return m_supportedScriptFormats; }
		static const auto& GetSupportedFontFormats()	{ return m_supportedFontFormats; }

	private:
		static std::vector<std::string> m_supportedImageFormats;
		static std::vector<std::string> m_supportedAudioFormats;
		static std::vector<std::string> m_supportedModelFormats;
		static std::vector<std::string> m_supportedShaderFormats;
		static std::vector<std::string> m_supportedScriptFormats;
		static std::vector<std::string> m_supportedFontFormats;
	};
}
