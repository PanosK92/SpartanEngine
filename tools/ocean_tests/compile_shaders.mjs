/*
Copyright(c) 2015-2026 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';

fs.mkdirSync('binaries/ocean_tests', { recursive: true });
// vcvars puts the Windows SDK's DXIL-only compiler first on PATH.
const dxc = process.env.VULKAN_SDK ? path.join(process.env.VULKAN_SDK, 'Bin', 'dxc.exe') : 'dxc';
const cases = [
    ['ocean/ocean_spectrum.hlsl', 'cs', 'INIT'], ['ocean/ocean_spectrum.hlsl', 'cs', 'UPDATE'],
    ['ocean/ocean_fft.hlsl', 'cs', 'HORIZONTAL'], ['ocean/ocean_fft.hlsl', 'cs', 'VERTICAL'],
    ['ocean/ocean_assemble.hlsl', 'cs', ''], ['g_buffer.hlsl', 'ps', ''], ['g_buffer.hlsl', 'vs', ''],
    ['reflections_apply.hlsl', 'cs', ''], ['light_composition.hlsl', 'cs', ''],
    ['fog_froxel.hlsl', 'cs', 'FOG_INJECT'], ['fog_froxel.hlsl', 'cs', 'FOG_INTEGRATE'],
    ['underwater.hlsl', 'cs', '']
];
for (const [file, stage, define] of cases) {
    const args = ['-T', `${stage}_6_7`, '-E', `main_${stage}`, '-spirv', '-fspv-target-env=vulkan1.3',
        '-fvk-use-dx-layout', '-Fo', `binaries/ocean_tests/${file.replaceAll('/', '_')}_${stage}_${define}.spv`];
    if (stage === 'cs') args.push('-D', 'SP_SHADER_STAGE_COMPUTE=1');
    if (define) args.push('-D', define);
    args.push(`data/shaders/${file}`);
    const result = spawnSync(dxc, args, { encoding: 'utf8' });
    if (result.status !== 0) {
        console.error(result.error ?? result.stderr);
        process.exit(1);
    }
    console.log(`PASS ${file} ${stage} ${define}`);
}
