// Run the production temporal candidate/weighting blocks with controlled shift results.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const source = fs.readFileSync(process.argv[2] ?? 'data/shaders/restir_pt_temporal.hlsl', 'utf8');
const eligibility = source.match(/bool usable = ([^;]+);/)[1];
const start = source.indexOf('            if (usable)');
const open = source.indexOf('{', start);
let depth = 1, end = open + 1;
while (depth && end < source.length) {
    if (source[end] === '{') depth++;
    if (source[end] === '}') depth--;
    end++;
}
assert.equal(depth, 0);
const weightsStart = source.indexOf('    float weight_cur =');
const weightsEnd = source.indexOf('    clamp_reservoir_M(combined, get_restir_m_cap());', weightsStart);
const adapt = code => code.replace(/\/\/[^\n]*/g, '')
    .replace(/\b(?:float[234]?|uint|bool|ShiftResult)\s+(\w+)\s*=/g, 'let $1 =')
    .replace(/\b(\d+)u\b/g, '$1').replace(/\b(\d+(?:\.\d+)?)f\b/g, '$1');
const script = new vm.Script(adapt(`if (${eligibility}) {${source.slice(open + 1, end - 1)}}\n` +
    source.slice(weightsStart, weightsEnd)));
const reservoir = (radiance, seed, M = 1) => ({
    M, W: radiance > 0 ? 1 : 0, target_pdf: radiance,
    sample: { seed_path: seed, src_pos: 0, src_normal: 1, src_albedo: 1, src_roughness: 1, src_metallic: 0 }
});
function stream(a, b, forwardVisible = true, backwardVisible = true, confidence = 1) {
    const current = reservoir(a, 1), temporal = reservoir(b, 2, confidence);
    const context = {
        current, temporal, target_cur: a, combined: {}, seed: 0,
        pos_ws: 0, normal_ws: 1, view_dir: 1, albedo: 1, roughness: 1, metallic: 0,
        have_temporal: false, target_temp: 0, jacobian_temp: 0, f_temp: 0,
        target_cur_at_temp: 0, jacobian_cur_at_temp: 0,
        max: Math.max, normalize: x => x, get_camera_position: () => 1,
        is_reservoir_valid: () => true, get_restir_validation_period: () => 0,
        target_scalar: x => x, random_float: () => 0.5,
        trace_shift_visibility: sample => sample.seed_path === 1 ? backwardVisible : forwardVisible,
        try_reconnection_shift: sample => ({ ok: true, jacobian: 1, f_dst: sample.seed_path === 1 ? a : b })
    };
    script.runInNewContext(context);
    return context.combined.weight_sum;
}
const close = (actual, expected, label) => assert.ok(Math.abs(actual - expected) < 1e-10,
    `${label}: expected ${expected}, got ${actual}`);
for (const p of [0, 0.001, 0.01, 0.1, 0.5, 1]) {
    for (const confidence of [1, 16, 64]) {
        let expectation = 0;
        for (const a of [0, 1]) for (const b of [0, 1]) {
            expectation += (a ? p : 1 - p) * (b ? p : 1 - p) * stream(a, b, true, true, confidence);
        }
        close(expectation, p, `temporal rare-light energy p=${p}, confidence=${confidence}`);
    }
}
close(stream(1, 0), 0.5, 'valid dark history retains its technique');
close(stream(1, 1, false, true), 0.5, 'forward occlusion does not skip backward evaluation');
close(stream(1, 1, true, false), 1.5, 'backward occlusion gives the canonical its full share');
close(stream(0, 1, false, false), 0, 'blocked history cannot light a dark pixel');
console.log('PASS temporal reuse: exhaustive rare-light energy, zero histories, both visibility directions');
