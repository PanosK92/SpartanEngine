// Run only against the separate audit engine listening on port 47779.
import fs from 'node:fs';
import path from 'node:path';
import {EngineClient} from '../mcp/spartan_engine/engine_client.mjs';
const directory=path.resolve('binaries/lighting_tests/bloom_tests');
fs.mkdirSync(directory,{recursive:true});
const slash=s=>s.replaceAll('\\','/');
const command=process.argv[2]??'load';
const client=new EngineClient({host:'127.0.0.1',port:47779,timeout_ms:30000,source:'bloom_audit'});
try {
    if(command==='load') {
        const lamps=[
            {name:'amber',color:[1,.1,.0075],x:-1.6,scale:[.25,.25,.03],strength:.25},
            {name:'tube',color:[1,1,1],x:0,scale:[.025,1.8,.03],strength:1},
            {name:'pinpoint',color:[.03,.3,1],x:1.6,scale:[.035,.035,.03],strength:5}
        ];
        for(const p of lamps)fs.writeFileSync(path.join(directory,`${p.name}.xml`),`<?xml version="1.0"?><Material><color_r>${p.color[0]}</color_r><color_g>${p.color[1]}</color_g><color_b>${p.color[2]}</color_b><color_a>1</color_a><roughness>1</roughness><metalness>0</metalness><emissive_from_albedo>${p.strength}</emissive_from_albedo><terrain_blend>0</terrain_blend></Material>`);
        const script=path.join(directory,'bloom_fixture.lua');
        fs.writeFileSync(script,`local fixture={}
function fixture:Initialize(host)
${lamps.map(p=>`do
 local e=World.CreateEntity();e:SetName('${p.name}');e:SetTransient(true);e:SetParent(host)
 e:SetPosition(Vector3(${p.x},0,4));e:SetScale(Vector3(${p.scale.join(',')}))
 local r=e:AddComponent(ComponentType.Render);r:SetMesh(MeshType.Cube)
 r:SetMaterial('${slash(path.join(directory,`${p.name}.xml`))}')
end`).join('\n')}
end
return fixture
`);
        const vars={'r.tonemapping':4,'r.restir_pt':0,'r.ray_traced_reflections':0,'r.ray_traced_shadows':0,'r.fog':0,'r.bloom':1,'r.bloom_scatter':.7,'r.vhs':0,'r.film_grain':0,'r.motion_blur':0,'r.depth_of_field':0,'r.chromatic_aberration':0,'r.sharpness':0,'r.antialiasing_upsampling':2,'r.grid':0,'r.dynamic_resolution':0,'r.resolution_scale':1};
        const world=path.join(directory,'bloom_fixture.world');
        fs.writeFileSync(world,`<?xml version="1.0"?><World name="bloom_fixture"><ConsoleVariables>${Object.entries(vars).map(([name,value])=>`<Variable name="${name}" value="${value}"/>`).join('')}</ConsoleVariables><Entities>
<Entity name="camera" id="7735000000000000100" active="true" position="0 0 0" rotation="0 0 0 1" scale="1 1 1"><camera exposure_mode="0" aperture="8" shutter_speed=".033" iso="100" fov_horizontal="1.57079637" near_plane=".1" far_plane="1000" projection="0" flags="0" /></Entity>
<Entity name="bloom_fixture" id="7735000000000000101" active="true" position="0 0 0" rotation="0 0 0 1" scale="1 1 1"><script file_path="${slash(script)}"/></Entity>
</Entities></World>`);
        console.log(await client.command('world_load',{path:world}));
    } else if(command==='capture') {
        console.log(await client.command('screenshot_take',{path:process.argv[3]??'bloom_live.png'}));
    } else if(command==='move') {
        console.log(await client.command('camera_set_view',{position:[Number(process.argv[3]??.001),0,0],rotation_euler:[0,0,0]}));
    } else if(command==='profile') {
        console.log(JSON.stringify(await client.command('profiler_snapshot',{type:'gpu',sort:'duration',top:100}),null,2));
    } else throw new Error('Use load, capture, move or profile');
} finally {client.socket?.destroy();}
