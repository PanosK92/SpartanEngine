/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===================================
#include "pch.h"
#include "editor/Selection.h"
#include "game/CameraController.h"
#include "../profiling/WorldWork.h"
#include "../core/ThreadPool.h"
#include "McpCommands.h"
#include "McpCommandsCommon.h"
#include "McpCommandsWorldBuild.h"
#include "McpCommandsMesh.h"
#include "McpGeometryKernel.h"
#include "McpTextureKernel.h"
#include "../commands/console/ConsoleCommands.h"
#include "../commands/CommandStack.h"
#include "../core/ProgressTracker.h"
#include "../logging/Log.h"
#include "../physics/PhysicsWorld.h"
#include "../profiling/Profiler.h"
#include "../memory/GpuMemory.h"
#include "../world/World.h"
#include "../world/Weather.h"
#include "../world/Entity.h"
#include "../world/components/Camera.h"
#include "../world/components/Component.h"
#include "../world/components/AudioSource.h"
#include "../world/components/Light.h"
#include "../world/components/ParticleSystem.h"
#include "../world/components/Physics.h"
#include "../world/components/Render.h"
#include "../world/components/Script.h"
#include "../world/components/Spline.h"
#include "../world/components/SplineFollower.h"
#include "../world/components/Terrain.h"
#include "../world/components/Text3D.h"
#include "../world/Prefab.h"
#include "../world/GameReady.h"
#include "../world/WorldHelpers.h"
#include "../io/pugixml.hpp"
#include "../game/components/RaceDriver.h"
#include "../resource/ResourceCache.h"
#include "../resource/import/ImageImporter.h"
#include "../animation/Animation.h"
#include "../geometry/GeometryGeneration.h"
#include "../geometry/Mesh.h"
#include "../rhi/RHI_Texture.h"
#include "../rhi/RHI_Buffer.h"
#include "../rhi/RHI_Device.h"
#include "../rhi/RHI_Shader.h"
#include "../rendering/Material.h"
#include "../rendering/Renderer.h"
#include "../math/Vector2.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <limits>
#include <optional>
#include <sstream>
#include <typeinfo>
#include <unordered_map>
//==============================================

#include "McpCommandsDiagnostics.h"

namespace spartan::mcp_diagnostics
{
    using namespace mcp_common;
        std::string cvar_type(const CVarVariant& value)
        {
            return std::visit([]<typename T>(const T&) -> std::string
            {
                if constexpr (std::is_same_v<T, int32_t>)
                {
                    return "int";
                }
                else if constexpr (std::is_same_v<T, float>)
                {
                    return "float";
                }
                else if constexpr (std::is_same_v<T, bool>)
                {
                    return "bool";
                }
                else
                {
                    return "string";
                }
            }, value);
        }

        std::string log_type_to_string(LogType type)
        {
            if (type == LogType::Warning)
            {
                return "warning";
            }
            if (type == LogType::Error)
            {
                return "error";
            }

            return "info";
        }

        std::optional<LogType> log_type_from_name(const std::string& name)
        {
            if (name == "info")
            {
                return LogType::Info;
            }
            if (name == "warning")
            {
                return LogType::Warning;
            }
            if (name == "error")
            {
                return LogType::Error;
            }

            return std::nullopt;
        }

        bool log_type_passes_filter(LogType type, LogType minimum_type)
        {
            return static_cast<uint32_t>(type) >= static_cast<uint32_t>(minimum_type);
        }

        bool is_blocked_cvar(const std::string& name)
        {
            static const std::set<std::string> blocked_cvars =
            {
                "r.hdr"
            };

            return blocked_cvars.contains(name);
        }

        std::optional<std::string> renderer_debug_cvar_from_name(const std::string& name)
        {
            if (name == "aabb")
            {
                return "r.aabb";
            }
            if (name == "volumes")
            {
                return "r.volumes";
            }
            if (name == "picking_ray")
            {
                return "r.picking_ray";
            }
            if (name == "grid")
            {
                return "r.grid";
            }
            if (name == "transform_handle")
            {
                return "r.transform_handle";
            }
            if (name == "selection_outline")
            {
                return "r.selection_outline";
            }
            if (name == "entity_icons")
            {
                return "r.entity_icons";
            }
            if (name == "performance_metrics")
            {
                return "r.performance_metrics";
            }
            if (name == "physics")
            {
                return "r.physics";
            }
            if (name == "ragdoll")
            {
                return "r.ragdoll";
            }
            if (name == "wireframe")
            {
                return "r.wireframe";
            }
            if (name == "meshlet_visualize")
            {
                return "r.meshlet_visualize";
            }
            if (name == "cluster_visualize")
            {
                return "r.cluster_visualize";
            }

            return std::nullopt;
        }

