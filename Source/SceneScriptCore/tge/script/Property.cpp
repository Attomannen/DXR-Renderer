#include <stdafx.h>

#include "Property.h"
#include <tge/script/JsonData.h>

using namespace Tga;

constexpr size_t MaxTypeCount = 512;

static uint32_t locTypeCount;
static PropertyTypeBase* locType[MaxTypeCount];

static bool locIsRegistryFinalized;
static std::unordered_map<StringId, const PropertyTypeBase*> locStringIdToPropertyType;
static std::vector<StringId> locSortedPropertyNames;

PropertyTypeBase::PropertyTypeBase(const char* name)
{
	if (locIsRegistryFinalized)
		std::abort();

	if (locTypeCount == MaxTypeCount)
		std::abort();

	myName = name;
	myId = { locTypeCount };
	locType[locTypeCount] = this;
	locTypeCount++;
}

StringId PropertyTypeBase::GetName() const
{ 
	if (!locIsRegistryFinalized)
		FinalizeTypeRegistry();

	return myStringId; 
}

Property::Property(const Property& p)
{
	myType = p.myType;

	if (myType != nullptr)
	{
		myType->Copy(p.myBuffer, myBuffer);
	}
}

Property::Property(Property&& p) noexcept
{
	myType = p.myType;

	if (myType != nullptr)
	{
		myType->Move(p.myBuffer, myBuffer);
	}
}

Property::Property(const PropertyTypeBase* type)
{
	myType = type;

	if (myType != nullptr)
	{
		myType->CreateDefault(myBuffer);
	}
}

Property& Property::operator=(const Property& p)
{
	if (myType != nullptr)
	{
		myType->Destruct(myBuffer);
	}

	myType = p.myType;

	if (myType != nullptr)
	{
		myType->Copy(p.myBuffer, myBuffer);
	}

	return *this;
}
Property& Property::operator=(Property&& p)
{
	if (myType != nullptr)
	{
		myType->Destruct(myBuffer);
	}

	myType = p.myType;

	if (myType != nullptr)
	{
		myType->Move(p.myBuffer, myBuffer);
	}

	return *this;
}

Property::~Property()
{
	if (myType != nullptr)
	{
		myType->Destruct(myBuffer);
	}
}

Property Property::CreateFromJson(const JsonData& data)
{
	if (!data.json.contains("type") || !data.json.contains("value"))
	{
		return {};
	}

	std::string_view typeString = data.json["type"];
	StringId typeStringId = StringRegistry::RegisterOrGetString(typeString);
	const PropertyTypeBase* type = PropertyTypeRegistry::GetPropertyType(typeStringId);

	Property p;
	p.myType = type;
	type->LoadFromJson(p.myBuffer, { data.json["value"] });

	return p;
}

Property Property::CreateFromJson(const PropertyTypeBase* type, const JsonData& value)
{
	Property p;
	p.myType = type;
	type->LoadFromJson(p.myBuffer, value);

	return p;
}

void Property::WriteToJson(JsonData& data) const
{
	if (myType == nullptr)
		return;

	data.json["type"] = myType->GetName().GetString();

	JsonData value;
	myType->WriteToJson(myBuffer, value);

	data.json["value"] = value.json;
}

void Property::WriteToJsonWithoutType(JsonData& data) const
{
	myType->WriteToJson(myBuffer, data);
}

bool Property::ShowImGuiEditor(const char* text, const char* description)
{
	return myType->ShowImGuiEditor(myBuffer, text, description);
}

void Tga::FinalizeTypeRegistry()
{
	locIsRegistryFinalized = true;

	locSortedPropertyNames.reserve(locTypeCount);

	for (int i = 0; i < (int)locTypeCount; i++)
	{
		StringId stringId = StringRegistry::RegisterOrGetString(locType[i]->myName);
		locType[i]->myStringId = stringId;
		locStringIdToPropertyType[stringId] = locType[i];

		locSortedPropertyNames.push_back(stringId);
	}

	std::sort(locSortedPropertyNames.begin(), locSortedPropertyNames.end());
}

const PropertyTypeBase* PropertyTypeRegistry::GetPropertyType(StringId string)
{
	if (!locIsRegistryFinalized)
		FinalizeTypeRegistry();

	auto it = locStringIdToPropertyType.find(string);
	if (it == locStringIdToPropertyType.end())
		return nullptr;

	return it->second;
}

std::span<StringId> PropertyTypeRegistry::GetAllPropertyNames()
{
	if (!locIsRegistryFinalized)
		FinalizeTypeRegistry();

	return locSortedPropertyNames;
}