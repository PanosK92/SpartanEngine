// Author the home's finished surfaces and curved furniture with native MCP building blocks.
// Run before build_home_garage.py, with an edit-mode engine bridge on port 47791.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {EngineClient} from '../mcp/spartan_engine/engine_client.mjs';
const out='binaries/project/home_garage';
fs.mkdirSync(`${out}/sources/native_geometry`,{recursive:true});
fs.mkdirSync(`${out}/sources/finishes`,{recursive:true});
const c=new EngineClient({host:'127.0.0.1',port:47791,timeout_ms:60000,source:'home_finish_authoring'});
const hash=x=>crypto.createHash('sha256').update(JSON.stringify(x)).digest('hex').slice(0,12);
const objects=[],surfaces={},geometry=new Map();
async function cmd(command,args={}) {const r=await c.command(command,args);if(!r.ok)throw Error(`${command}: ${JSON.stringify(r)}`);return r;}
const fill=(color,roughness)=>({type:'fill',color,roughness});
const grain=(a,b,frequency=40,relief=.018)=>({type:'noise',noise:'perlin',color:a,color_b:b,frequency,octaves:3,relief,roughness:.54,roughness_b:.74});
async function surface(name,layers,properties={}) {
  if(['chrome','brass'].includes(name))layers=layers.map(layer=>({...layer,metalness:name==='chrome'?1:.9}));
  const key=hash({version:2,layers,properties}),file=`${out}/sources/finishes/${name}_${key}.xml`;
  if(!fs.existsSync(file)) {
    const created=await cmd('material_create',{path:`home_finish_${name}_${key}.xml`,name:`home_finish_${name}_${key}`});
    const material_path=created.material.resource.path;
    await cmd('texture_generate',{name:`home_finish_${name}_${key}`,layers,width:1024,height:1024,seed:2002,seamless:true,normal_strength:.55,normal_bevel:3,base_roughness:.65,material_path});
    for(const [property,value] of Object.entries({color_r:1,color_g:1,color_b:1,roughness:1,metalness:name==='chrome'?1:name==='brass'?.9:name==='oxblood'?.2:0,...properties}))await cmd('material_set_property',{path:material_path,property,value});
    await cmd('resource_save',{path:material_path,type:'material'});
    fs.copyFileSync(path.join('binaries',material_path),file);
    console.log(`Finished surface: ${name}`);
  }
  surfaces[name]={file,color:properties.preview_color??null};
}
async function part(name,group,material,p,args,rotation=[0,0,0]) {
  const key=hash(args),nameMesh=`home_curve_${args.shape}_${key}`,cached=`${out}/sources/native_geometry/${nameMesh}.json`;
  let entry=geometry.get(key);
  if(!entry) {
    if(fs.existsSync(cached))entry=JSON.parse(fs.readFileSync(cached,'utf8'));
    else {
      const request={...args,path:`${nameMesh}.mesh`,uv_projection:args.uv_projection??'box',uv_scale:args.uv_scale??[1,1]};
      if(request.profile)request.profile=request.profile.flat();
      if(request.path_points)request.path_points=request.path_points.flat();
      if(fs.existsSync(`binaries/project/mcp/blockout/meshes/${nameMesh}.mesh`))request.reuse_existing=true;
      const result=await cmd('mesh_generate',request);
      const mesh_path=result.resource?.path??result.path??result.mesh?.path;
      if(!mesh_path)throw Error('Missing mesh path: '+JSON.stringify(result));
      const raw=await cmd('mesh_raw_get',{path:mesh_path});
      entry={name:nameMesh,mesh_path,raw};fs.writeFileSync(cached,JSON.stringify(entry));
    }
    geometry.set(key,entry);
  }
  objects.push({name,group,material,position:p,rotation,mesh_path:entry.mesh_path,source:cached,triangles:entry.raw.indices.length/3});
}
const round=(size,radius=.04)=>({shape:'rounded_box',size,radius,segments:8});
const bevel=(size,b=.01)=>({shape:'beveled_box',size,bevel:b});
const turn=profile=>({shape:'revolved_profile',profile,segments:48,uv_projection:'cylindrical'});
const pipe=points=>({shape:'pipe',path_points:points,radius:.008,segments:12});
function ovalLoop(width,depth,y,z=0,n=40) {return Array.from({length:n+1},(_,i)=>[width/2*Math.cos(i*2*Math.PI/n),y,z+depth/2*Math.sin(i*2*Math.PI/n)]);}
async function furniture() {
  // Deep, rounded club chairs replace the original thin rectangular seats.
  for(const x of [7,8.1,9.2]) {
    const p=(dx,y,dz)=>[x+dx,y,-.7+dz];
    await part('Walnut seat cradle','club_chairs','wood',p(0,.405,0),round([.72,.12,.70],.04));
    await part('Leather seat cushion','club_chairs','leather',p(0,.53,0),round([.76,.21,.77],.085));
    await part('Curved padded back','club_chairs','leather',p(0,.89,-.30),round([.81,.70,.23],.10),[-9,0,0]);
    for(const s of [-1,1]) {
      await part('Rolled leather arm','club_chairs','leather',p(s*.43,.70,0),round([.17,.23,.81],.075));
      for(const z of [-.27,.27])await part('Turned walnut foot','club_chairs','wood',p(s*.29,.065,z),turn([[0,0],[.043,0],[.047,.04],[.030,.30],[0,.30]]));
    }
    await part('Seat piping','club_chairs','linen',p(0,0,0),pipe(ovalLoop(.73,.73,.595)));
    for(const dx of [-.19,.19])for(const y of [.82,1.03])await part('Upholstery button','club_chairs','leather',p(dx,y,-.158),{shape:'ellipsoid',size:[.03,.03,.016],segments:16});
  }
  for(const x of [10.3,12.3,14.3]) {
    await part('Flared stool pedestal','bar_stools','chrome',[x,.055,-5.55],turn([[0,0],[.29,0],[.30,.025],[.27,.055],[.055,.11],[.038,.66],[.12,.70],[0,.70]]));
    await part('Stool padded seat','bar_stools','leather',[x,.82,-5.55],{shape:'rounded_cylinder',radius:.32,height:.15,bevel:.045,segments:48,bevel_segments:8});
    await part('Stool foot ring','bar_stools','brass',[x,.35,-5.55],{shape:'torus',major_radius:.235,minor_radius:.014,segments:48,minor_segments:12});
    await part('Seat welt','bar_stools','linen',[x,.84,-5.55],{shape:'torus',major_radius:.308,minor_radius:.005,segments:48,minor_segments:8});
  }
  await part('Walnut coffee table','coffee_table','oak',[8.1,.345,1.1],round([2.55,.09,1.02],.043));
  for(const x of [7.15,9.05])for(const z of [.79,1.41])await part('Splayed table leg','coffee_table','wood',[x,.065,z],turn([[0,0],[.045,0],[.055,.255],[0,.255]]));
  // The jukebox has a closed arched walnut case, a domed record window and inlaid trim.
  const x=14.6,z=-1.9;
  const outline=[[-.66,0],[.66,0],[.66,1.44]];
  for(let i=1;i<=32;i++){const a=i*Math.PI/32;outline.push([.66*Math.cos(a),1.44+.66*Math.sin(a)]);}
  await part('Arched jukebox cabinet','jukebox_case','wood',[x,.06,z],{shape:'extruded_profile',profile:outline,depth:.70});
  await part('Jukebox kick plinth','jukebox_case','metal',[x,.10,z],round([1.38,.16,.77],.055));
  await part('Jukebox inset grille','jukebox_case','brass',[x,.68,z+.38],{shape:'inset_panel',size:[.97,1.0,.055],border:.055,inset:.022});
  await part('Speaker grille cloth','jukebox_case','speaker_cloth',[x,.66,z+.403],round([.82,.84,.025],.012));
  for(let j=0;j<9;j++)await part('Grille ribs','jukebox_case','brass',[x-.34+j*.085,.66,z+.423],{shape:'capsule',radius:.010,height:.76,segments:16});
  for(const [radius,inner,mat,dz] of [[.625,.591,'brass',.388],[.580,.547,'amber',.410],[.530,.506,'brass',.418],[.482,.458,'mint_glow',.423]]) {
    await part('Crown inlay','jukebox_case',mat,[x,1.50,z+dz],{shape:'arc',radius,inner_radius:inner,start_degrees:0,sweep_degrees:180,depth:.027,segments:48});
    for(const s of [-1,1])await part('Vertical inlay','jukebox_case',mat,[x+s*(radius+inner)/2,.82,z+dz],round([radius-inner,1.36,.027],.010));
  }
  await part('Record display backing','jukebox_case','glass',[x,1.49,z+.393],{shape:'sector',radius:.445,start_degrees:0,sweep_degrees:180,depth:.02,segments:48});
  await part('Record label medallion','jukebox_case','brass',[x,1.67,z+.421],{shape:'disk',radius:.125,depth:.016,segments:48},[90,0,0]);
  await part('Selection bezel','jukebox_case','chrome',[x,1.33,z+.426],round([.81,.35,.045],.020));
  await part('Selection glass','jukebox_case','glass',[x,1.33,z+.451],round([.75,.30,.021],.010));
  for(let row=0;row<3;row++)for(let col=0;col<6;col++)await part('Song selection key','jukebox_case','paper',[x-.30+col*.12,1.225+row*.1,z+.469],round([.087,.061,.012],.005));
  await part('Coin slot surround','jukebox_case','brass',[x,1.08,z+.449],round([.25,.11,.02],.009));
  await part('Coin slot','jukebox_case','metal',[x,1.08,z+.462],bevel([.14,.015,.012],.002));
  // Rounded pressure vessel, turned gauge and motor cooling fins.
  await part('Compressor pressure vessel','compressor','oxblood',[-10.4,.58,-1.9],{shape:'capsule',radius:.30,height:1.02,segments:48});
  await part('Compressor motor','compressor','metal',[-10.4,1.19,-1.9],{shape:'rounded_cylinder',radius:.17,height:.38,bevel:.04,segments:32,bevel_segments:6},[0,0,90]);
  for(let i=0;i<6;i++)await part('Motor cooling fin','compressor','chrome',[-10.55+i*.06,1.19,-1.9],{shape:'disk',radius:.185,depth:.012,segments:32},[0,0,90]);
  await part('Pressure gauge rim','compressor','chrome',[-10.4,1.12,-1.56],{shape:'disk',radius:.073,depth:.032,segments:32},[90,0,0]);
  await part('Pressure gauge dial','compressor','paper',[-10.4,1.12,-1.538],{shape:'disk',radius:.061,depth:.008,segments:32},[90,0,0]);
  for(const dx of [-.22,.22])await part('Compressor rubber foot','compressor','rubber',[-10.4+dx,.12,-1.9],round([.12,.18,.32],.035));
  // Raised-panel cabinetry replaces a completely plain front without filling the walkway.
  for(const x of [10.4,11.45,12.5,13.55,14.6]) {
    await part('Bar cabinet door','cabinet_fronts','wood',[x,.60,-6.416],{shape:'inset_panel',size:[.92,.83,.055],border:.055,inset:.026});
    await part('Cabinet pull','cabinet_fronts','brass',[x+.31,.81,-6.367],{shape:'capsule',radius:.013,height:.14,segments:16});
  }
  // Sculpted porcelain pots: a rolled rim and hollow opening visible at eye level.
  for(const [x,s] of [[5.6,1.2],[15.5,1.3]])await part('Porch ceramic planter','porch_pottery','ceramic',[x,.03,6.5],turn([[.17,0],[.21,.025],[.28,.40],[.29,.47],[.285,.51],[.25,.52],[.24,.49],[.25,.45],[.20,.08],[.17,.06]].map(([r,y])=>[r*s,y*s])));
}
try {
  const snapshot=await cmd('context_snapshot');if(snapshot.status.loading)throw Error('Wait for the world to finish loading before authoring');
  await cmd('engine_set_mode',{mode:'edit'});
  await surface('wood',[fill('#66462e',.72),{...grain('#5c3d27','#7d5a38',9,.003),opacity:.24,scale_x:1,scale_y:14,warp:.20},{type:'scratches',color:'#92704d',opacity:.025,density:.08,roughness:.76}],{normal:.06,clearcoat:.10,clearcoat_roughness:.45});
  await surface('oak',[fill('#977348',.69),{...grain('#88633f','#b58a54',8,.003),opacity:.24,scale_x:1,scale_y:18,warp:.18}],{normal:.06,clearcoat:.12,clearcoat_roughness:.40});
  await surface('leather',[fill('#65452f',.73),{...grain('#5e402e','#76553e',115,.005),opacity:.20},{type:'noise',noise:'worley',frequency:145,opacity:.035,color:'#503a2a',color_b:'#816049',relief:.001,roughness:.72}],{normal:.085,sheen:.20,clearcoat:.03,clearcoat_roughness:.65});
  await surface('chrome',[fill('#a4a9a6',.29),{type:'scratches',color:'#e2dfd0',opacity:.15,density:.65,angle:0,relief:.006,roughness:.37,metalness:1}],{anisotropic:.6,anisotropic_rotation:.25});
  await surface('brass',[fill('#a37b38',.32),{...grain('#786039','#bf9854',12,.005),opacity:.17,metalness:.9}],{anisotropic:.45,clearcoat:.12});
  await surface('oxblood',[fill('#652e23',.38),{type:'noise',noise:'perlin',frequency:95,color:'#55241c',color_b:'#75392b',opacity:.14,relief:.007,metalness:.35}],{clearcoat:.65,clearcoat_roughness:.17});
  await surface('linen',[fill('#bcb294',.88),{...grain('#afa588','#cbbfa0',115,.003),opacity:.25}],{normal:.08,sheen:.65});
  await surface('speaker_cloth',[fill('#292a23',.91),{type:'checker',count_x:150,count_y:150,color:'#4d4b3b',color_b:'#171914',opacity:.40},grain('#1c211b','#444237',85,.010)],{sheen:.25});
  await surface('ceramic',[fill('#789484',.34),{type:'noise',noise:'perlin',frequency:6,color:'#648374',color_b:'#9cafa0',opacity:.25,relief:.008}],{clearcoat:.85,clearcoat_roughness:.14});
  await furniture();
  fs.writeFileSync(`${out}/sources/native_upgrades.json`,JSON.stringify({surfaces,objects},null,2));
  console.log(`Native home finish complete: ${objects.length} placements, ${geometry.size} shared meshes, ${Object.keys(surfaces).length} surfaces`);
} finally {c.close();}