        std::string renderer_debug_options_json()
        {
            return "[\"aabb\",\"volumes\",\"picking_ray\",\"grid\",\"transform_handle\",\"selection_outline\",\"entity_icons\",\"performance_metrics\",\"physics\",\"ragdoll\",\"wireframe\",\"meshlet_visualize\",\"cluster_visualize\"]";
        }

        const char* queue_type_to_name(RHI_Queue_Type type)
        {
            switch (type)
            {
            case RHI_Queue_Type::Graphics:
                return "graphics";
            case RHI_Queue_Type::Compute:
                return "compute";
            case RHI_Queue_Type::Copy:
                return "copy";
            case RHI_Queue_Type::Present:
                return "present";
            default:
                return "none";
            }
        }

        std::string command_profiler_record(const McpRequest& request)
        {
            const std::string action = get_argument(request, "action").value_or("status");
            if (action == "start")
            {
                if (!Profiler::StartRecording()) return json_error("profiler is already recording or stopping");
            }
            else if (action == "stop")
            {
                Profiler::StopRecording();
            }
            else if (action != "status")
            {
                return json_error("action must be start, stop or status");
            }
            return "{\"ok\":true,\"recording\":" + json_bool(Profiler::IsRecording()) +
                ",\"stopping\":" + json_bool(Profiler::IsRecordingStopping()) + "}";
        }

        std::string command_meshlet_snapshot(const McpRequest&)
        {
            // Explicit diagnostic only: never stall the GPU in normal rendering
            // or contaminate an active timing capture with a synchronous read.
            if (Profiler::IsRecording() || Profiler::IsRecordingStopping())
                return json_error("stop profiler recording before reading meshlet counts");
            RHI_Buffer* instances = Renderer::GetBuffer(Renderer_Buffer::InstanceDispatchArgs);
            RHI_Buffer* meshlets = Renderer::GetBuffer(Renderer_Buffer::TriangleDispatchArgs);
            RHI_Buffer* survivors = Renderer::GetBuffer(Renderer_Buffer::SurvivingInstances);
            RHI_Buffer* meshlet_list = Renderer::GetBuffer(Renderer_Buffer::MeshletInstances);
            if (!instances || !meshlets || !survivors || !meshlet_list ||
                !instances->GetMappedData() || !meshlets->GetMappedData())
                return json_error("GPU culling buffers are not ready");
            RHI_Device::QueueWaitAll();
            uint32_t instance_count = 0;
            uint32_t opaque_count = 0;
            uint32_t alpha_count = 0;
            std::memcpy(&instance_count, instances->GetMappedData(), sizeof(uint32_t));
            std::memcpy(&opaque_count, meshlets->GetMappedData(), sizeof(uint32_t));
            std::memcpy(&alpha_count, static_cast<const uint8_t*>(meshlets->GetMappedData()) + meshlets->GetStride(), sizeof(uint32_t));
            return "{\"ok\":true,\"sampled_after_gpu_wait\":true,\"frame\":" + std::to_string(Renderer::GetFrameNumber()) +
                ",\"surviving_instances\":" + std::to_string(instance_count) +
                ",\"opaque_meshlets\":" + std::to_string(opaque_count) +
                ",\"alpha_meshlets\":" + std::to_string(alpha_count) +
                ",\"instance_capacity\":" + std::to_string(survivors->GetElementCount()) +
                ",\"meshlet_capacity_per_category\":" + std::to_string(meshlet_list->GetElementCount() / 2u) + "}";
        }

