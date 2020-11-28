/*
Copyright(c) 2016-2020 Panos Karabelas

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
#include "../ProgressReport.h"
#include "../../Math/Vector2.h"
#include "../../Math/Vector3.h"
#include "../../Math/Matrix.h"
#include "../../World/Entity.h"
#include "../../World/Components/Transform.h"
#include "../../Core/FileSystem.h"
#include "../../Logging/Log.h"
//===========================================

namespace Spartan::AssimpHelper
{
    inline Math::Matrix ai_matrix4_x4_to_matrix(const aiMatrix4x4& transform)
    {
        return Math::Matrix
        (
            transform.a1, transform.b1, transform.c1, transform.d1,
            transform.a2, transform.b2, transform.c2, transform.d2,
            transform.a3, transform.b3, transform.c3, transform.d3,
            transform.a4, transform.b4, transform.c4, transform.d4
        );
    }

    inline void set_entity_transform(const aiNode* node, Entity* entity)
    {
        if (!entity)
            return;

        // Convert to engine matrix
        const auto matrix_engine = ai_matrix4_x4_to_matrix(node->mTransformation);

        // Apply position, rotation and scale
        entity->GetTransform()->SetPositionLocal(matrix_engine.GetTranslation());
        entity->GetTransform()->SetRotationLocal(matrix_engine.GetRotation());
        entity->GetTransform()->SetScaleLocal(matrix_engine.GetScale());
    }

    constexpr void compute_node_count(const aiNode* node, int* count)
    {
        if (!node)
            return;

        (*count)++;

        // Process children
        for (uint32_t i = 0; i < node->mNumChildren; i++)
        {
            compute_node_count(node->mChildren[i], count);
        }
    }

    inline Math::Vector4 to_vector4(const aiColor4D& ai_color)
    {
        return Math::Vector4(ai_color.r, ai_color.g, ai_color.b, ai_color.a);
    }

    inline Math::Vector3 to_vector3(const aiVector3D& ai_vector)
    {
        return Math::Vector3(ai_vector.x, ai_vector.y, ai_vector.z);
    }

    inline Math::Vector2 to_vector2(const aiVector2D& ai_vector)
    {
        return Math::Vector2(ai_vector.x, ai_vector.y);
    }

    inline Math::Quaternion to_quaternion(const aiQuaternion& ai_quaternion)
    {
        return Math::Quaternion(ai_quaternion.x, ai_quaternion.y, ai_quaternion.z, ai_quaternion.w);
    }

    // Implement Assimp:Logger
    class AssimpLogger : public Assimp::Logger
    {
    public:
        bool attachStream(Assimp::LogStream* pStream, uint32_t severity) override { return true; }
        bool detatchStream(Assimp::LogStream* pStream, uint32_t severity) override { return true; }

    private:
        void OnDebug(const char* message) override
        {
#ifdef DEBUG
            LOG_INFO(message, LogType::Info);
#endif
        }

        void OnInfo(const char* message) override
        {
            LOG_INFO(message, LogType::Info);
        }

        void OnWarn(const char* message) override
        {
            LOG_WARNING(message, LogType::Warning);
        }

        void OnError(const char* message) override
        {
            LOG_ERROR(message, LogType::Error);
        }
    };

    // Implement Assimp::ProgressHandler
    class AssimpProgress : public Assimp::ProgressHandler
    {
    public:
        AssimpProgress(const std::string& file_path)
        {
            m_file_path = file_path;
            m_file_name = FileSystem::GetFileNameFromFilePath(file_path);

            // Start progress tracking
            auto& progress = ProgressReport::Get();
            progress.Reset(g_progress_model_importer);
            progress.SetIsLoading(g_progress_model_importer, true);
        }

        ~AssimpProgress()
        {
            ProgressReport::Get().SetIsLoading(g_progress_model_importer, false);
        }

        bool Update(float percentage) override { return true; }

        void UpdateFileRead(int current_step, int number_of_steps) override
        {
            auto& progress = ProgressReport::Get();
            progress.SetStatus(g_progress_model_importer, "Loading \"" + m_file_name + "\" from disk...");
            progress.SetJobsDone(g_progress_model_importer, current_step);
            progress.SetJobCount(g_progress_model_importer, number_of_steps);
        }

        void UpdatePostProcess(int current_step, int number_of_steps) override
        {
            auto& progress = ProgressReport::Get();
            progress.SetStatus(g_progress_model_importer, "Post-Processing \"" + m_file_name + "\"");
            progress.SetJobsDone(g_progress_model_importer, current_step);
            progress.SetJobCount(g_progress_model_importer, number_of_steps);
        }

    private:
        std::string m_file_path;
        std::string m_file_name;
    };

    inline std::string texture_try_multiple_extensions(const std::string& file_path)
    {
        // Remove extension
        const auto file_path_no_ext = FileSystem::GetFilePathWithoutExtension(file_path);

        // Check if the file exists using all engine supported extensions
        for (const auto& supported_format : supported_formats_image)
        {
            auto new_file_path            = file_path_no_ext + supported_format;
            auto new_file_path_upper    = file_path_no_ext + FileSystem::ConvertToUppercase(supported_format);

            if (FileSystem::Exists(new_file_path))
            {
                return new_file_path;
            }

            if (FileSystem::Exists(new_file_path_upper))
            {
                return new_file_path_upper;
            }
        }

        return file_path;
    }

    inline std::string texture_validate_path(std::string original_texture_path, const std::string& model_path)
    {
        std::replace(original_texture_path.begin(), original_texture_path.end(), '\\', '/');

        // Models usually return a texture path which is relative to the model's directory.
        // However, to load anything, we'll need an absolute path, so we construct it here.
        const auto model_dir    = FileSystem::GetDirectoryFromFilePath(model_path);
        auto full_texture_path = model_dir + original_texture_path;

        // 1. Check if the texture path is valid
        if (FileSystem::Exists(full_texture_path))
            return full_texture_path;

        // 2. Check the same texture path as previously but 
        // this time with different file extensions (jpg, png and so on).
        full_texture_path = texture_try_multiple_extensions(full_texture_path);
        if (FileSystem::Exists(full_texture_path))
            return full_texture_path;

        // At this point we know the provided path is wrong, we will make a few guesses.
        // The most common mistake is that the artist provided a path which is absolute to his computer.

        // 3. Check if the texture is in the same folder as the model
        full_texture_path = model_dir + FileSystem::GetFileNameFromFilePath(full_texture_path);
        if (FileSystem::Exists(full_texture_path))
            return full_texture_path;

        // 4. Check the same texture path as previously but 
        // this time with different file extensions (jpg, png and so on).
        full_texture_path = texture_try_multiple_extensions(full_texture_path);
        if (FileSystem::Exists(full_texture_path))
            return full_texture_path;

        // Give up, no valid texture path was found
        return "";
    }
}
