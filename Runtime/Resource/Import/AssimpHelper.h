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

//= INCLUDES ================================
#include <memory>
#include <assimp/scene.h>
#include <assimp/DefaultLogger.hpp>
#include <assimp/ProgressHandler.hpp>
#include "../../Math/Vector2.h"
#include "../../Math/Vector3.h"
#include "../../Math/Matrix.h"
#include "../../World/Entity.h"
#include "../../World/Components/Transform.h"
#include "../../FileSystem/FileSystem.h"
#include "../ProgressReport.h"
//===========================================

namespace Directus::AssimpHelper
{
	inline Math::Matrix aiMatrix4x4ToMatrix(const aiMatrix4x4& transform)
	{
		return Math::Matrix
		(
			transform.a1, transform.b1, transform.c1, transform.d1,
			transform.a2, transform.b2, transform.c2, transform.d2,
			transform.a3, transform.b3, transform.c3, transform.d3,
			transform.a4, transform.b4, transform.c4, transform.d4
		);
	}

	inline void SetentityTransform(aiNode* node, Entity* entity)
	{
		if (!entity)
			return;

		// Convert to engine matrix
		Math::Matrix mEngine = aiMatrix4x4ToMatrix(node->mTransformation);

		// Apply position, rotation and scale
		entity->GetTransform_PtrRaw()->SetPositionLocal(mEngine.GetTranslation());
		entity->GetTransform_PtrRaw()->SetRotationLocal(mEngine.GetRotation());
		entity->GetTransform_PtrRaw()->SetScaleLocal(mEngine.GetScale());
	}

	inline void ComputeNodeCount(aiNode* node, int* count)
	{
		if (!node)
			return;

		(*count)++;

		// Process children
		for (unsigned int i = 0; i < node->mNumChildren; i++)
		{
			ComputeNodeCount(node->mChildren[i], count);
		}
	}

	inline Math::Vector4 ToVector4(const aiColor4D& aiColor)
	{
		return Math::Vector4(aiColor.r, aiColor.g, aiColor.b, aiColor.a);
	}

	inline Math::Vector3 ToVector3(const aiVector3D& aiVector)
	{
		return Math::Vector3(aiVector.x, aiVector.y, aiVector.z);
	}

	inline Math::Vector2 ToVector2(const aiVector2D& aiVector)
	{
		return Math::Vector2(aiVector.x, aiVector.y);
	}

	inline Math::Quaternion ToQuaternion(const aiQuaternion& aiQuaternion)
	{
		return Math::Quaternion(aiQuaternion.x, aiQuaternion.y, aiQuaternion.z, aiQuaternion.w);
	}

	// Implement Assimp:Logger
	class AssimpLogger : public Assimp::Logger
	{
	public:
		bool attachStream(Assimp::LogStream* pStream, unsigned int severity) override { return true; }
		bool detatchStream(Assimp::LogStream* pStream, unsigned int severity) override { return true; }

	private:
		void OnDebug(const char* message) override
		{
#ifdef DEBUG
			Log::m_callerName = "Directus::ModelImporter";
			Log::Write(message, Log_Type::Log_Info);
#endif
		}

		void OnInfo(const char* message) override
		{
			Log::m_callerName = "Directus::ModelImporter";
			Log::Write(message, Log_Type::Log_Info);
		}

		void OnWarn(const char* message) override
		{
			Log::m_callerName = "Directus::ModelImporter";
			Log::Write(message, Log_Type::Log_Warning);
		}

		void OnError(const char* message) override
		{
			Log::m_callerName = "Directus::ModelImporter";
			Log::Write(message, Log_Type::Log_Error);
		}
	};

	// Implement Assimp::ProgressHandler
	class AssimpProgress : public Assimp::ProgressHandler
	{
	public:
		AssimpProgress(const std::string& filePath)
		{
			m_filePath = filePath;
			m_fileName = FileSystem::GetFileNameFromFilePath(filePath);

			// Start progress tracking
			ProgressReport& progress = ProgressReport::Get();
			progress.Reset(Directus::g_progress_ModelImporter);
			progress.SetIsLoading(Directus::g_progress_ModelImporter, true);
		}

		~AssimpProgress()
		{
			ProgressReport::Get().SetIsLoading(Directus::g_progress_ModelImporter, false);
		}

		bool Update(float percentage) override { return true; }

		void UpdateFileRead(int currentStep, int numberOfSteps) override
		{
			ProgressReport& progress = ProgressReport::Get();
			progress.SetStatus(Directus::g_progress_ModelImporter, "Loading \"" + m_fileName + "\" from disk...");
			progress.SetJobsDone(Directus::g_progress_ModelImporter, currentStep);
			progress.SetJobCount(Directus::g_progress_ModelImporter, numberOfSteps);
		}

		void UpdatePostProcess(int currentStep, int numberOfSteps) override
		{
			ProgressReport& progress = ProgressReport::Get();
			progress.SetStatus(Directus::g_progress_ModelImporter, "Post-Processing \"" + m_fileName + "\"");
			progress.SetJobsDone(Directus::g_progress_ModelImporter, currentStep);
			progress.SetJobCount(Directus::g_progress_ModelImporter, numberOfSteps);
		}

	private:
		std::string m_filePath;
		std::string m_fileName;
	};

	inline std::string Texture_TryMultipleExtensions(const std::string& filePath)
	{
		// Remove extension
		std::string filePathNoExt = FileSystem::GetFilePathWithoutExtension(filePath);

		// Check if the file exists using all engine supported extensions
		auto supportedFormats = FileSystem::GetSupportedImageFormats();
		for (unsigned int i = 0; i < supportedFormats.size(); i++)
		{
			std::string newFilePath			= filePathNoExt + supportedFormats[i];
			std::string newFilePathUpper	= filePathNoExt + FileSystem::ConvertToUppercase(supportedFormats[i]);

			if (FileSystem::FileExists(newFilePath))
			{
				return newFilePath;
			}

			if (FileSystem::FileExists(newFilePathUpper))
			{
				return newFilePathUpper;
			}
		}

		return filePath;
	}

	inline std::string Texture_ValidatePath(const std::string& originalTexturePath, const std::string& modelPath)
	{
		// Models usually return a texture path which is relative to the model's directory.
		// However, to load anything, we'll need an absolute path, so we construct it here.
		std::string modelDir		= FileSystem::GetDirectoryFromFilePath(modelPath);
		std::string fullTexturePath = modelDir + originalTexturePath;

		// 1. Check if the texture path is valid
		if (FileSystem::FileExists(fullTexturePath))
			return fullTexturePath;

		// 2. Check the same texture path as previously but 
		// this time with different file extensions (jpg, png and so on).
		fullTexturePath = Texture_TryMultipleExtensions(fullTexturePath);
		if (FileSystem::FileExists(fullTexturePath))
			return fullTexturePath;

		// At this point we know the provided path is wrong, we will make a few guesses.
		// The most common mistake is that the artist provided a path which is absolute to his computer.

		// 3. Check if the texture is in the same folder as the model
		fullTexturePath = modelDir + FileSystem::GetFileNameFromFilePath(fullTexturePath);
		if (FileSystem::FileExists(fullTexturePath))
			return fullTexturePath;

		// 4. Check the same texture path as previously but 
		// this time with different file extensions (jpg, png and so on).
		fullTexturePath = Texture_TryMultipleExtensions(fullTexturePath);
		if (FileSystem::FileExists(fullTexturePath))
			return fullTexturePath;

		// Give up, no valid texture path was found
		return NOT_ASSIGNED;
	}
}