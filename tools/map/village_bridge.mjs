import fs from 'node:fs';
import {EngineClient} from '../mcp/spartan_engine/engine_client.mjs';
const client = new EngineClient({host:'127.0.0.1',port:47791,timeout_ms:60000,source:'village_authoring'});
try {
  const input = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
  for (const request of Array.isArray(input) ? input : [input]) {
    const result = await client.command(request.command, request.args ?? {});
    if (request.output) fs.writeFileSync(request.output, JSON.stringify(result,null,2));
    console.log(JSON.stringify(result));
    if (!result.ok) break;
  }
} finally {client.close();}
