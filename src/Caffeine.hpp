#pragma once

#include "core/Types.hpp"
#include "core/Platform.hpp"
#include "core/Compiler.hpp"
#include "core/Assertions.hpp"

#include "core/io/CafTypes.hpp"
#include "core/io/Crc32.hpp"
#include "core/io/BlobLoader.hpp"
#include "core/io/CafWriter.hpp"

#include "memory/Allocator.hpp"
#include "memory/LinearAllocator.hpp"
#include "memory/PoolAllocator.hpp"
#include "memory/StackAllocator.hpp"

#include "containers/Vector.hpp"
#include "containers/HashMap.hpp"
#include "containers/StringView.hpp"
#include "containers/FixedString.hpp"

#include "math/Vec2.hpp"
#include "math/Vec3.hpp"
#include "math/Vec4.hpp"
#include "math/Mat4.hpp"
#include "math/Math.hpp"