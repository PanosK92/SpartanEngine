// Exercise the production UI callback and vendor sizing code with recorded UI
// interactions. No GPU or window is needed to reproduce the false 4K label.
import fs from 'node:fs';
function extract(file,signature) {
    const source=fs.readFileSync(file,'utf8');
    const start=source.indexOf(signature);
    if(start<0)throw new Error(`Missing ${signature}`);
    let end=source.indexOf('{',start),depth=1;
    while(depth&&++end<source.length){if(source[end]==='{')depth++;if(source[end]==='}')depth--;}
    if(depth)throw new Error(`Unclosed ${signature}`);
    return source.slice(start,end+1);
}
const ui='source/editor/widgets/RenderOptions.cpp';
const fixture=String.raw`
#include <algorithm>
#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
using namespace std;
struct Vector2 { float x,y; };
struct DisplayMode { uint32_t width,height,refresh_rate; };
vector<DisplayMode> display_modes;
vector<string> display_modes_string;
bool visible=true;
bool option_visible(const char*) { return visible; }
void option_first_column() {}
void option_second_column() {}
namespace ImGui {
    string preview,click;
    vector<string> entries;
    bool open=true;
    void TextDisabled(const char*,...) {}
    void PushID(const char*) {}
    void PopID() {}
    void PushItemWidth(float) {}
    void PopItemWidth() {}
    bool BeginCombo(const char*,const char* text) { preview=text;entries.clear();return open; }
    bool Selectable(const char* text,bool) { entries.emplace_back(text);if(click==text){click.clear();return true;}return false; }
    void SetItemDefaultFocus() {}
    void EndCombo() {}
}
namespace ImGuiSp { void tooltip(const char*) {} }
namespace Display {
    vector<DisplayMode> modes;
    const vector<DisplayMode>& GetDisplayModes() { return modes; }
}
struct RenderOptions { void OnVisible(); };
enum class Renderer_AntiAliasing_Upsampling { AA_Off_Upscale_Linear, AA_Fxaa_Upscale_Linear, AA_Taau_Upscale_Taau, AA_Xess_Upscale_Xess, AA_Dlss_Upscale_Dlss };
struct Cvar {
    int value=0;
    template<class T>T GetValueAs() { return static_cast<T>(value); }
} cvar_antialiasing_upsampling;
namespace Renderer {
    Vector2 render{1920,1080},output{3840,2160};int writes=0;
    const Vector2& GetResolutionRender() { return render; }
    const Vector2& GetResolutionOutput() { return output; }
    void SetResolutionRender(uint32_t w,uint32_t h,bool=true) { render={float(w),float(h)};writes++; }
}
#define SP_LOG_INFO(...) ((void)0)
`+extract(ui,'void option_resolution(')+'\n'+extract(ui,'void RenderOptions::OnVisible()')+'\n'+
extract('source/rendering/Renderer.cpp','void sanitize_vendor_upscaler_resolution()')+String.raw`
void draw() { option_resolution("Render resolution",Renderer::render,Renderer::SetResolutionRender,"size"); }
int main() {
    Display::modes={{3840,2160,240},{3840,2160,60},{1920,1080,60},{1920,1080,120},{1280,720,60}};
    RenderOptions options;options.OnVisible();
    assert(display_modes.size()==3); // Keep lower-Hz dimensions, remove duplicate sizes.
    for(int mode=0;mode<=4;mode++) {
        cvar_antialiasing_upsampling.value=mode;
        Renderer::output={3840,2160};Renderer::render={3840,2160};Renderer::writes=0;
        ImGui::click="1920x1080";draw();
        assert(Renderer::writes==1 && Renderer::render.x==1920 && Renderer::render.y==1080);
        sanitize_vendor_upscaler_resolution();draw();
        assert(Renderer::writes==1 && ImGui::preview=="1920x1080");
    }
    for(int mode:{3,4}) {
        cvar_antialiasing_upsampling.value=mode;
        Renderer::output={3180,1555};Renderer::render={1920,1080};Renderer::writes=0;
        sanitize_vendor_upscaler_resolution();draw();
        assert(Renderer::render.x==1920 && Renderer::render.y==938);
        assert(ImGui::preview=="1920x938" && Renderer::writes==1);
        for(int frame=0;frame<100;frame++) {sanitize_vendor_upscaler_resolution();draw();}
        assert(ImGui::preview=="1920x938" && Renderer::writes==1);
    }
    display_modes.clear();display_modes_string.clear();draw();
    assert(ImGui::preview=="1920x938" && ImGui::entries.empty());
    ImGui::open=false;Renderer::render={1280,720};draw();assert(ImGui::preview=="1280x720");
    puts("PASS resolution selection across Off/FXAA/TAAU/XeSS/DLSS, custom-size label, stable frames, empty presets and refresh-rate deduplication");
}
`;
fs.mkdirSync('binaries/render_options_tests',{recursive:true});
fs.writeFileSync('binaries/render_options_tests/resolution.cpp',fixture);
