// Exercise the actual traversal loops against the RayQuery Proceed/Commit contract.
// This CPU contract test does not replace validation against a real GPU scene.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const source = fs.readFileSync(process.argv[2] ?? 'data/shaders/restir_reservoir.hlsl', 'utf8');
const fog = fs.readFileSync('data/shaders/fog.hlsl','utf8');
for (const [input, anchor, name] of [
    [source, 'float3 direct_lighting_at_vertex(', 'probe_query'],
    [source, 'bool trace_shift_visibility(PathSample src, float3 dst_pos, float3 dst_normal)\n{', 'query'],
    [source, 'bool trace_shadow_ray(float3 origin, float3 direction, float max_dist)\n{', 'query'],
    [fog, 'float fog_trace_visibility(', 'query']
]) {
    const normalized = input.replaceAll('\r\n', '\n');
    const start = normalized.indexOf('RayQuery<', normalized.indexOf(anchor));
    assert.ok(start >= 0 && normalized.indexOf(anchor) >= 0, `missing ${anchor}`);
    const end = normalized.indexOf(name === 'query' ? 'return query.CommittedStatus()' :
        'if (probe_query.CommittedStatus()', start);
    const fragment = normalized.slice(start, end);
    const firstHit = fragment.includes('RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH');
    const program = fragment.replace(/RayQuery<[^>]+>\s+(\w+);/, 'const $1 = new Query();') +
        `result = ${name}.CommittedStatus(); finished = ${name}.finished;`;
    for (const events of [[], ['opaque'], ['candidate'], ['candidate', 'opaque'], ['candidate', 'candidate', 'opaque']]) {
        class Query {
            index = 0; committed = 0; finished = false;
            TraceRayInline() {}
            Proceed() {
                if (this.finished) return false;
                while (this.index < events.length) {
                    const event = events[this.index++];
                    if (event === 'candidate') return true;
                    this.committed = 1;
                    if (firstHit) break;
                }
                this.finished = true;
                return false;
            }
            CandidateType() { return 1; }
            CommitNonOpaqueTriangleHit() { this.committed = 1; if (firstHit) this.finished = true; }
            CommittedStatus() { return this.committed; }
        }
        const context = { Query, tlas: 0, RAY_FLAG_NONE: 0, ray: {}, probe_ray: {}, CANDIDATE_NON_OPAQUE_TRIANGLE: 1 };
        new vm.Script(program).runInNewContext(context);
        assert.equal(context.finished, true, `${anchor}: read result before traversal completed (${events})`);
        assert.equal(context.result, events.length ? 1 : 0, `${anchor}: wrong visibility (${events})`);
    }
}
console.log('PASS visibility: four production queries, opaque/non-opaque/miss traversal');