        std::string command_gpu_memory_snapshot(const McpRequest& request)
        {
            uint32_t top = 40;
            if (const std::optional<std::string> value = get_argument(request, "top"))
            {
                uint64_t parsed = 0;
                if (parse_uint64(*value, parsed) && parsed > 0)
                {
                    top = static_cast<uint32_t>(parsed);
                }
            }

            std::string kind_filter;
            if (const std::optional<std::string> value = get_argument(request, "kind"))
            {
                kind_filter = to_lower_copy(*value);
            }

            std::vector<GpuMemoryBlock> blocks;
            GpuMemory::GetBlocks(blocks);

            std::array<uint64_t, static_cast<size_t>(GpuMemoryKind::Count)> kind_bytes = {};
            std::array<uint32_t, static_cast<size_t>(GpuMemoryKind::Count)> kind_count = {};
            uint64_t tracked_bytes   = 0;
            uint64_t dedicated_bytes = 0;
            std::unordered_map<uint64_t, uint64_t> heap_sizes;
            for (const GpuMemoryBlock& block : blocks)
            {
                kind_bytes[static_cast<size_t>(block.kind)] += block.size;
                kind_count[static_cast<size_t>(block.kind)]++;
                tracked_bytes += block.size;
                if (block.heap_id == 0)
                {
                    dedicated_bytes += block.size;
                }
                else
                {
                    heap_sizes[block.heap_id] = block.heap_size;
                }
            }
            uint64_t pooled_heap_bytes = 0;
            for (const auto& [heap_id, heap_size] : heap_sizes)
            {
                pooled_heap_bytes += heap_size;
            }

            // the filter only narrows the groups and the allocation list, the totals always cover everything
            std::vector<const GpuMemoryBlock*> filtered;
            filtered.reserve(blocks.size());
            for (const GpuMemoryBlock& block : blocks)
            {
                if (kind_filter.empty() || to_lower_copy(GpuMemory::GetKindName(block.kind)) == kind_filter)
                {
                    filtered.push_back(&block);
                }
            }

            struct group
            {
                std::string name;
                const char* kind = nullptr;
                uint32_t count   = 0;
                uint64_t bytes   = 0;
            };
            std::unordered_map<std::string, group> groups;
            for (const GpuMemoryBlock* block : filtered)
            {
                const char* name = block->name[0] ? block->name : "(unnamed)";
                const char* kind = GpuMemory::GetKindName(block->kind);
                group& entry = groups[std::string(name) + '\t' + kind];
                if (entry.count == 0)
                {
                    entry.name = name;
                    entry.kind = kind;
                }
                entry.count++;
                entry.bytes += block->size;
            }
            std::vector<const group*> sorted_groups;
            sorted_groups.reserve(groups.size());
            for (const auto& [key, entry] : groups)
            {
                sorted_groups.push_back(&entry);
            }
            std::sort(sorted_groups.begin(), sorted_groups.end(), [](const group* a, const group* b) { return a->bytes > b->bytes; });
            std::sort(filtered.begin(), filtered.end(), [](const GpuMemoryBlock* a, const GpuMemoryBlock* b) { return a->size > b->size; });

            auto to_mb = [](uint64_t bytes) { return json_number(static_cast<double>(bytes) / (1024.0 * 1024.0)); };

            std::string json = "{\"ok\":true";
            json += ",\"device_local_usage_mb\":" + std::to_string(RHI_Device::MemoryGetAllocatedMb());
            json += ",\"device_local_budget_mb\":" + std::to_string(RHI_Device::MemoryGetAvailableMb());
            json += ",\"device_local_total_mb\":" + std::to_string(RHI_Device::MemoryGetTotalMb());
            json += ",\"tracked_mb\":" + to_mb(tracked_bytes);
            json += ",\"dedicated_allocations_mb\":" + to_mb(dedicated_bytes);
            json += ",\"pooled_heaps_mb\":" + to_mb(pooled_heap_bytes);
            json += ",\"pooled_heap_count\":" + std::to_string(heap_sizes.size());
            json += ",\"allocation_count\":" + std::to_string(blocks.size());

            json += ",\"by_kind\":[";
            bool first = true;
            for (uint8_t i = 0; i < static_cast<uint8_t>(GpuMemoryKind::Count); i++)
            {
                if (kind_count[i] == 0)
                {
                    continue;
                }
                json += first ? "" : ",";
                first = false;
                json += "{\"kind\":" + json_string(GpuMemory::GetKindName(static_cast<GpuMemoryKind>(i)));
                json += ",\"count\":" + std::to_string(kind_count[i]);
                json += ",\"mb\":" + to_mb(kind_bytes[i]) + "}";
            }
            json += "]";

            json += ",\"by_name\":[";
            for (size_t i = 0; i < sorted_groups.size() && i < top; i++)
            {
                const group* entry = sorted_groups[i];
                json += i == 0 ? "" : ",";
                json += "{\"name\":" + json_string(entry->name);
                json += ",\"kind\":" + json_string(entry->kind);
                json += ",\"count\":" + std::to_string(entry->count);
                json += ",\"mb\":" + to_mb(entry->bytes) + "}";
            }
            json += "]";

            json += ",\"largest\":[";
            for (size_t i = 0; i < filtered.size() && i < top; i++)
            {
                const GpuMemoryBlock* block = filtered[i];
                json += i == 0 ? "" : ",";
                json += "{\"name\":" + json_string(block->name[0] ? block->name : "(unnamed)");
                json += ",\"kind\":" + json_string(GpuMemory::GetKindName(block->kind));
                json += ",\"mb\":" + to_mb(block->size);
                json += ",\"dedicated\":" + json_bool(block->heap_id == 0);
                if (block->width > 0)
                {
                    json += ",\"size\":[" + std::to_string(block->width) + "," + std::to_string(block->height) + "," + std::to_string(block->depth) + "]";
                    json += ",\"mips\":" + std::to_string(block->mip_count);
                }
                if (block->format[0])
                {
                    json += ",\"format\":" + json_string(block->format);
                }
                json += "}";
            }
            json += "]}";
            return json;
        }

