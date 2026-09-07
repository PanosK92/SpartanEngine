// Capture an already loaded disposable island; never save or change its population.
import fs from 'node:fs';
import assert from 'node:assert/strict';
import {EngineClient} from '../mcp/spartan_engine/engine_client.mjs';
const label=process.argv[2]??'capture';
assert.match(label,/^[a-z0-9_-]+$/i);
const c=new EngineClient({host:'127.0.0.1',port:Number(process.env.SPARTAN_MCP_PORT ?? 47785),timeout_ms:30000});
try {
 const context=await c.command('context_snapshot');
 assert.ok(context.ok&&!context.status.loading&&context.world.entity_count>10000);
 const samples=[];
 for(let i=0;i<30;i++) {
  await new Promise(r=>setTimeout(r,400));
  const sample=await c.command('profiler_snapshot');
  assert.ok(sample.ok);samples.push(sample);
 }
 const contextAfter=await c.command('context_snapshot');
 const comparable=contextAfter.world.file_path===context.world.file_path
  && contextAfter.world.entity_count===context.world.entity_count
  && contextAfter.camera.entity_id===context.camera.entity_id
  && JSON.stringify(contextAfter.camera.position)===JSON.stringify(context.camera.position)
  && JSON.stringify(contextAfter.camera.forward)===JSON.stringify(context.camera.forward);
 const result={context,contextAfter,comparable,samples};
 fs.mkdirSync('binaries/forest_perf',{recursive:true});
 fs.writeFileSync(`binaries/forest_perf/${label}.json`,JSON.stringify(result,null,2));
 assert.ok(comparable,'World or camera changed during capture; saved samples are not a stationary benchmark');
 assert.ok(samples.filter(s=>s.time_blocks.some(b=>b.name==='spartan::Renderer::ProduceFrame'&&b.type==='cpu'&&b.duration_ms>0)).length>=28,
  'Window stopped rendering during capture; discard these results');
 const med=values=>values.sort((a,b)=>a-b)[Math.floor(values.length/2)];
 const block=(name,type)=>med(samples.map(s=>s.time_blocks.find(b=>b.name===name&&b.type===type)?.duration_ms??0));
 console.log(JSON.stringify({label,cpu_ms:med(samples.map(s=>s.cpu_ms)),gpu_ms:med(samples.map(s=>s.gpu_ms)),frame_ms:med(samples.map(s=>s.frame_ms)),world_ms:block('spartan::World::Tick','cpu'),produce_ms:block('spartan::Renderer::ProduceFrame','cpu'),gbuffer_ms:block('g_buffer','gpu'),depth_ms:block('depth_prepass','gpu'),instance_cull_ms:block('instance_cull','gpu')}));
 console.log(JSON.stringify(await c.command('screenshot_take',{path:`project/mcp/blockout/thumbnails/${label}.png`})));
} finally {c.close();}
