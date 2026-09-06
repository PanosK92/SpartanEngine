// Execute the production spatial streaming block with scalar light-path fixtures.
// This checks its actual neighbor eligibility and MIS arithmetic, not a second estimator.
// Texture fetches, visibility results and random selection are supplied by the fixture;
// shader compilation separately checks HLSL types and the full GPU entry points.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const source = fs.readFileSync(process.argv[2] ?? 'data/shaders/restir_pt_spatial.hlsl', 'utf8');
const begin = source.indexOf('    float center_M =');
const end = source.indexOf('    clamp_reservoir_M(combined, get_restir_m_cap());', begin);
assert.ok(begin >= 0 && end > begin, 'production streaming block was not found');
const program = source.slice(begin, end)
    .replace(/\/\/[^\n]*/g, '')
    .replace(/\b(?:float[234]?|uint|bool|int2|PathSample|Reservoir)\s+(\w+)\s*\[RESTIR_PAIRING_COUNT\];/g,
        'let $1 = new Array(RESTIR_PAIRING_COUNT);')
    .replace(/\b(?:float[234]?|uint|bool|int2|PathSample|Reservoir)\s+(\w+)\s*=/g, 'let $1 =')
    .replace(/\b(\d+)u\b/g, '$1')
    .replace(/\b(\d+(?:\.\d+)?)f\b/g, '$1');
const script = new vm.Script(program);

const reservoir = (radiance, seed = 1, confidence = 1) => ({
    M: confidence, W: radiance > 0 ? 1 : 0, target_pdf: radiance,
    sample: { seed_path: seed }
});
function stream(center, neighbors, forward, backward) {
    const context = {
        center, target_cur: center.target_pdf, combined: {}, seed: 0,
        pixel: -1, pos_ws: 0, normal_ws: 0, linear_depth: 1, resolution: 100,
        RESTIR_PAIRING_COUNT: neighbors.length,
        max: Math.max, float: Number, int2: Number,
        restir_pairing_partner: (_, t) => t,
        is_neighbor_gbuffer_compatible: () => true,
        is_reservoir_valid: () => true,
        target_scalar: value => value,
        random_float: () => 0.5,
        read_shift: (t, pixel) => pixel < 0 ? backward[t] : forward[t],
        unpack_reservoir: index => neighbors[index],
        tex_reservoir_prev0: neighbors.map((_, i) => i),
        tex_reservoir_prev1: [], tex_reservoir_prev2: [],
        tex_reservoir_prev3: [], tex_reservoir_prev4: []
    };
    script.runInNewContext(context);
    return context.combined.weight_sum;
}
const shift = radiance => ({ rgb: radiance, a: radiance > 0 ? 1 : 0 });
const close = (actual, expected, label) => assert.ok(Math.abs(actual - expected) < 1e-10,
    `${label}: expected ${expected}, got ${actual}`);

// A rare lit path is found with probability p on an otherwise dark surface.
// Exhaustively integrate all independent draw combinations: reuse must preserve p.
for (const p of [0, 0.001, 0.01, 0.1, 0.5, 1]) {
    for (const count of [1, 3]) {
        let expectation = 0;
        for (let bits = 0; bits < (1 << (count + 1)); bits++) {
            const draws = Array.from({ length: count + 1 }, (_, i) => (bits >> i) & 1);
            const probability = draws.reduce((v, draw) => v * (draw ? p : 1 - p), 1);
            const neighbors = draws.slice(1).map((draw, i) => reservoir(draw, i + 2));
            expectation += probability * stream(reservoir(draws[0]), neighbors,
                draws.slice(1).map(shift), neighbors.map(() => shift(draws[0])));
        }
        close(expectation, p, `rare-light energy p=${p}, neighbors=${count}`);
    }
}

close(stream(reservoir(1), [reservoir(0, 2)], [shift(0)], [shift(1)]), 0.75,
    'a valid zero draw retains its technique in the canonical denominator');
close(stream(reservoir(1), [reservoir(1, 2)], [shift(0)], [shift(1)]), 0.75,
    'an occluded forward draw does not discard the independently valid backward density');
close(stream(reservoir(0), [reservoir(1, 2)], [shift(0)], [shift(0)]), 0,
    'a dark pixel cannot receive an occluded path');
close(stream(reservoir(1), [reservoir(1)], [shift(1)], [shift(1)]), 1,
    'duplicate paths preserve constant illumination');
console.log('PASS spatial reuse: exhaustive rare-light energy, zero draws, occlusion, duplicate paths');
