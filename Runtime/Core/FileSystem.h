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

//====================================================
static const char* EXTENSION_WORLD		= ".world";
static const char* EXTENSION_MATERIAL	= ".material";
static const char* EXTENSION_MODEL		= ".model";
static const char* EXTENSION_PREFAB		= ".prefab";
static const char* EXTENSION_SHADER		= ".shader";
static const char* EXTENSION_FONT       = ".font";
static const char* EXTENSION_TEXTURE	= ".texture";
static const char* EXTENSION_MESH		= ".mesh";
static const char* EXTENSION_AUDIO      = ".audio";
//====================================================

namespace Spartan
{
	class SPARTAN_CLASS FileSystem
	{
	public:
		static void Initialize();

        // Strings
        static bool IsEmptyOrWhitespace(const std::string& var);
        static bool IsAlphanumeric(const std::string& var);
        static std::string GetStringBeforeExpression(const std::string& str, const std::string& exp);
        static std::string GetStringAfterExpression(const std::string& str, const std::string& exp);
        static std::string GetStringBetweenExpressions(const std::string& str, const std::string& exp_a, const std::string& exp_b);
        static std::string ConvertToUppercase(const std::string& lower);
        static std::string ReplaceExpression(const std::string& str, const std::string& from, const std::string& to);
        static std::wstring StringToWstring(const std::string& str);
        static std::vector<std::string> GetIncludedFiles(const std::string& path);

        // Paths
        static void OpenDirectoryWindow(const std::string& path);
		static bool CreateDirectory_(const std::string& path);
		static bool Delete(const std::string& path);	
		static bool Exists(const std::string& path);
        static bool IsDirectory(const std::string& path);
        static bool IsFile(const std::string& path);
		static bool CopyFileFromTo(const std::string& source, const std::string& destination);
		static std::string GetFileNameFromFilePath(const std::string& path);
		static std::string GetFileNameNoExtensionFromFilePath(const std::string& path);
		static std::string GetDirectoryFromFilePath(const std::string& path);
		static std::string GetFilePathWithoutExtension(const std::string& path);
		static std::string GetExtensionFromFilePath(const std::string& path);
        static std::string NativizeFilePath(const std::string& path);
		static std::string GetRelativePath(const std::string& path);
		static std::string GetWorkingDirectory();	
        static std::string GetRootDirectory(const std::string& path);
        static std::string GetParentDirectory(const std::string& path);
		static std::vector<std::string> GetDirectoriesInDirectory(const std::string& path);
		static std::vector<std::string> GetFilesInDirectory(const std::string& path);

        // Supported files
		static bool IsSupportedAudioFile(const std::string& path);
		static bool IsSupportedImageFile(const std::string& path);
		static bool IsSupportedModelFile(const std::string& path);
		static bool IsSupportedShaderFile(const std::string& path);
		static bool IsSupportedFontFile(const std::string& path);
		static bool IsEngineScriptFile(const std::string& path);
		static bool IsEnginePrefabFile(const std::string& path);
		static bool IsEngineMaterialFile(const std::string& path);
		static bool IsEngineMeshFile(const std::string& path);
		static bool IsEngineModelFile(const std::string& path);
		static bool IsEngineSceneFile(const std::string& path);
		static bool IsEngineTextureFile(const std::string& path);
        static bool IsEngineAudioFile(const std::string& path);
		static bool IsEngineShaderFile(const std::string& path);
        static bool IsEngineFile(const std::string& path);

        // Supported files in directory
        static std::vector<std::string> GetSupportedFilesInDirectory(const std::string& path);
        static std::vector<std::string> GetSupportedImageFilesFromPaths(const std::vector<std::string>& paths);
        static std::vector<std::string> GetSupportedAudioFilesFromPaths(const std::vector<std::string>& paths);
        static std::vector<std::string> GetSupportedScriptFilesFromPaths(const std::vector<std::string>& paths);
        static std::vector<std::string> GetSupportedModelFilesFromPaths(const std::vector<std::string>& paths);
        static std::vector<std::string> GetSupportedModelFilesInDirectory(const std::string& path);
        static std::vector<std::string> GetSupportedSceneFilesInDirectory(const std::string& path);

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
