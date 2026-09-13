// Convert Blender packets to engine meshes via the running engine's control bridge.
import fs from 'node:fs';
import path from 'node:path';
import {EngineClient} from '../mcp/spartan_engine/engine_client.mjs';
const manifest=JSON.parse(fs.readFileSync('binaries/project/home_garage/manifest.json','utf8'));
const c=new EngineClient({host:'127.0.0.1',port:47791,timeout_ms:60000,source:'home_authoring'});
try {
  for (const [i,m] of manifest.meshes.entries()) {
    const destination=path.resolve('binaries',m.mesh_path);
    if(fs.existsSync(destination))continue;
    const args=JSON.parse(fs.readFileSync(m.file,'utf8'));
    const clean=[];
    for(let j=0;j<args.indices.length;j+=3) {
      const [a,b,d]=args.indices.slice(j,j+3),p=args.positions;
      const u=[p[b*3]-p[a*3],p[b*3+1]-p[a*3+1],p[b*3+2]-p[a*3+2]];
      const v=[p[d*3]-p[a*3],p[d*3+1]-p[a*3+1],p[d*3+2]-p[a*3+2]];
      if((u[1]*v[2]-u[2]*v[1])**2+(u[2]*v[0]-u[0]*v[2])**2+(u[0]*v[1]-u[1]*v[0])**2>1e-12)clean.push(a,b,d);
    }
    args.indices=clean;
    const r=await c.command('mesh_raw_create',args);
    if(!r.ok)throw new Error(m.name+': '+JSON.stringify(r));
    const imported=path.resolve('binaries',r.path);
    if(imported!==destination) {
      fs.copyFileSync(imported,destination);
      // Only remove the exact newly generated intermediate, inside this project.
      if(imported.startsWith(path.resolve('binaries/project')+path.sep))fs.unlinkSync(imported);
    }
    if(i%10===0)console.log(`Home mesh ${i+1}/${manifest.meshes.length}`);
  }
  console.log('HOME MESH IMPORT COMPLETE');
} finally {c.close();}
