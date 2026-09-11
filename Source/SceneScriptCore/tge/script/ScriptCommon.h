#pragma once

#include <variant>
#include <tge/stringRegistry/StringRegistry.h>
#include <tge/math/Vector.h>
#include <tge/math/color.h>
#include <tge/script/Property.h>

namespace Tga
{
	class ScriptRuntimeInstance;
	struct ScriptUpdateContext;
	class ScriptCreationContext;
	class ScriptExecutionContext;
	class ScriptNodeBase;
	struct JsonData;

	// TODO: setup .natvis for all the common ID types
	// might need to add a Script pointer to IDs for debugging purposes


	enum class ScriptLinkType
	{
		Unknown,
		Flow,
		Property,
		Count
	};
	constexpr const char* ScriptLinkTypeNames[(size_t)ScriptLinkType::Count] =
	{
		"Unknown",
		"Flow",
		"Property",
	};

	struct ScriptPinId
	{
		constexpr static unsigned int InvalidId = 0xFFFFFFFF;
		unsigned int id = InvalidId;

		bool operator==(const ScriptPinId& other) const { return this->id == other.id; }
		bool operator!=(const ScriptPinId& other) const { return this->id != other.id; }
	};

	struct ScriptNodeId
	{
		constexpr static unsigned int InvalidId = 0xFFFFFFFF;
		unsigned int id = InvalidId;

		bool operator==(const ScriptNodeId& other) const { return this->id == other.id; }
		bool operator!=(const ScriptNodeId& other) const { return this->id != other.id; }
	};

	struct ScriptLinkId
	{
		constexpr static unsigned int InvalidId = 0xFFFFFFFF;
		unsigned int id = InvalidId;

		bool operator==(const ScriptLinkId& other) const { return this->id == other.id; }
		bool operator!=(const ScriptLinkId& other) const { return this->id != other.id; }
	};

	struct ScriptNodeTypeId
	{
		constexpr static unsigned int InvalidId = 0xFFFFFFFF;
		unsigned int id = InvalidId;

		bool operator==(const ScriptNodeTypeId& other) const { return this->id == other.id; }
		bool operator!=(const ScriptNodeTypeId& other) const { return this->id != other.id; }
	};

	enum class ScriptNodeResult
	{
		Finished,
		KeepRunning,
	};

	struct ScriptLink
	{
		ScriptPinId sourcePinId;
		ScriptPinId targetPinId;
	};

	enum class ScriptPinRole
	{
		Input,
		Output,
	};

	struct ScriptPin
	{
		ScriptPinRole role;
		int sortingNumber;
		ScriptNodeId node;
		StringId name;
		ScriptLinkType type;
		const PropertyTypeBase* dataType;

		Property defaultValue;
		Property overridenValue;
	};
} // namespace Tga


template<>
struct std::hash<Tga::ScriptPinId>
{
	std::size_t operator()(Tga::ScriptPinId const& id) const noexcept
	{
		return id.id;
	}
};

template<>
struct std::hash<Tga::ScriptNodeId>
{
	std::size_t operator()(Tga::ScriptNodeId const& id) const noexcept
	{
		return id.id;
	}
};

template<>
struct std::hash<Tga::ScriptLinkId>
{
	std::size_t operator()(Tga::ScriptLinkId const& id) const noexcept
	{
		return id.id;
	}
};

template<>
struct std::hash<Tga::ScriptNodeTypeId>
{
	std::size_t operator()(Tga::ScriptNodeTypeId const& id) const noexcept
	{
		return id.id;
	}
};

