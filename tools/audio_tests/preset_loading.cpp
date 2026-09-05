// Exercise the actual car loader and its validation independently of simulation.
#include <new>
#include "../../source/core/pch.h"
#include "../../source/car/CarPresets.h"
#include "../../source/io/pugixml.hpp"
#include <filesystem>

namespace spartan
{
    void Log::FormatBuffer(char* buffer, const char*, const char* format, ...)
    {
        va_list args; va_start(args, format);
        vsnprintf(buffer, SP_LOG_BUFFER_SIZE, format, args); va_end(args);
    }
    void Log::WriteBuffer(const char* text, LogType type)
    {
        if (type != LogType::Info) fprintf(stderr, "%s\n", text);
    }
    bool FileSystem::Exists(const std::string& path) { return std::filesystem::exists(path); }
    bool FileSystem::IsDirectory(const std::string& path) { return std::filesystem::is_directory(path); }
    std::vector<std::string> FileSystem::GetFilesInDirectory(const std::string& path)
    {
        std::vector<std::string> files;
        for (const auto& entry : std::filesystem::directory_iterator(path))
            if (entry.is_regular_file()) files.push_back(entry.path().string());
        return files;
    }
    std::string FileSystem::GetExtensionFromFilePath(const std::string& path) { return std::filesystem::path(path).extension().string(); }
}

int main()
{
    for (const auto& entry : std::filesystem::directory_iterator("worlds/cars"))
    {
        if (entry.path().extension() != ".car") continue;
        const auto* definition = car::load_car_file(entry.path().string());
        if (!definition) return 1;
        const auto& p = definition->performance;
        if (p.intake_valve_duration_deg < 120 || p.engine_combustion_variation < 0) return 1;
        printf("PASS preset: %s\n", entry.path().filename().string().c_str());
    }
    pugi::xml_document doc;
    if (!doc.load_file("worlds/cars/ferrari_laferrari.car")) return 1;
    auto engine = doc.child("car").child("engine");
    engine.append_attribute("intake_runner_length_m") = .55f;
    engine.append_attribute("intake_valve_duration_deg") = 240;
    engine.append_attribute("engine_combustion_variation") = .05f;
    engine.append_attribute("turbo_bypass_valve") = false;
    engine.append_attribute("engine_firing_intervals_deg") = "50,70,50,70,50,70,50,70,50,70,50,70";
    if (!doc.save_file("binaries/audio_tests/authored.car")) return 1;
    const auto* authored = car::load_car_file("binaries/audio_tests/authored.car");
    if (!authored || fabsf(authored->performance.intake_runner_length_m - .55f) > 1e-6f ||
        authored->performance.intake_valve_duration_deg != 240 || authored->performance.turbo_bypass_valve ||
        fabsf(authored->performance.engine_combustion_variation - .05f) > 1e-6f ||
        authored->performance.engine_firing_intervals_deg[1] != 70) return 1;
    engine.attribute("engine_firing_intervals_deg") = "50,70,50,70,50,70,50,70,50,70,50,60";
    if (!doc.save_file("binaries/audio_tests/invalid_intervals.car")) return 1;
    if (car::load_car_file("binaries/audio_tests/invalid_intervals.car")) return 1;
    puts("PASS authored audio specs and rejection of invalid crank intervals (expected validation error above)");
    return 0;
}
