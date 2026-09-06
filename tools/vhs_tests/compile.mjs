import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';

fs.mkdirSync('binaries/vhs_tests', { recursive: true });
const dxc = process.env.VULKAN_SDK ? path.join(process.env.VULKAN_SDK, 'Bin', 'dxc.exe') : 'dxc';
for (const spirv of [true, false]) {
    const args = ['-T', 'cs_6_7', '-E', 'main_cs', '-D', 'SP_SHADER_STAGE_COMPUTE=1', '-WX'];
    if (spirv) args.push('-spirv', '-fspv-target-env=vulkan1.3', '-fvk-use-dx-layout',
        '-fspv-preserve-bindings', '-fvk-u-shift', '100', 'all', '-fvk-b-shift', '200', 'all',
        '-fvk-t-shift', '300', 'all', '-fvk-s-shift', '400', 'all');
    else args.push('-Wno-ignored-attributes'); // the shared bindings also carry Vulkan annotations
    args.push('-Fo', `binaries/vhs_tests/vhs.${spirv ? 'spv' : 'dxil'}`, 'data/shaders/vhs.hlsl');
    const result = spawnSync(dxc, args, { encoding: 'utf8' });
    if (result.status !== 0) throw new Error(result.error ?? result.stderr);
    console.log(`PASS production VHS shader: ${spirv ? 'Vulkan SPIR-V' : 'DXIL'}`);
}