        std::string command_profiler_snapshot(const McpRequest& request)
        {
            // optional filters, type is cpu, gpu or all, sort is duration or timeline
            std::string type_filter = "all";
            if (const std::optional<std::string> value = get_argument(request, "type"))
            {
                type_filter = to_lower_copy(*value);
            }

            bool sort_by_duration = true;
            if (const std::optional<std::string> value = get_argument(request, "sort"))
            {
                sort_by_duration = to_lower_copy(*value) != "timeline";
            }

            uint32_t top = 0;
            if (const std::optional<std::string> value = get_argument(request, "top"))
            {
                uint64_t parsed = 0;
                if (parse_uint64(*value, parsed))
                {
                    top = static_cast<uint32_t>(parsed);
                }
            }

            std::vector<const TimeBlock*> blocks;
            for (const TimeBlock& block : Profiler::GetTimeBlocks())
            {
                if (!block.IsComplete())
                {
                    continue;
                }

                const bool is_cpu = block.GetType() == TimeBlockType::Cpu;
                if (type_filter == "cpu" && !is_cpu)
                {
                    continue;
                }
                if (type_filter == "gpu" && is_cpu)
                {
                    continue;
                }

                blocks.push_back(&block);
            }

            if (sort_by_duration)
            {
                std::sort(blocks.begin(), blocks.end(), [](const TimeBlock* a, const TimeBlock* b)
                {
                    return a->GetDuration() > b->GetDuration();
                });
            }

            std::string json = "{\"ok\":true";
            json += ",\"fps\":" + std::to_string(Profiler::GetFps());
            json += ",\"frame_ms\":" + std::to_string(Profiler::GetFrameDurationMs());
            json += ",\"cpu_ms\":" + std::to_string(Profiler::GetTimeCpuLast());
            json += ",\"gpu_ms\":" + std::to_string(Profiler::GetTimeGpuLast());
            json += ",\"frame_ms_last\":" + std::to_string(Profiler::GetTimeFrameLast());
            json += ",\"cpu_stuttering\":" + json_bool(Profiler::IsCpuStuttering());
            json += ",\"gpu_stuttering\":" + json_bool(Profiler::IsGpuStuttering());
            json += ",\"update_interval_sec\":" + std::to_string(Profiler::GetUpdateInterval());
            json += ",\"visualized\":" + json_bool(Profiler::IsVisualized());

            json += ",\"rhi\":{";
            json += "\"draw_calls\":" + std::to_string(Profiler::m_rhi_draw);
            json += ",\"instance_count\":" + std::to_string(Profiler::m_rhi_instance_count);
            json += ",\"timeblock_count\":" + std::to_string(Profiler::m_rhi_timeblock_count);
            json += ",\"pipeline_barriers\":" + std::to_string(Profiler::m_rhi_pipeline_barriers);
            json += ",\"pipeline_bindings\":" + std::to_string(Profiler::m_rhi_bindings_pipeline);
            json += ",\"descriptor_set_count\":" + std::to_string(Profiler::m_rhi_descriptor_set_count);
            json += "}";

            const size_t limit = (top > 0 && top < blocks.size()) ? top : blocks.size();
            json += ",\"time_block_count\":" + std::to_string(blocks.size());
            json += ",\"time_blocks\":[";
            for (size_t i = 0; i < limit; i++)
            {
                const TimeBlock* block = blocks[i];
                if (i != 0)
                {
                    json += ",";
                }

                json += "{\"name\":" + json_string(block->GetName() ? block->GetName() : "");
                json += ",\"type\":" + json_string(block->GetType() == TimeBlockType::Cpu ? "cpu" : "gpu");
                json += ",\"queue\":" + json_string(queue_type_to_name(block->GetQueueType()));
                json += ",\"duration_ms\":" + std::to_string(block->GetDuration());
                json += ",\"start_ms\":" + std::to_string(block->GetStartMs());
                json += ",\"end_ms\":" + std::to_string(block->GetEndMs());
                json += ",\"tree_depth\":" + std::to_string(block->GetTreeDepth());
                json += "}";
            }
            json += "]";
            json += "}";
            return json;
        }

