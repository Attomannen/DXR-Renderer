#pragma once

#include <age/script/Property.h>
#include <age/script/BaseProperties.h>
#include <age/script/CopyOnWriteWrapper.h>
#include <age/math/Vector.h>
#include <age/stringRegistry/StringRegistry.h>

#include <utility>
#include <vector>

namespace Ag
{
	// Arrays and string-keyed maps for scripts, like Blueprint's Array and Map variables. There is one property type per
	// element type ("Float Array", "String Map", ...), because a pin has exactly one type.
	//
	// They have value semantics (assigning copies, cheaply, until one side is edited). The nodes that change an array in
	// place (Add, Remove, Sort...) work on a variable by name; the others return a new array.

	// bool is stored as unsigned char: std::vector<bool> hands out proxies, not references.
	template <typename T> struct ArrayStorage { using Type = T; };
	template <> struct ArrayStorage<bool> { using Type = unsigned char; };

	template <typename T>
	struct ScriptArray
	{
		std::vector<typename ArrayStorage<T>::Type> items;
	};

	template <typename T>
	struct ScriptMap
	{
		std::vector<std::pair<StringId, typename ArrayStorage<T>::Type>> entries;   // in insertion order
	};

	template <typename T> using ArrayValue = CopyOnWriteWrapper<ScriptArray<T>>;
	template <typename T> using MapValue = CopyOnWriteWrapper<ScriptMap<T>>;

	// Equality for the element types (Vector3f has no operator== we can rely on).
	template <typename T> inline bool ElementEqual(const T& a, const T& b) { return a == b; }
	template <> inline bool ElementEqual<Vector3f>(const Vector3f& a, const Vector3f& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

	template <typename T> inline T FromStored(const typename ArrayStorage<T>::Type& stored) { return static_cast<T>(stored); }
	template <typename T> inline typename ArrayStorage<T>::Type ToStored(const T& value) { return static_cast<typename ArrayStorage<T>::Type>(value); }
	template <> inline Vector3f FromStored<Vector3f>(const Vector3f& stored) { return stored; }
	template <> inline Vector3f ToStored<Vector3f>(const Vector3f& value) { return value; }
	template <> inline StringId FromStored<StringId>(const StringId& stored) { return stored; }
	template <> inline StringId ToStored<StringId>(const StringId& value) { return value; }

	DECLARE_PROPERTY_TYPE(CopyOnWriteWrapper<ScriptArray<int>>)
	DECLARE_PROPERTY_TYPE(CopyOnWriteWrapper<ScriptArray<float>>)
	DECLARE_PROPERTY_TYPE(CopyOnWriteWrapper<ScriptArray<bool>>)
	DECLARE_PROPERTY_TYPE(CopyOnWriteWrapper<ScriptArray<StringId>>)
	DECLARE_PROPERTY_TYPE(CopyOnWriteWrapper<ScriptArray<Vector3f>>)

	DECLARE_PROPERTY_TYPE(CopyOnWriteWrapper<ScriptMap<int>>)
	DECLARE_PROPERTY_TYPE(CopyOnWriteWrapper<ScriptMap<float>>)
	DECLARE_PROPERTY_TYPE(CopyOnWriteWrapper<ScriptMap<bool>>)
	DECLARE_PROPERTY_TYPE(CopyOnWriteWrapper<ScriptMap<StringId>>)
	DECLARE_PROPERTY_TYPE(CopyOnWriteWrapper<ScriptMap<Vector3f>>)
}
