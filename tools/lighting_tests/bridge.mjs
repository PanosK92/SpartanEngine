import {EngineClient} from '../mcp/spartan_engine/engine_client.mjs';
const client=new EngineClient({host:'127.0.0.1',port:47779,timeout_ms:30000,source:'lighting_audit'});
console.log(JSON.stringify(await client.command(process.argv[2],JSON.parse(process.argv[3]??'{}')),null,2));
client.socket?.destroy();
