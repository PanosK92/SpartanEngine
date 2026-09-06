import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const source = fs.readFileSync(process.argv[2] ?? 'data/shaders/restir_reservoir.hlsl', 'utf8');
const body = source.match(/float restir_primary_footprint_sq\([^)]*\)\s*\{([^}]+)\}/)[1]
    .replace(/\bfloat3?\s+(\w+)\s*=/g, 'let $1 =')
    .replace(/\b(\d+(?:\.\d+)?(?:e-\d+)?)f\b/g, '$1');
// Axis-aligned camera displacement, with primary_normal representing its cosine to the view.
const script = new vm.Script(`result = (() => { ${body} })();`);
for (const distance of [1, 10, 100]) for (const cosine of [1, 0.5, 0.1]) {
    const context = { primary_pos: 0, primary_normal: cosine, get_camera_position: () => distance,
        dot: (a, b) => a * b, normalize: Math.sign, max: Math.max, PI: Math.PI };
    script.runInNewContext(context);
    // Lin et al. 2026, Eq. 5: |x0-x1|^2 / (cos(theta)/(4*pi)).
    const expected = 4 * Math.PI * distance * distance / cosine;
    assert.ok(Math.abs(context.result / expected - 1) < 1e-12,
        `Eq. 5 at distance ${distance}, cosine ${cosine}: ${context.result} != ${expected}`);
}
console.log('PASS primary footprint: paper Eq. 5 at nine distances/angles');
