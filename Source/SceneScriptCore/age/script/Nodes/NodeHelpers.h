#pragma once

#include <age/script/ScriptNodeTypeRegistry.h>
#include <age/script/ScriptCommon.h>
#include <age/script/ScriptNodeBase.h>
#include <age/script/BaseProperties.h>
#include <age/script/Contexts/ScriptUpdateContext.h>
#include <age/stringRegistry/StringRegistry.h>

// Small helpers that keep node definitions short: declare a pin in one line, read a value in one line.
namespace Ag::NodeHelpers
{
	inline ScriptPinId FlowIn(const ScriptCreationContext& context, const char* name)
	{
		ScriptPin pin = {};
		pin.type = ScriptLinkType::Flow;
		pin.role = ScriptPinRole::Input;
		pin.name = StringRegistry::RegisterOrGetString(name);
		pin.node = context.GetNodeId();
		return context.FindOrCreatePin(pin);
	}

	inline ScriptPinId FlowOut(const ScriptCreationContext& context, const char* name)
	{
		ScriptPin pin = {};
		pin.type = ScriptLinkType::Flow;
		pin.role = ScriptPinRole::Output;
		pin.name = StringRegistry::RegisterOrGetString(name);
		pin.node = context.GetNodeId();
		return context.FindOrCreatePin(pin);
	}

	template <typename T>
	ScriptPinId In(const ScriptCreationContext& context, const char* name, const T& defaultValue = T{})
	{
		ScriptPin pin = {};
		pin.type = ScriptLinkType::Property;
		pin.role = ScriptPinRole::Input;
		pin.dataType = GetPropertyType<T>();
		pin.name = StringRegistry::RegisterOrGetString(name);
		pin.node = context.GetNodeId();
		pin.defaultValue = Property::Create<T>(defaultValue);
		return context.FindOrCreatePin(pin);
	}

	template <typename T>
	ScriptPinId Out(const ScriptCreationContext& context, const char* name)
	{
		ScriptPin pin = {};
		pin.type = ScriptLinkType::Property;
		pin.role = ScriptPinRole::Output;
		pin.dataType = GetPropertyType<T>();
		pin.name = StringRegistry::RegisterOrGetString(name);
		pin.node = context.GetNodeId();
		return context.FindOrCreatePin(pin);
	}

	// The value on an input pin, or T{} if the pin holds another type (which a correct graph never does).
	template <typename T>
	T Read(ScriptExecutionContext& context, ScriptPinId pin)
	{
		const Property property = context.ReadInputPin(pin);
		const T* value = property.Get<T>();
		return value ? *value : T{};
	}

	template <typename T>
	Property Make(const T& value)
	{
		return Property::Create<T>(value);
	}

	inline StringId MakeString(std::string_view text)
	{
		return StringRegistry::RegisterOrGetString(text);
	}
}
