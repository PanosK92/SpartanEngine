// Controlled live fixture on the separately launched audit engine (port 47779).
import fs from 'node:fs';
import path from 'node:path';
import {EngineClient} from '../mcp/spartan_engine/engine_client.mjs';
const directory=path.resolve('binaries/lighting_tests');
fs.mkdirSync(directory,{recursive:true});
const slash=s=>s.replaceAll('\\','/');
const command=process.argv[2]??'load';
if(command==='capture') {
    const client=new EngineClient({host:'127.0.0.1',port:47779,timeout_ms:30000,source:'lighting_audit'});
    console.log(await client.command('screenshot_take',{path:process.argv[3]??'lighting_emission_fixture.png'}));
    client.socket?.destroy();
    process.exit(0);
}
const reflections=command==='reflections';
const gi=command==='gi'||command==='gi-black';
const panels=[
    {name:'red_on_black',color:[0,0,0],texture:[255,0,0]},
    {name:'green_on_blue',color:[0,0,1],texture:[0,255,0]},
    {name:'white_texture',color:[.18,.18,.18],texture:[255,255,255]},
    {name:'white_from_albedo',color:[1,1,1],strength:.1}
];
if(gi)panels.splice(0,panels.length,{name:command==='gi-black'?'black_on_blue':'green_on_blue',color:[0,0,1],texture:command==='gi-black'?[0,0,0]:[0,255,0]});
for(const p of panels) {
    if(p.texture) {
        const tga=Buffer.alloc(18+16*16*4);tga[2]=2;tga.writeUInt16LE(16,12);tga.writeUInt16LE(16,14);tga[16]=32;tga[17]=0x28;
        for(let i=18;i<tga.length;i+=4){tga[i]=p.texture[2];tga[i+1]=p.texture[1];tga[i+2]=p.texture[0];tga[i+3]=255;}
        fs.writeFileSync(path.join(directory,`${p.name}.tga`),tga);
    }
    const texture=p.texture?`<textures count="36"><texture_20 texture_type="5" texture_slot="0" texture_name="${p.name}" texture_path="${slash(path.join(directory,`${p.name}.tga`))}" /></textures>`:'';
    fs.writeFileSync(path.join(directory,`${p.name}.xml`),`<?xml version="1.0"?><Material><color_r>${p.color[0]}</color_r><color_g>${p.color[1]}</color_g><color_b>${p.color[2]}</color_b><color_a>1</color_a><roughness>1</roughness><metalness>0</metalness><emissive_from_albedo>${p.strength??0}</emissive_from_albedo><terrain_blend>0</terrain_blend><texture_tiling_x>1</texture_tiling_x><texture_tiling_y>1</texture_tiling_y>${texture}</Material>`);
}
const script=path.join(directory,'emission_fixture.lua');
fs.writeFileSync(path.join(directory,'mirror.xml'),`<?xml version="1.0"?><Material><color_r>${gi?.18:1}</color_r><color_g>${gi?.18:1}</color_g><color_b>${gi?.18:1}</color_b><color_a>1</color_a><roughness>${gi?1:0}</roughness><metalness>${gi?0:1}</metalness><terrain_blend>0</terrain_blend></Material>`);
fs.writeFileSync(script,`local fixture={}
function fixture:Initialize(host)
${panels.map((p,i)=>`do
 local e=World.CreateEntity();e:SetName('${p.name}');e:SetTransient(true);e:SetParent(host)
 e:SetPosition(Vector3(${gi?0:i*2-3},0,4));e:SetScale(Vector3(1.8,2.8,.1))
 local r=e:AddComponent(ComponentType.Render);r:SetMesh(MeshType.Cube)
 r:SetMaterial('${slash(path.join(directory,`${p.name}.xml`))}')
end`).join('\n')}
${reflections||gi?`do
 local e=World.CreateEntity();e:SetName('mirror');e:SetTransient(true);e:SetParent(host)
 e:SetPosition(Vector3(0,-1.5,4));e:SetScale(Vector3(10,.1,8))
 local r=e:AddComponent(ComponentType.Render);r:SetMesh(MeshType.Cube)
 r:SetMaterial('${slash(path.join(directory,'mirror.xml'))}')
end`:''}
end
return fixture
`);
const vars={'r.tonemapping':5,'r.restir_pt':0,'r.ray_traced_reflections':0,'r.ray_traced_shadows':0,'r.fog':0,'r.bloom':0,'r.vhs':0,'r.film_grain':0,'r.motion_blur':0,'r.depth_of_field':0,'r.chromatic_aberration':0,'r.sharpness':0,'r.antialiasing_upsampling':0,'r.grid':0};
if(reflections)vars['r.ray_traced_reflections']=1;
if(gi)vars['r.restir_pt']=1;
const world=path.join(directory,'emission_fixture.world');
fs.writeFileSync(world,`<?xml version="1.0"?><World name="lighting_emission_fixture"><ConsoleVariables>${Object.entries(vars).map(([name,value])=>`<Variable name="${name}" value="${value}"/>`).join('')}</ConsoleVariables><Entities>
<Entity name="camera" id="7734000000000000100" active="true" position="0 0 0" rotation="0 0 0 1" scale="1 1 1"><camera exposure_mode="0" aperture="10" shutter_speed=".0096" iso="100" fov_horizontal="1.57079637" near_plane=".1" far_plane="1000" projection="0" flags="0" /></Entity>
<Entity name="emission_fixture" id="7734000000000000101" active="true" position="0 0 0" rotation="0 0 0 1" scale="1 1 1"><script file_path="${slash(script)}"/></Entity>
</Entities></World>`);
const client=new EngineClient({host:'127.0.0.1',port:47779,timeout_ms:30000,source:'lighting_audit'});
if(command==='load'||reflections||gi)console.log(await client.command('world_load',{path:world}));
else throw new Error('Use load, reflections, gi or capture');
client.socket?.destroy();
