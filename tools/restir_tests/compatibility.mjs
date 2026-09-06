import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const source = fs.readFileSync(process.argv[2] ?? 'data/shaders/restir_reservoir.hlsl', 'utf8');
const begin = source.indexOf('bool is_neighbor_gbuffer_compatible(');
const end = source.indexOf('// sampling helpers', begin);
const body = source.slice(source.indexOf('{', begin) + 1, source.lastIndexOf('}', end))
    .replace(/\/\/[^\n]*/g, '')
    .replace(/\bfloat[234]?\s+(\w+)\s*=/g, 'let $1 =')
    .replace(/\(int\)/g, '')
    .replace(/\b(\d+(?:\.\d+)?(?:e-\d+)?)f\b/g, '$1');
const script = new vm.Script(`result = (() => { ${body} })();`);
function compatible(a, b, separation) {
    const context = { center_linear_depth: a, center_pos: 0, center_normal: 1,
        neighbor_pixel: { x: 1, y: 1 }, resolution: { x: 100, y: 100 },
        RESTIR_DEPTH_THRESHOLD: 0.03, RESTIR_NORMAL_THRESHOLD: 0.9,
        tex_depth: { SampleLevel: () => ({ r: b }) },
        GET_SAMPLER: () => 0, sampler_point_clamp: 0,
        linearize_depth: x => x, get_normal: () => 1, get_position: () => separation,
        length: Math.abs, dot: (x, y) => x * y, min: Math.min, max: Math.max, abs: Math.abs,
        saturate: x => Math.min(Math.max(x, 0), 1), lerp: (x, y, t) => x + (y - x) * t };
    script.runInNewContext(context);
    return context.result;
}
for (const a of [1, 10, 100, 200]) for (const ratio of [1, 1.02, 1.0305, 1.045, 1.06]) {
    for (const separation of [0.001, 0.05, a * 0.0199, a * 0.0201]) {
        const b = a * ratio;
        assert.equal(compatible(a, b, separation), compatible(b, a, separation),
            `pair eligibility disagrees at depths ${a}/${b}, separation ${separation}`);
    }
}
assert.equal(compatible(10, 10, 0.1), true);
assert.equal(compatible(10, 20, 0.1), false);
console.log('PASS pairing: reciprocal compatibility at 80 depth/distance boundaries');
