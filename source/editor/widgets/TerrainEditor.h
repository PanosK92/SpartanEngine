/*
Copyright(c) 2015-2026 Panos Karabelas

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

//= INCLUDES ========================
#include "Widget.h"
#include "world/TerrainSystem.h"
#include <functional>
#include <memory>
#include <string>
//===================================

namespace spartan
{
    class Terrain;
}

class FileDialog;

// the terrain authoring window, four questions in four tabs, one action bar that owns every rebuild
class TerrainEditor : public Widget
{
public:
    TerrainEditor(Editor* editor);
    ~TerrainEditor();

    void OnTick() override;
    void OnTickVisible() override;

    static bool IsSculptActive() { return s_sculpt_active; }

private:
    // shape is the land, ground is what it is made of, life is what grows on it, analysis is why
    enum class Mode : uint32_t
    {
        Shape,
        Ground,
        Life,
        Analysis,
        Max
    };

    spartan::Terrain* ResolveTerrain() const;
    int ResolveSelectedTileIndex() const;
    void DrawBrushRing(const spartan::math::Vector3& hit) const;
    void TickSculpting();

    void DrawSummary(spartan::Terrain* terrain);
    void DrawActionBar(spartan::Terrain* terrain);
    void DrawShape(spartan::Terrain* terrain);
    void DrawSculpt(spartan::Terrain* terrain);
    void DrawGround(spartan::Terrain* terrain);
    void DrawGroundLayer(spartan::Terrain* terrain, uint32_t index);
    void DrawLife(spartan::Terrain* terrain);
    void DrawLifeLayer(spartan::Terrain* terrain, uint32_t index);
    void DrawAnalysis(spartan::Terrain* terrain);
    void DrawNoTerrain();

    void Rebuild(spartan::Terrain* terrain);
    void Rescatter(spartan::Terrain* terrain);
    void Browse(const std::function<void(const std::string&)>& on_selected);
    void TickBrowse();

    Mode m_mode                 = Mode::Shape;
    uint32_t m_ground_selected  = 0;
    uint32_t m_life_selected    = 0;
    // amber on the action bar, what is authored no longer matches what is in the viewport
    bool m_shape_dirty          = false;
    bool m_scatter_dirty        = false;

    // sculpting
    spartan::TerrainBrush m_brush;
    bool m_sculpt_enabled       = false;
    bool m_heights_dirty        = false;
    uint32_t m_flat_resolution  = 128;
    float m_strength_per_second = 20.0f;
    float m_rebuild_timer       = 0.0f;

    // click to browse, the window needs a dialog for the height map and for scatter meshes
    std::unique_ptr<FileDialog> m_dialog;
    std::function<void(const std::string&)> m_dialog_callback;
    bool m_dialog_visible = false;

    inline static bool s_sculpt_active = false;
};
