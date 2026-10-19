/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =================================
#include "pch.h"
#include "Renderer_Internal.h"
#include "../rhi/RHI_CommandList.h"
#include "../rhi/RHI_Device.h"
#include "../rhi/RHI_Texture.h"
#include "../world/World.h"
#include "../world/Entity.h"
#include "../world/components/Camera.h"
#include "../world/components/Terrain.h"
#include "../world/components/Water.h"
#include "../world/TerrainDeformation.h"
#include "../core/Timer.h"
//============================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    bool Renderer::IsTerrainDeformationReady()
    {
        const RHI_Texture* texture = m_pass_state.terrain_deformation.texture.get();
        return texture && texture->GetResourceState() == ResourceState::PreparedForGpu && texture->GetRhiSrv();
    }

    void Renderer::UpdateFrameCb_TerrainDeformation()
    {
        auto& state = m_pass_state.terrain_deformation;

        // the field belongs to the primary view, a preview keeps the last mapping
        if (!IsSecondaryViewActive())
        {
            // nothing persists, a new play session or switching the feature off erases every rut, the ruts of the
            // last session stay around the camera after stopping so they can be inspected with the editor camera
            const bool simulating = Engine::IsFlagSet(EngineMode::Playing) || Engine::IsFlagSet(EngineMode::Paused);
            if ((simulating && !state.was_simulating) || !TerrainDeformation::IsEnabled())
            {
                TerrainDeformation::Clear();
            }
            state.was_simulating = simulating;

            if (Camera* camera = World::GetCamera())
            {
                TerrainDeformation::SetFocus(camera->GetEntity()->GetPosition());
            }

            TerrainDeformationSand sand;
            if (Terrain* terrain = Terrain::FindActive())
            {
                for (const TerrainLayerRule& rule : terrain->GetLayerRules())
                {
                    if (rule.sand)
                    {
                        const bool shore = (rule.flags & TerrainLayerFlags_BelowSea) != 0;
                        sand.sea_level   = shore ? (m_pass_state.ocean ? m_pass_state.ocean->GetSeaLevel() : terrain->GetSeaLevel()) : 0.0f;
                        sand.height_min  = rule.height_min;
                        sand.height_max  = rule.height_max;
                        sand.slope_min   = rule.slope_min;
                        sand.slope_max   = rule.slope_max;
                        sand.valid       = true;
                        break;
                    }
                }
            }
            TerrainDeformation::SetSand(sand);

            if (simulating && !Engine::IsFlagSet(EngineMode::Paused))
            {
                TerrainDeformation::Wash(static_cast<float>(Timer::GetDeltaTimeSec()), &Renderer::GetOceanSwashDepth);
            }

            state.mapping = TerrainDeformation::GetMapping();
            state.bounds  = TerrainDeformation::GetBounds();
            TerrainDeformation::LogStats(IsTerrainDeformationReady());
        }

        Renderer::view().frame.terrain_deformation        = IsTerrainDeformationReady() ? state.mapping : Vector4::Zero;
        Renderer::view().frame.terrain_deformation_bounds = state.bounds;
    }

    void Renderer::Pass_TerrainDeformation()
    {
        auto& state = m_pass_state.terrain_deformation;
        if (!state.texture)
        {
            // a texture only gets an image once it has data, flat ground is all zeros
            vector<RHI_Texture_Slice> slices(1);
            slices[0].mips.resize(1);
            slices[0].mips[0].bytes.assign(static_cast<size_t>(TerrainDeformation::resolution) * TerrainDeformation::resolution * sizeof(uint16_t), std::byte{ 0 });
            state.texture = make_shared<RHI_Texture>(
                RHI_Texture_Type::Type2D,
                TerrainDeformation::resolution, TerrainDeformation::resolution, 1, 1,
                RHI_Format::R16_Float, RHI_Texture_Srv,
                "terrain_deformation", move(slices)
            );
        }

        if (!IsTerrainDeformationReady() || !TerrainDeformation::CollectUploads(state.uploads))
        {
            return;
        }

        // frame uploads record into this command list, ahead of the passes that sample the field
        RHI_CommandList::BeginPass("terrain_deformation");
        for (const TerrainDeformationUpload& upload : state.uploads)
        {
            state.texture->UpdateRegion(upload.x, upload.y, upload.width, upload.height, upload.texels.data(), true);
        }
        RHI_CommandList::EndPass();
    }
}