        std::string command_cvar_list()
        {
            std::string json = "{\"ok\":true,\"cvars\":[";
            bool first = true;
            for (const auto& [name, cvar] : ConsoleRegistry::Get().GetAll())
            {
                if (!first)
                {
                    json += ",";
                }
                first = false;

                std::optional<std::string> value = ConsoleRegistry::Get().GetValueAsString(name);
                json += "{";
                json += "\"name\":" + json_string(std::string(name));
                json += ",\"type\":" + json_string(cvar_type(*cvar.m_value_ptr));
                json += ",\"hint\":" + json_string(std::string(cvar.m_hint));
                json += ",\"value\":" + json_string(value.value_or(""));
                json += "}";
            }
            json += "],\"resource_cleanup_failures\":[";
            first = true;
            for (
                const std::string& file :
                World::GetLastResourceCleanupFailures()
            )
            {
                if (!first)
                {
                    json += ",";
                }
                first = false;
                json += json_string(file);
            }
            json += "]}";
            return json;
        }

        std::string command_cvar_get(const McpRequest& request)
        {
            const std::optional<std::string> name = get_argument(request, "name");
            if (!name)
            {
                return json_error("missing name");
            }

            ConsoleVariable* cvar = ConsoleRegistry::Get().Find(*name);
            if (cvar == nullptr)
            {
                return json_error("cvar not found");
            }

            std::optional<std::string> value = ConsoleRegistry::Get().GetValueAsString(*name);
            if (!value)
            {
                return json_error("cvar value is unsupported");
            }

            const std::vector<std::string>& failures =
                World::GetLastResourceCleanupFailures();
            std::string json = "{\"ok\":" +
                json_bool(failures.empty());
            json += ",\"name\":" + json_string(*name);
            json += ",\"type\":" + json_string(cvar_type(*cvar->m_value_ptr));
            json += ",\"hint\":" + json_string(std::string(cvar->m_hint));
            json += ",\"value\":" + json_string(*value);
            json += "}";
            return json;
        }

        std::string command_cvar_set(const McpRequest& request)
        {
            const std::optional<std::string> name  = get_argument(request, "name");
            const std::optional<std::string> value = get_argument(request, "value");
            if (!name || !value)
            {
                return json_error("missing name or value");
            }

            if (is_blocked_cvar(*name))
            {
                return json_error("cvar is blocked by MCP");
            }

            if (ConsoleRegistry::Get().Find(*name) == nullptr)
            {
                return json_error("cvar not found");
            }

            if (!ConsoleRegistry::Get().SetValueFromString(*name, *value))
            {
                return json_error("failed to set cvar");
            }

            return command_cvar_get(request);
        }

