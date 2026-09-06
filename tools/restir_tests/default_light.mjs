import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const source = fs.readFileSync(process.argv[2] ?? 'source/rendering/Renderer.cpp', 'utf8');
const assignment = source.indexOf('sun.intensity         = 85000.0f / 683.0f');
assert.ok(assignment >= 0, 'default sun block not found');
const start = source.lastIndexOf('if (', assignment);
const body = source.indexOf('{', start);
const condition = source.slice(start + 4, body).trim().replace(/\)$/, '')
    .replaceAll('World::GetFilePath().empty()', 'worldPath.length === 0')
    .replaceAll('light_entities().empty()', 'authoredLightCount === 0');
const fixtures = [
    {name:'empty editor', worldPath:'', authoredLightCount:0, first_directional:false, m_count_active_lights:0, expected:true},
    {name:'loaded unlit room', worldPath:'closed_room.world', authoredLightCount:0, first_directional:false, m_count_active_lights:0, expected:false},
    {name:'local lights outside view', worldPath:'liminal_space.world', authoredLightCount:6, first_directional:false, m_count_active_lights:0, expected:false},
    {name:'unsaved scene with culled lights', worldPath:'', authoredLightCount:6, first_directional:false, m_count_active_lights:0, expected:false},
    {name:'disabled authored sun', worldPath:'night.world', authoredLightCount:1, first_directional:true, m_count_active_lights:0, expected:false},
    {name:'visible point light', worldPath:'liminal_space.world', authoredLightCount:6, first_directional:false, m_count_active_lights:1, expected:false},
];
for (const fixture of fixtures) {
    const actual = new vm.Script(`Boolean(${condition})`).runInNewContext(fixture);
    assert.equal(actual, fixture.expected, fixture.name);
}
console.log('PASS default sun: authored dark/culled/disabled lighting never creates an editor light');

// Execute the production upload block with a stale sun in slot 0. A zero count must
// upload a zero-intensity sentinel; just clearing the CPU array leaves the GPU lit.
const uploadStart = source.indexOf('buffer->ResetOffset();', assignment);
const uploadEnd = source.indexOf('// upload the compact volumetric', uploadStart);
assert.ok(uploadStart >= 0 && uploadEnd > uploadStart, 'light upload block not found');
const upload = source.slice(uploadStart, uploadEnd)
    .replaceAll('buffer->', 'buffer.').replaceAll('&m_bindless_lights[0]', 'm_bindless_lights[0]')
    .replace(/(\d+\.\d+)f/g, '$1').replace(/(\d+)u\b/g, '$1');
for (const count of [0, 1, 6]) {
    let uploaded;
    const context = {
        m_count_active_lights: count,
        m_bindless_lights: [{intensity: 125, flags: 1}],
        Sb_Light: () => ({intensity: 0, flags: 0}),
        Vector3: (x,y,z) => [x,y,z], max: Math.max,
        buffer: {ResetOffset() {}, GetStride: () => 128,
            Update: (slot, bytes) => { uploaded = {slot: structuredClone(slot), bytes}; }},
    };
    new vm.Script(upload).runInNewContext(context);
    assert.ok(uploaded, 'zero-light frames must still upload slot 0');
    assert.equal(uploaded.bytes, Math.max(count, 1) * 128);
    assert.equal(uploaded.slot.intensity, count === 0 ? 0 : 125);
    if (count === 0) {
        assert.equal(uploaded.slot.flags, 0);
        assert.equal(Math.hypot(...uploaded.slot.direction), 1, 'sky direction must remain finite and nonzero');
    }
}
console.log('PASS light upload: zero-light transition clears the GPU slot and preserves a valid sky direction');
