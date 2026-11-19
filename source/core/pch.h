/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

// xcode prefixes every source with this header, the c sources (mimalloc, lz4, lzma) must not see it
#ifdef __cplusplus

// memory
#include "../memory/MemoryOverrides.h"

// std (lean, ubiquitous-only)
#include <string>
#include <string_view>
#include <algorithm>
#include <type_traits>
#include <memory>
#include <limits>
#include <cassert>
#include <cstdint>
#include <cctype>
#include <cmath>
#include <bit>
#include <numeric>
#include <optional>
#include <functional>
#include <atomic>
#include <mutex>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <cstdio>
#include <array>
#include <vector>
#include <cstdarg>
#include <cstring>
#include <chrono>
#include <utility>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <future>
#include <any>
#include <span>
#include <memory_resource>
#include <deque>
//===========================

// common
#include "Portability.h"
#include "Definitions.h"
#include "Engine.h"
#include "Event.h"
#include "Settings.h"
#include "Timer.h"
#include "../file_system/FileSystem.h"
#include "Stopwatch.h"
#include "../logging/Log.h"
SP_WARNINGS_OFF
#include "../io/pugixml.hpp"
SP_WARNINGS_ON

// math
#include "../math/Vector2.h"
#include "../math/Vector3.h"
#include "../math/Vector4.h"
#include "../math/Quaternion.h"
#include "../math/Ray.h"
#include "../math/RayHitResult.h"
#include "../math/Rectangle.h"
#include "../math/BoundingBox.h"
#include "../math/Matrix.h"
#include "../math/Frustum.h"
#include "../math/Plane.h"
#include "../math/Helper.h"

#endif // __cplusplus
