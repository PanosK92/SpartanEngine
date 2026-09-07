import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
const sdk=process.env.VULKAN_SDK;
if(!sdk) throw new Error('VULKAN_SDK is required');
fs.mkdirSync('binaries/forest_perf/shaders',{recursive:true});
const cases=[['grass_populate',[]]];
for(const filter of ['MIN','MAX','AVERAGE']) {
    cases.push(['amd_fidelity_fx/spd',[filter]],['amd_fidelity_fx/spd',[filter,'ONE_MIP']]);
}
for(const [shader,defines] of cases) {
    const name=shader.replaceAll('/','_')+'_'+defines.join('_');
    const out=`binaries/forest_perf/shaders/${name}.spv`;
    const args=['-T','cs_6_7','-E','main_cs','-spirv','-fspv-target-env=vulkan1.3','-fvk-use-dx-layout',
        '-D','SP_SHADER_STAGE_COMPUTE=1',...defines.flatMap(d=>['-D',d+'=1']),'-Fo',out,`data/shaders/${shader}.hlsl`];
    for(const [exe,params] of [[path.join(sdk,'Bin/dxc.exe'),args],[path.join(sdk,'Bin/spirv-val.exe'),['--target-env','vulkan1.3','--scalar-block-layout',out]]]) {
        const r=spawnSync(exe,params,{encoding:'utf8'});
        if(r.status!==0) throw new Error(r.error??r.stderr??r.stdout);
    }
    console.log('PASS',name);
}
