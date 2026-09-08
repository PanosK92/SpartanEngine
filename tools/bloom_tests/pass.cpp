#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
using namespace std;
#define SP_ASSERT assert
enum class RHI_Shader_Type { Compute, RayGeneration, RayMiss, RayHit, Vertex, MeshShader };
enum class Renderer_Shader { bloom_prefilter_c, bloom_downsample_c, bloom_upsample_blend_mip_c, bloom_blend_frame_c, exposure };
enum class Renderer_RenderTarget { bloom };
enum class Renderer_BindingsSrv { tex, tex2 };
enum class Renderer_BindingsUav { tex };
constexpr uint32_t rhi_all_mips=~0u;
struct RHI_Shader {
    Renderer_Shader id;
    RHI_Shader_Type GetShaderStage() const { return RHI_Shader_Type::Compute; }
    bool IsCompiled() const { return true; }
};
struct RHI_Texture {
    uint32_t width,height,mips;
    uint32_t GetWidth() const { return width; }
    uint32_t GetHeight() const { return height; }
    uint32_t GetMipCount() const { return mips; }
};
struct RHI_PipelineState {
    const char* name=nullptr;
    array<RHI_Shader*,6> shaders{};
    array<RHI_Texture*,1> render_target_color_textures{};
    RHI_Texture* render_target_depth_texture=nullptr;
    void* render_target_swapchain=nullptr;
    bool IsCompute() const { return shaders[0]!=nullptr; }
    bool IsRayTracing() const { return shaders[1]!=nullptr; }
    bool IsGraphics() const { return shaders[4]!=nullptr||shaders[5]!=nullptr; }
};
struct RHI_Device { static void InvokePassReset() {} };
struct DispatchRecord { Renderer_Shader shader;uint32_t x,y; };
struct RHI_CommandList {
    RHI_PipelineState m_pso_pending;
    bool m_pipeline_state_dirty=false,m_pass_boundary=false;
    RHI_Shader* bound=nullptr;
    vector<DispatchRecord> dispatches;
    void begin_timeblock(const char*) {}
    void end_timeblock() {}
    void begin_pass(const char*);
    void end_pass();
    void set_pass(const char*);
    void set_shader(RHI_Shader*,const char*);
    bool IsPendingPipelineReady() const;
    void TryBindPendingPipeline();
    void set_pipeline_state(const RHI_PipelineState& pso) { bound=pso.shaders[0];m_pipeline_state_dirty=false; }
    static void BeginPass(const char*);
    static void EndPass();
    static void BeginTimeblock(const char*) {}
    static void EndTimeblock() {}
    static void BeginMarker(const char*) {}
    static void EndMarker() {}
    static void SetShader(RHI_Shader*,const char* name=nullptr);
    static void SetTexture(uint32_t,RHI_Texture*,uint32_t,uint32_t,bool=false) {}
    static void Dispatch(uint32_t,uint32_t);
    static void Dispatch(RHI_Texture* t) { Dispatch((t->width+7)/8,(t->height+7)/8); }
} command;
void RHI_CommandList::BeginPass(const char* name) { command.begin_pass(name); }
void RHI_CommandList::EndPass() { command.end_pass(); }
void RHI_CommandList::SetShader(RHI_Shader* shader,const char* name) { command.set_shader(shader,name); }
void RHI_CommandList::Dispatch(uint32_t x,uint32_t y) {
    command.TryBindPendingPipeline();
    assert(command.bound);
    command.dispatches.push_back({command.bound->id,x,y});
}
struct Cvar { float value;float GetValue() const { return value; } } cvar_bloom{1},cvar_bloom_scatter{.7f};
struct PassConstants { void set_f3_value(float,float,float) {} } m_pcb_pass_cpu;
namespace Renderer {
    RHI_Texture pyramid{1590,773,7};
    RHI_Shader shaders[]={{Renderer_Shader::bloom_prefilter_c},{Renderer_Shader::bloom_downsample_c},
        {Renderer_Shader::bloom_upsample_blend_mip_c},{Renderer_Shader::bloom_blend_frame_c},{Renderer_Shader::exposure}};
    RHI_Texture* GetRenderTarget(Renderer_RenderTarget) { return &pyramid; }
    RHI_Shader* GetShader(Renderer_Shader id) { return &shaders[static_cast<unsigned>(id)]; }
    void Pass_Bloom(RHI_Texture*,RHI_Texture*);
}
#include "pass_fixture.h"
int main() {
    for(bool automatic:{false,true})for(unsigned levels:{1,7}) {
        command=RHI_CommandList{};
        command.begin_pass("preceding_exposure");
        command.set_shader(Renderer::GetShader(Renderer_Shader::exposure),nullptr);
        if(automatic)command.end_pass(); // The showroom uses automatic camera exposure.
        Renderer::pyramid.mips=levels;
        RHI_Texture input{3180,1547,1},output=input;
        Renderer::Pass_Bloom(&input,&output);
        vector<Renderer_Shader> expected{Renderer_Shader::bloom_prefilter_c};
        for(unsigned i=1;i<levels;i++)expected.push_back(Renderer_Shader::bloom_downsample_c);
        for(unsigned i=1;i<levels;i++)expected.push_back(Renderer_Shader::bloom_upsample_blend_mip_c);
        expected.push_back(Renderer_Shader::bloom_blend_frame_c);
        assert(command.dispatches.size()==expected.size());
        for(size_t i=0;i<expected.size();i++)if(command.dispatches[i].shader!=expected[i]) {
            fprintf(stderr,"FAIL: bloom dispatched stale shader at stage %zu (automatic exposure %d)\n",i,automatic);
            return 1;
        }
        assert(command.dispatches.front().x==199 && command.dispatches.front().y==97);
        assert(command.dispatches.back().x==398 && command.dispatches.back().y==194);
    }
    puts("PASS production bloom/RHI pass sequence after manual and automatic exposure, one and seven mip levels");
}
