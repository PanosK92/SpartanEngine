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

class __declspec(dllexport) FileSystem
{
public:
	static bool FileExists(const std::string& path);
	static void CreateFolder(const std::string& path);
	static void CopyFileFromTo(const std::string& source, const std::string& destination);

	static std::string GetFileNameFromPath(std::string path);
	static std::string GetFileNameNoExtensionFromPath(std::string path);
	static std::string GetPathWithoutFileName(std::string path);
	static std::string GetExtensionFromPath(std::string path);
	static std::string GetRelativePathFromAbsolutePath(std::string path);

	static std::vector<std::string> FileSystem::GetSupportedImageFormats(bool includeUppercase);

	static std::vector<std::string> GetFoldersInDirectory(std::string directory);
	static std::vector<std::string> GetFilesInDirectory(std::string directory);
	static std::vector<std::string> GetSupportedFilesInDirectory(std::string directory);
	static std::vector<std::string> GetImagesFromPaths(std::vector<std::string> paths);
	static std::vector<std::string> GetScriptsFromPaths(std::vector<std::string> paths);
	static std::vector<std::string> GetModelsFromPaths(std::vector<std::string> paths);

	static bool IsSupportedImage(std::string path);
	static bool IsSupportedScript(std::string path);
	static bool IsSupportedScene(std::string path);
	static bool IsSupportedModel(std::string path);
	static bool IsSupportedShader(std::string path);

	// string
	static std::string ConvertToUppercase(std::string);
};
