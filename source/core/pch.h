/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
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