        std::string shader_stage_name(RHI_Shader_Type stage)
        {
            switch (stage)
            {
                case RHI_Shader_Type::Vertex:  return "vertex";
                case RHI_Shader_Type::Hull:    return "hull";
                case RHI_Shader_Type::Domain:  return "domain";
                case RHI_Shader_Type::Pixel:   return "pixel";
                case RHI_Shader_Type::Compute: return "compute";
                default:                       return "other";
            }
        }

        std::string command_shader_reload(const McpRequest& request)
        {
            const std::string filter = to_lower_copy(get_argument(request, "name").value_or(""));
            bool force = false;
            if (const std::optional<std::string> force_arg = get_argument(request, "force"))
            {
                if (!parse_bool(*force_arg, force))
                {
                    return json_error("force must be true or false");
                }
            }
            if (force && filter.empty())
            {
                return json_error("force needs a name filter, recompiling every shader at once stalls the engine for minutes");
            }

            std::unordered_map<std::string, std::string> disk_sources;
            auto read_disk = [&disk_sources](const std::string& path) -> const std::string&
            {
                auto it = disk_sources.find(path);
                if (it == disk_sources.end())
                {
                    std::ifstream in(path);
                    std::stringstream content;
                    content << in.rdbuf();
                    it = disk_sources.emplace(path, content.str()).first;
                }
                return it->second;
            };

            std::string recompiled = "[";
            std::string failed     = "[";
            uint32_t recompiled_count = 0;
            uint32_t compiling_count  = 0;
            uint32_t failed_count     = 0;
            uint32_t matched_count    = 0;
            for (const std::shared_ptr<RHI_Shader>& shader : Renderer::GetShaders())
            {
                if (!shader || shader->GetFilePath().empty())
                {
                    continue;
                }

                const std::vector<std::string>& file_paths = shader->GetFilePaths();
                if (!filter.empty())
                {
                    bool matches = false;
                    for (const std::string& file_path : file_paths)
                    {
                        if (to_lower_copy(FileSystem::GetFileNameFromFilePath(file_path)).find(filter) != std::string::npos)
                        {
                            matches = true;
                            break;
                        }
                    }
                    if (!matches)
                    {
                        continue;
                    }
                }
                matched_count++;

                const RHI_ShaderCompilationState state = shader->GetCompilationState();
                if (state == RHI_ShaderCompilationState::Compiling || shader->IsReloading())
                {
                    compiling_count++;
                    continue;
                }

                bool changed = force;
                const std::vector<std::string>& sources = shader->GetSources();
                for (size_t i = 0; !changed && i < file_paths.size() && i < sources.size(); i++)
                {
                    changed = read_disk(file_paths[i]) != sources[i];
                }

                if (!changed)
                {
                    if (state == RHI_ShaderCompilationState::Failed || shader->ReloadFailed())
                    {
                        if (failed_count++ != 0)
                        {
                            failed += ",";
                        }
                        failed += json_string(shader->GetObjectName());
                    }
                    continue;
                }

                shader->Compile(shader->GetShaderStage(), shader->GetFilePath(), true, shader->GetVertexType());
                if (recompiled_count++ != 0)
                {
                    recompiled += ",";
                }
                recompiled += "{\"name\":" + json_string(shader->GetObjectName());
                recompiled += ",\"stage\":" + json_string(shader_stage_name(shader->GetShaderStage())) + "}";
                compiling_count++;
            }
            recompiled += "]";
            failed     += "]";

            std::string json = "{\"ok\":true";
            json += ",\"shader_directory\":" + json_string(ResourceCache::GetResourceDirectory(ResourceDirectory::Shaders));
            json += ",\"matched\":" + std::to_string(matched_count);
            json += ",\"recompiled_count\":" + std::to_string(recompiled_count);
            json += ",\"recompiled\":" + recompiled;
            json += ",\"compiling\":" + std::to_string(compiling_count);
            json += ",\"failed\":" + failed;
            json += ",\"note\":" + json_string(compiling_count != 0 ?
                "compiling in the background, call shader_reload again until compiling is 0, then check failed and console_read for compiler errors" :
                "nothing is compiling");
            json += "}";
            return json;
        }

