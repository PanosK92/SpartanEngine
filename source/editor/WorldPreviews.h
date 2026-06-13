#pragma once

#include <string>

namespace spartan
{
    class RHI_Texture;
}

class WorldPreviews
{
public:
    static void Shutdown();
    static void Tick();

    static void RequestGeneration(const std::string& world_file_path);
    static std::string GetPreviewPath(const std::string& world_file_path);
    static spartan::RHI_Texture* GetTexture(const std::string& world_file_path);
};
