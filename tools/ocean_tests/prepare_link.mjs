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

import fs from 'node:fs';

// Reuse the isolated engine build's libraries and objects, replacing only its entry point.
const log = fs.readFileSync('binaries/ocean_tests/obj/spartan.tlog/link.command.1.tlog', 'utf16le');
let args = log.split(/\r?\n/).map(line => line.trim()).filter(line => line && !line.startsWith('^')).join('\n');
if (!/BINARIES\/OCEAN_TESTS\/OBJ\/MAIN\.OBJ/i.test(args)) throw new Error('Engine main object not found');
args = args.replace(/BINARIES\/OCEAN_TESTS\/OBJ\/MAIN\.OBJ/ig, 'binaries/ocean_tests/component_tests.obj')
    .replaceAll('SPARTAN_OCEAN_VALIDATION', 'SPARTAN_OCEAN_COMPONENT_TESTS')
    .replace('/SUBSYSTEM:WINDOWS', '/SUBSYSTEM:CONSOLE /ENTRY:WinMainCRTStartup');
fs.writeFileSync('binaries/ocean_tests/component_link.rsp', args);
