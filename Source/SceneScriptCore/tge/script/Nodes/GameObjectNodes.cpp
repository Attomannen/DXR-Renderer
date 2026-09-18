#include <stdafx.h>
#include "GameObjectNodes.h"

#include <tge/script/ScriptNodeTypeRegistry.h>
#include <tge/stringRegistry/StringRegistry.h>
#include <tge/script/ScriptCommon.h>
#include <tge/script/ScriptNodeBase.h>
#include <tge/script/Contexts/GameScriptContext.h>
#include <tge/script/BaseProperties.h>

using namespace Tga;

namespace
{
	GameScriptContext* GetGameContext(ScriptExecutionContext& context)
	{
		return dynamic_cast<GameScriptContext*>(&context.GetUpdateContext());
	}

	ScriptPinId AddPin(const ScriptCreationContext& context, ScriptPinRole role, ScriptLinkType type, const char* name, const PropertyTypeBase* dataType = nullptr)
	{
		ScriptPin pin = {};
		pin.type = type;
		pin.role = role;
		pin.dataType = dataType;
		pin.name = StringRegistry::RegisterOrGetString(name);
		pin.node = context.GetNodeId();
		return context.FindOrCreatePin(pin);
	}

	ScriptPinId AddVector3Input(const ScriptCreationContext& context, const char* name)
	{
		ScriptPin pin = {};
		pin.type = ScriptLinkType::Property;
		pin.role = ScriptPinRole::Input;
		pin.dataType = GetPropertyType<Vector3f>();
		pin.name = StringRegistry::RegisterOrGetString(name);
		pin.node = context.GetNodeId();
		pin.defaultValue = Property::Create<Vector3f>(Vector3f{ 0.f, 0.f, 0.f });
		return context.FindOrCreatePin(pin);
	}
}

class GetLocationNode : public ScriptNodeBase
{
public:
	void Init(const ScriptCreationContext& context) override
	{
		AddPin(context, ScriptPinRole::Output, ScriptLinkType::Property, "Location", GetPropertyType<Vector3f>());
	}

	Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
	{
		const GameScriptContext* game = GetGameContext(context);
		return Property::Create<Vector3f>(game ? game->GetLocation() : Vector3f{ 0.f, 0.f, 0.f });
	}
};

class SetLocationNode : public ScriptNodeBase
{
	ScriptPinId myInPin, myLocationPin, myOutPin;

public:
	void Init(const ScriptCreationContext& context) override
	{
		myInPin = AddPin(context, ScriptPinRole::Input, ScriptLinkType::Flow, "Set");
		myLocationPin = AddVector3Input(context, "Location");
		myOutPin = AddPin(context, ScriptPinRole::Output, ScriptLinkType::Flow, "Out");
	}

	ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId) const override
	{
		if (GameScriptContext* game = GetGameContext(context))
			game->SetLocation(*context.ReadInputPin(myLocationPin).Get<Vector3f>());
		context.TriggerOutputPin(myOutPin);
		return ScriptNodeResult::Finished;
	}
};

class AddImpulseNode : public ScriptNodeBase
{
	ScriptPinId myInPin, myImpulsePin, myOutPin;

public:
	void Init(const ScriptCreationContext& context) override
	{
		myInPin = AddPin(context, ScriptPinRole::Input, ScriptLinkType::Flow, "Add Impulse");
		myImpulsePin = AddVector3Input(context, "Impulse (kg m/s)");
		myOutPin = AddPin(context, ScriptPinRole::Output, ScriptLinkType::Flow, "Out");
	}

	ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId) const override
	{
		GameScriptContext* game = GetGameContext(context);
		if (game && game->HasPhysicsBody())
			game->AddImpulse(*context.ReadInputPin(myImpulsePin).Get<Vector3f>());
		context.TriggerOutputPin(myOutPin);
		return ScriptNodeResult::Finished;
	}
};

class GetDeltaTimeNode : public ScriptNodeBase
{
public:
	void Init(const ScriptCreationContext& context) override
	{
		AddPin(context, ScriptPinRole::Output, ScriptLinkType::Property, "Delta Seconds", GetPropertyType<float>());
	}

	Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
	{
		return Property::Create<float>(context.GetUpdateContext().deltaTime);
	}
};

void Tga::RegisterGameObjectNodes()
{
	ScriptNodeTypeRegistry::RegisterType<GetLocationNode>("Object/Get Location", "The location of the object this script runs on (cm)");
	ScriptNodeTypeRegistry::RegisterType<SetLocationNode>("Object/Set Location", "Moves the object this script runs on (cm)");
	ScriptNodeTypeRegistry::RegisterType<AddImpulseNode>("Object/Add Impulse", "Gives the object's physics body a push, in kg m/s. Needs a Rigidbody and a running simulation");
	ScriptNodeTypeRegistry::RegisterType<GetDeltaTimeNode>("Common/Get Delta Time", "Seconds since the previous frame");
}
