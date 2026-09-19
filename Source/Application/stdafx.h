#pragma once
#pragma message("-------AttoEngine-------------")
#include "targetver.h"
#include <age/log/Log.h>

#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif

#include <age/math/matrix4x4.h>
#include <age/math/vector2.h>
#include <age/math/vector4.h>

#include <age/settings/settings.h>

#include <algorithm>
#include <array>
#include <exception>
#include <malloc.h>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "imgui/imgui.h"
