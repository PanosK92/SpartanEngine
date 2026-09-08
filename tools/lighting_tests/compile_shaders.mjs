import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
fs.mkdirSync('binaries/lighting_tests',{recursive:true});
const dxc=process.env.VULKAN_SDK?path.join(process.env.VULKAN_SDK,'Bin','dxc.exe'):'dxc';
const variants=[];
for(const rt of [false,true]) {
    for(const shader of ['output','auto_exposure','light','light_image_based','light_composition','reflections_shade']) {
        if(shader==='reflections_shade'&&!rt)continue;
        variants.push({shader,defines:rt?['RAY_TRACING_ENABLED']:[],entry:'main_cs',profile:'cs_6_7'});
    }
    for(const indirect of [false,true]) variants.push({shader:'g_buffer',defines:[...(rt?['RAY_TRACING_ENABLED']:[]),...(indirect?['INDIRECT_DRAW']:[])],entry:'main_ps',profile:'ps_6_7'});
    for(const stage of ['FOG_INJECT','FOG_INTEGRATE'])variants.push({shader:'fog_froxel',defines:[stage,...(rt?['RAY_TRACING_ENABLED']:[])],entry:'main_cs',profile:'cs_6_7'});
}
for(const shader of ['restir_pt','reflections_trace'])variants.push({shader,defines:['RAY_TRACING_ENABLED'],profile:'lib_6_7'});
for(const shader of ['restir_pt_temporal','restir_pt_spatial','restir_pt_spatial_shift'])variants.push({shader,defines:['RAY_TRACING_ENABLED'],entry:'main_cs',profile:'cs_6_7'});
for(const stage of ['PREFILTER','DOWNSAMPLE','UPSAMPLE_BLEND_MIP','BLEND_FRAME'])variants.push({shader:'bloom',defines:[stage],entry:'main_cs',profile:'cs_6_7'});
for(const v of variants) {
    const name=[v.shader,...v.defines].join('_');
    const args=['-T',v.profile,...v.defines.flatMap(d=>['-D',d]),'-spirv','-fspv-target-env=vulkan1.3','-fvk-use-dx-layout','-fspv-preserve-bindings',
        '-fvk-u-shift','100','all','-fvk-b-shift','200','all','-fvk-t-shift','300','all','-fvk-s-shift','400','all',
        '-Fo',`binaries/lighting_tests/${name}.spv`];
    if(v.entry)args.push('-E',v.entry,'-D',v.profile.startsWith('cs')?'SP_SHADER_STAGE_COMPUTE=1':'SP_SHADER_STAGE_PIXEL=1');
    args.push(`data/shaders/${v.shader}.hlsl`);
    const r=spawnSync(dxc,args,{encoding:'utf8'});
    if(r.status!==0){console.error(name,r.error??r.stdout+r.stderr);process.exit(1);}
    console.log(`PASS ${name}`);
}
