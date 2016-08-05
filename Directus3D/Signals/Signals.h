/*
Copyright(c) 2016 Panos Karabelas

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

#pragma once

//= SIGNAL IDs ======================
#define SIGNAL_ENGINE_INITIALIZED	0 // Fired when engine get's initialized, happens only once.
#define SIGNAL_ENGINE_START			1 // Fired when the engine starts, can happen at any time (e.g. Button play get's clicked in the editor).
#define SIGNAL_ENGINE_STOP			2 // Fired when the engine stops.
#define SIGNAL_FRAME_START			3 // Fired when an engine cycle starts.
#define SIGNAL_FRAME_END			4 // Fired when an engine cycle ends.
#define SIGNAL_RENDER_START			5 // Fired when the starts.
#define SIGNAL_RENDER_END			6 // Fired when the rendering ends.
#define SIGNAL_PHYSICS_STEP			7 // Fired when the physics get stepped (updated).
#define SIGNAL_TRANSFORM_UPDATED	8 // Fired when any transform updates.
//====================================