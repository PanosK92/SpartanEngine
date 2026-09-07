import fs from 'node:fs';
import path from 'node:path';
import {EngineClient} from '../mcp/spartan_engine/engine_client.mjs';
const manifest=JSON.parse(fs.readFileSync('binaries/project/exo_chora/manifest.json','utf8'));
const client=new EngineClient({host:'127.0.0.1',port:47791,timeout_ms:60000,source:'village_mesh_import'});
const receipt=[];
try {
  for (let i=0;i<manifest.meshes.length;i++) {
    const m=manifest.meshes[i];
    const args=JSON.parse(fs.readFileSync(path.join('binaries/project/exo_chora/packets',path.basename(m.file)),'utf8'));
    // Blender bevel triangulation can contain collapsed corners after float rounding.
    const cleaned=[];
    for(let t=0;t<args.indices.length;t+=3) {
      const [a,b,c]=args.indices.slice(t,t+3);
      if(a===b||a===c||b===c) continue;
      const p=args.positions;
      const ab=[p[b*3]-p[a*3],p[b*3+1]-p[a*3+1],p[b*3+2]-p[a*3+2]];
      const ac=[p[c*3]-p[a*3],p[c*3+1]-p[a*3+1],p[c*3+2]-p[a*3+2]];
      const cross=[ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0]];
      if(cross.reduce((sum,x)=>sum+x*x,0)<=1e-12) continue;
      cleaned.push(a,b,c);
    }
    args.indices=cleaned;
    if(fs.existsSync(path.join('binaries',m.mesh_path))) {
      receipt.push({name:m.name,cached:true});continue;
    }
    const result=await client.command('mesh_raw_create',args);
    if(!result.ok) throw new Error(m.name+': '+JSON.stringify(result));
    receipt.push({name:m.name,path:result.path,vertices:result.vertex_count,triangles:result.index_count/3});
    if(i%20===0) console.log(`Imported ${i+1}/${manifest.meshes.length}: ${m.group}`);
  }
  for (const variant of [1,2,3]) {
    const name='olive_0'+variant;
    if(fs.existsSync('binaries/project/exo_chora/meshes/'+name+'.mesh'))continue;
    const saved='binaries/project/mcp/blockout/meshes/'+name+'.mesh';
    if(fs.existsSync(saved)) {fs.copyFileSync(saved,'binaries/project/exo_chora/meshes/'+name+'.mesh');continue;}
    const result=await client.command('resource_save',{name,type:'mesh',save_path:'project/exo_chora/meshes/'+name+'.mesh'});
    if(!result.ok) throw new Error(JSON.stringify(result));
    fs.copyFileSync(path.join('binaries',result.path),'binaries/project/exo_chora/meshes/'+name+'.mesh');
    console.log('Preserved local olive mesh',name);
  }
  fs.writeFileSync('binaries/project/exo_chora/import-receipt.json',JSON.stringify(receipt,null,2));
  console.log('VILLAGE_MESH_IMPORT_COMPLETE',receipt.length);
} finally {client.close();}
