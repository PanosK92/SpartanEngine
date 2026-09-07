import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';

const out = 'binaries/payload_tests';
fs.mkdirSync(out, {recursive: true});
const tool = name => process.env.VULKAN_SDK ? path.join(process.env.VULKAN_SDK, 'Bin', `${name}.exe`) : name;
function run(name, args) {
    const result = spawnSync(tool(name), args, {encoding: 'utf8'});
    assert.equal(result.status, 0, `${name}: ${result.error ?? result.stderr}`);
    return result.stdout;
}
for (const shader of ['restir_pt', 'reflections_trace', 'ray_traced_shadows']) {
    for (const debug of shader === 'reflections_trace' ? [false, true] : [false]) {
        for (const api of ['vulkan', 'd3d12']) {
            const name = `${shader}_${api}${debug ? '_debug' : ''}`;
            const args = ['-T', 'lib_6_8', '-Werror=payload-access-perf', '-Werror=payload-access-trace', '-D', 'RAY_TRACING_ENABLED', '-D', `DEBUG_RAY_TRACING=${Number(debug)}`];
            if (api === 'vulkan') {
                args.push('-spirv', '-fspv-target-env=vulkan1.3', '-fvk-use-dx-layout', '-fspv-preserve-bindings',
                    '-fvk-u-shift', '100', 'all', '-fvk-b-shift', '200', 'all',
                    '-fvk-t-shift', '300', 'all', '-fvk-s-shift', '400', 'all');
            } else {
                args.push('-D', 'API_D3D12=1', '-Wno-ignored-attributes', '-flegacy-macro-expansion');
            }
            const binary = `${out}/${name}.${api === 'vulkan' ? 'spv' : 'dxil'}`;
            run('dxc', [...args, '-Fo', binary, `data/shaders/${shader}.hlsl`]);
            if (api === 'vulkan') {
                run('spirv-val', ['--target-env', 'vulkan1.3', '--scalar-block-layout', binary]);
                const assembly = run('spirv-dis', [binary]);
                if (shader !== 'ray_traced_shadows') {
                    assert.match(assembly, debug ? /%HitPayload = OpTypeStruct %float\s*$/m : /%HitPayload = OpTypeStruct %float %uint %uint %uint/);
                    assert.match(assembly, /OpVariable %_ptr_RayPayloadKHR_HitPayload RayPayloadKHR/);
                    if (!debug) {
                        assert.match(assembly, /OpDecorate %_runtimearr_GeometryInfo ArrayStride 144/);
                        for (const [member, offset] of [[7, 40], [9, 48], [10, 64], [11, 80], [12, 96], [13, 112], [14, 128]]) {
                            assert.match(assembly, new RegExp(`OpMemberDecorate %GeometryInfo ${member} Offset ${offset}\\b`));
                        }
                    }
                }
            }
            console.log(`PASS ${name}`);
        }
    }
}
