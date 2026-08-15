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

// memory
#include "../memory/MemoryOverrides.h"

// std (lean, ubiquitous-only)
#include <string>
#include <algorithm>
#include <type_traits>
#include <memory>
#include <limits>
#include <cassert>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <map>
#include <unordered_map>
#include <set>
#include <cstdio>
#include <array>
#include <vector>
#include <cstdarg>
#include <cstring>
#include <chrono>
#include <utility>
//===========================

// common
#include "Definitions.h"
#include "Engine.h"
#include "Event.h"
#include "Settings.h"
#include "Timer.h"
#include "../file_system/FileSystem.h"
#include "Stopwatch.h"
#include "../logging/Log.h"

// math
#include "../math/Vector2.h"
#include "../math/Vector3.h"
#include "../math/Vector4.h"
#include "../math/Ray.h"
#include "../math/RayHitResult.h"
#include "../math/Rectangle.h"
#include "../math/BoundingBox.h"
#include "../math/Sphere.h"
#include "../math/Matrix.h"
#include "../math/Frustum.h"
#include "../math/Plane.h"
#include "../math/Helper.h"
