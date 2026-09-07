// Regression for the terrain plane used by ocean/shore intersections.
// Evaluate the production HLSL expression against independent geometric planes.
import assert from 'node:assert/strict';
import fs from 'node:fs';

const source = fs.readFileSync('data/shaders/common.hlsl', 'utf8');
const expression = source.match(/float bed = ([\s\S]*?);/);
assert.ok(expression, 'The ocean must intersect terrain triangles, not a bilinear bed');
const height = new Function('h00', 'h10', 'h01', 'h11', 'f',
    `return ${expression[1].replace(/(\d)f\b/g, '$1')};`);

function planeHeight(a, b, c, x, z) {
    const u = b.map((v, i) => v - a[i]);
    const v = c.map((value, i) => value - a[i]);
    const n = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]];
    return a[1] - (n[0] * (x - a[0]) + n[2] * (z - a[2])) / n[1];
}

let checked = 0;
for (const heights of [[0, 0, 0, 100], [3, -2, 7, -4], [-5.9, -5.9, -5.9, -5.9], [0, 25, 25, 50]]) {
    const [h00, h10, h01, h11] = heights;
    const a = [0, h00, 0], b = [1, h10, 0], c = [0, h01, 1], d = [1, h11, 1];
    for (let ix = 0; ix <= 100; ix++) {
        for (let iz = 0; iz <= 100; iz++) {
            const x = ix / 100, z = iz / 100;
            const expected = x + z <= 1 ? planeHeight(a, b, c, x, z) : planeHeight(d, c, b, x, z);
            assert.ok(Math.abs(height(...heights, {x, y: z}) - expected) < 1e-10,
                `Incorrect shore plane at ${x}, ${z}`);
            checked++;
        }
    }
}
// In this saddle cell bilinear filtering gives 6.25 m: a second, false coast.
assert.equal(height(0, 0, 0, 100, {x: 0.25, y: 0.25}), 0);
console.log(`PASS ${checked} ocean-bed samples, including diagonal and tile edges`);