        std::string command_console_read(const McpRequest& request)
        {
            uint32_t limit = 100;
            if (const std::optional<std::string> limit_arg = get_argument(request, "limit"))
            {
                uint64_t parsed = 0;
                if (!parse_uint64(*limit_arg, parsed) || parsed == 0 || parsed > 500)
                {
                    return json_error("limit must be between 1 and 500");
                }

                limit = static_cast<uint32_t>(parsed);
            }

            LogType minimum_type = LogType::Info;
            if (const std::optional<std::string> minimum_type_arg = get_argument(request, "minimum_type"))
            {
                const std::optional<LogType> parsed = log_type_from_name(*minimum_type_arg);
                if (!parsed)
                {
                    return json_error("minimum_type must be info, warning, or error");
                }

                minimum_type = *parsed;
            }

            std::vector<LogCmd> entries = Log::GetRecentEntries(500);
            std::vector<LogCmd> filtered_entries;
            filtered_entries.reserve(limit);
            for (auto it = entries.rbegin(); it != entries.rend(); ++it)
            {
                if (!log_type_passes_filter(it->type, minimum_type))
                {
                    continue;
                }

                filtered_entries.emplace_back(*it);
                if (filtered_entries.size() >= limit)
                {
                    break;
                }
            }
            std::reverse(filtered_entries.begin(), filtered_entries.end());

            std::string json = "{\"ok\":true,\"entries\":[";
            bool first = true;
            for (const LogCmd& entry : filtered_entries)
            {
                if (!first)
                {
                    json += ",";
                }
                first = false;

                json += "{";
                json += "\"type\":" + json_string(log_type_to_string(entry.type));
                json += ",\"text\":" + json_string(entry.text);
                json += "}";
            }
            json += "]}";
            return json;
        }

        std::string command_renderer_debug_get()
        {
            std::string json = "{\"ok\":true,\"options\":" + renderer_debug_options_json() + ",\"values\":{";
            bool first = true;
            const std::vector<std::string> options =
            {
                "aabb", "volumes", "picking_ray", "grid", "transform_handle", "selection_outline", "entity_icons", "performance_metrics", "physics", "ragdoll", "wireframe", "meshlet_visualize", "cluster_visualize"
            };

            for (const std::string& option : options)
            {
                const std::optional<std::string> cvar = renderer_debug_cvar_from_name(option);
                if (!cvar)
                {
                    continue;
                }
                const std::optional<std::string> value = ConsoleRegistry::Get().GetValueAsString(*cvar);
                if (!value)
                {
                    continue;
                }
                if (!first)
                {
                    json += ",";
                }
                first = false;
                json += json_string(option) + ":" + json_string(*value);
            }

            json += "}}";
            return json;
        }

        std::string command_renderer_debug_set(const McpRequest& request)
        {
            const std::optional<std::string> option_arg = get_argument(request, "option");
            const std::optional<std::string> value_arg = get_argument(request, "value");
            if (!option_arg || !value_arg)
            {
                return json_error("missing option or value");
            }

            const std::optional<std::string> cvar = renderer_debug_cvar_from_name(to_lower_copy(*option_arg));
            if (!cvar)
            {
                return json_error("unknown renderer debug option");
            }

            std::string value = to_lower_copy(*value_arg);
            if (value == "true")
            {
                value = "1";
            }
            else if (value == "false")
            {
                value = "0";
            }

            if (!ConsoleRegistry::Get().SetValueFromString(*cvar, value))
            {
                return json_error("failed to set renderer debug option");
            }

            return command_renderer_debug_get();
        }
    void Register()
    {
        RegisterMcpCommand("profiler_snapshot", command_profiler_snapshot);
        RegisterMcpCommand("gpu_memory_snapshot", command_gpu_memory_snapshot);
        RegisterMcpCommand("profiler_record", command_profiler_record);
        RegisterMcpCommand("meshlet_snapshot", command_meshlet_snapshot);
        RegisterMcpCommand("cvar_list", [](const McpRequest&) { return command_cvar_list(); });
        RegisterMcpCommand("cvar_get", command_cvar_get);
        RegisterMcpCommand("cvar_set", command_cvar_set);
        RegisterMcpCommand("shader_reload", command_shader_reload);
        RegisterMcpCommand("console_read", command_console_read);
        RegisterMcpCommand("renderer_debug_get", [](const McpRequest&) { return command_renderer_debug_get(); });
        RegisterMcpCommand("renderer_debug_set", command_renderer_debug_set);
    }
}
