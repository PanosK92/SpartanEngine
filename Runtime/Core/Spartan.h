/*
Copyright(c) 2016-2022 Panos Karabelas

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

// Engine macros
#include "SpartanDefinitions.h"

//= STD ================
#include <string>
#include <algorithm>
#include <type_traits>
#include <memory>
#include <fstream>
#include <sstream>
#include <limits>
#include <cassert>
#include <cstdint>
#include <array>
#include <atomic>
#include <map>
#include <unordered_map>
//======================

//= RUNTIME ====================
// Core
#include "Engine.h"
#include "EventSystem.h"
#include "Settings.h"
#include "Context.h"
#include "Timer.h"
#include "FileSystem.h"
#include "Stopwatch.h"

// Logging
#include "Runtime/Logging/Log.h"

// Math
#include "Runtime/Math/Vector2.h"
#include "Runtime/Math/Vector3.h"
#include "Runtime/Math/Vector4.h"
#include "Runtime/Math/Ray.h"
#include "Runtime/Math/RayHit.h"
#include "Runtime/Math/Rectangle.h"
#include "Runtime/Math/BoundingBox.h"
#include "Runtime/Math/Sphere.h"
#include "Runtime/Math/Matrix.h"
#include "Runtime/Math/Frustum.h"
#include "Runtime/Math/Plane.h"
#include "Runtime/Math/MathHelper.h"
//==============================
