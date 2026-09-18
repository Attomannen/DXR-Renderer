#include <stdafx.h>
#include "GameObjectNodes.h"

#include <tge/script/ScriptNodeTypeRegistry.h>
#include <tge/stringRegistry/StringRegistry.h>
#include <tge/script/ScriptCommon.h>
#include <tge/script/ScriptNodeBase.h>
#include <tge/script/Contexts/GameScriptContext.h>
#include <tge/script/BaseProperties.h>
#include <cctype>
#include <cstdlib>
#include <string>

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

class SetRotationNode : public ScriptNodeBase
{
	ScriptPinId myInPin, myRotationPin, myOutPin;

public:
	void Init(const ScriptCreationContext& context) override
	{
		myInPin = AddPin(context, ScriptPinRole::Input, ScriptLinkType::Flow, "Set");
		myRotationPin = AddVector3Input(context, "Rotation (degrees)");
		myOutPin = AddPin(context, ScriptPinRole::Output, ScriptLinkType::Flow, "Out");
	}

	ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId) const override
	{
		if (GameScriptContext* game = GetGameContext(context))
			game->SetRotation(*context.ReadInputPin(myRotationPin).Get<Vector3f>());
		context.TriggerOutputPin(myOutPin);
		return ScriptNodeResult::Finished;
	}
};

// Forward, right and up of the object as it is turned right now.
class GetDirectionsNode : public ScriptNodeBase
{
	ScriptPinId myForward, myRight, myUp;

public:
	void Init(const ScriptCreationContext& context) override
	{
		myForward = AddPin(context, ScriptPinRole::Output, ScriptLinkType::Property, "Forward", GetPropertyType<Vector3f>());
		myRight = AddPin(context, ScriptPinRole::Output, ScriptLinkType::Property, "Right", GetPropertyType<Vector3f>());
		myUp = AddPin(context, ScriptPinRole::Output, ScriptLinkType::Property, "Up", GetPropertyType<Vector3f>());
	}

	Property ReadPin(ScriptExecutionContext& context, ScriptPinId pin) const override
	{
		const GameScriptContext* game = GetGameContext(context);
		if (!game)
			return Property::Create<Vector3f>(Vector3f{ 0.f, 0.f, 0.f });
		return Property::Create<Vector3f>(pin == myForward ? game->GetForward() : (pin == myRight ? game->GetRight() : game->GetUp()));
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

class GetVelocityNode : public ScriptNodeBase
{
public:
	void Init(const ScriptCreationContext& context) override
	{
		AddPin(context, ScriptPinRole::Output, ScriptLinkType::Property, "Velocity", GetPropertyType<Vector3f>());
	}

	Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
	{
		const GameScriptContext* game = GetGameContext(context);
		return Property::Create<Vector3f>(game ? game->GetVelocity() : Vector3f{ 0.f, 0.f, 0.f });
	}
};

// keepVertical: leave the body's own up/down speed alone, so a walking character still falls and jumps.
template <bool keepVertical>
class SetVelocityNode : public ScriptNodeBase
{
	ScriptPinId myInPin, myVelocityPin, myOutPin;

public:
	void Init(const ScriptCreationContext& context) override
	{
		myInPin = AddPin(context, ScriptPinRole::Input, ScriptLinkType::Flow, keepVertical ? "Set Horizontal" : "Set");
		myVelocityPin = AddVector3Input(context, "Velocity (cm/s)");
		myOutPin = AddPin(context, ScriptPinRole::Output, ScriptLinkType::Flow, "Out");
	}

	ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId) const override
	{
		GameScriptContext* game = GetGameContext(context);
		if (game && game->HasPhysicsBody())
		{
			Vector3f velocity = *context.ReadInputPin(myVelocityPin).Get<Vector3f>();
			if (keepVertical)
				velocity.y = game->GetVelocity().y;
			game->SetVelocity(velocity);
		}
		context.TriggerOutputPin(myOutPin);
		return ScriptNodeResult::Finished;
	}
};

// Key names the input nodes accept: A-Z, 0-9, F1-F12, Space, Shift, Ctrl, Alt, Enter,
// Escape, Tab, Left/Right/Up/Down, "Mouse Left/Right/Middle". Case-insensitive.
static int KeyNameToCode(const char* name)
{
	std::string key = name ? name : "";
	for (char& c : key)
		c = (char)std::tolower((unsigned char)c);

	if (key.size() == 1)
	{
		const char c = key[0];
		if (c >= 'a' && c <= 'z') return 'A' + (c - 'a');
		if (c >= '0' && c <= '9') return c;
	}
	if (key.size() >= 2 && key.size() <= 3 && key[0] == 'f')
	{
		const int number = std::atoi(key.c_str() + 1);
		if (number >= 1 && number <= 12) return 0x6F + number;
	}
	static const struct { const char* name; int code; } kNamed[] = {
		{ "space", 0x20 }, { "shift", 0x10 }, { "ctrl", 0x11 }, { "control", 0x11 }, { "alt", 0x12 },
		{ "enter", 0x0D }, { "escape", 0x1B }, { "esc", 0x1B }, { "tab", 0x09 },
		{ "left", 0x25 }, { "up", 0x26 }, { "right", 0x27 }, { "down", 0x28 },
		{ "mouse left", 0x01 }, { "mouse right", 0x02 }, { "mouse middle", 0x04 },
	};
	for (const auto& entry : kNamed)
		if (key == entry.name)
			return entry.code;
	return 0;
}

static ScriptPinId AddKeyInput(const ScriptCreationContext& context, const char* name, const char* defaultKey)
{
	ScriptPin pin = {};
	pin.type = ScriptLinkType::Property;
	pin.role = ScriptPinRole::Input;
	pin.dataType = GetPropertyType<StringId>();
	pin.name = StringRegistry::RegisterOrGetString(name);
	pin.node = context.GetNodeId();
	pin.defaultValue = Property::Create<StringId>(StringRegistry::RegisterOrGetString(defaultKey));
	return context.FindOrCreatePin(pin);
}

static int ReadKey(ScriptExecutionContext& context, ScriptPinId pin)
{
	const StringId* key = context.ReadInputPin(pin).Get<StringId>();
	return key ? KeyNameToCode(key->GetString()) : 0;
}

class IsKeyDownNode : public ScriptNodeBase
{
	ScriptPinId myKeyPin;

public:
	void Init(const ScriptCreationContext& context) override
	{
		myKeyPin = AddKeyInput(context, "Key", "W");
		AddPin(context, ScriptPinRole::Output, ScriptLinkType::Property, "Down", GetPropertyType<bool>());
	}

	Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
	{
		const GameScriptContext* game = GetGameContext(context);
		const int key = ReadKey(context, myKeyPin);
		return Property::Create<bool>(game && key && game->IsKeyDown(key));
	}
};

class WasKeyPressedNode : public ScriptNodeBase
{
	ScriptPinId myKeyPin;

public:
	void Init(const ScriptCreationContext& context) override
	{
		myKeyPin = AddKeyInput(context, "Key", "Space");
		AddPin(context, ScriptPinRole::Output, ScriptLinkType::Property, "Pressed", GetPropertyType<bool>());
	}

	Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
	{
		const GameScriptContext* game = GetGameContext(context);
		const int key = ReadKey(context, myKeyPin);
		return Property::Create<bool>(game && key && game->WasKeyPressed(key));
	}
};

// +1 while the positive key is held, -1 while the negative key is: WASD becomes two Get Axis nodes.
class GetAxisNode : public ScriptNodeBase
{
	ScriptPinId myPositivePin, myNegativePin;

public:
	void Init(const ScriptCreationContext& context) override
	{
		myPositivePin = AddKeyInput(context, "Positive Key", "D");
		myNegativePin = AddKeyInput(context, "Negative Key", "A");
		AddPin(context, ScriptPinRole::Output, ScriptLinkType::Property, "Axis", GetPropertyType<float>());
	}

	Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
	{
		const GameScriptContext* game = GetGameContext(context);
		float axis = 0.f;
		if (game)
		{
			const int positive = ReadKey(context, myPositivePin), negative = ReadKey(context, myNegativePin);
			if (positive && game->IsKeyDown(positive)) axis += 1.f;
			if (negative && game->IsKeyDown(negative)) axis -= 1.f;
		}
		return Property::Create<float>(axis);
	}
};

class GetMouseDeltaNode : public ScriptNodeBase
{
public:
	void Init(const ScriptCreationContext& context) override
	{
		AddPin(context, ScriptPinRole::Output, ScriptLinkType::Property, "Delta", GetPropertyType<Vector2f>());
	}

	Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
	{
		const GameScriptContext* game = GetGameContext(context);
		return Property::Create<Vector2f>(game ? game->GetMouseDelta() : Vector2f{ 0.f, 0.f });
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
	ScriptNodeTypeRegistry::RegisterType<SetRotationNode>("Object/Set Rotation", "Turns the object this script runs on. Euler degrees; Y is the yaw");
	ScriptNodeTypeRegistry::RegisterType<GetDirectionsNode>("Object/Get Directions", "Forward, right and up of the object as it is turned now");
	ScriptNodeTypeRegistry::RegisterType<AddImpulseNode>("Object/Add Impulse", "Gives the object's physics body a push, in kg m/s. Needs a Rigidbody and a running simulation");
	ScriptNodeTypeRegistry::RegisterType<GetVelocityNode>("Object/Get Velocity", "The object's physics velocity (cm/s). Zero without a simulated Rigidbody");
	ScriptNodeTypeRegistry::RegisterType<SetVelocityNode<false>>("Object/Set Velocity", "Sets the object's physics velocity (cm/s)");
	ScriptNodeTypeRegistry::RegisterType<SetVelocityNode<true>>("Object/Set Horizontal Velocity", "Sets the sideways velocity (X and Z, cm/s) and keeps the current up/down speed, for walking");
	ScriptNodeTypeRegistry::RegisterType<IsKeyDownNode>("Input/Is Key Down", "True while the key is held. Keys: A-Z, 0-9, F1-F12, Space, Shift, Ctrl, Alt, Enter, Escape, Tab, Left/Right/Up/Down, Mouse Left/Right/Middle");
	ScriptNodeTypeRegistry::RegisterType<WasKeyPressedNode>("Input/Was Key Pressed", "True on the frame the key goes down");
	ScriptNodeTypeRegistry::RegisterType<GetAxisNode>("Input/Get Axis", "+1 while the positive key is held, -1 while the negative key is");
	ScriptNodeTypeRegistry::RegisterType<GetMouseDeltaNode>("Input/Get Mouse Delta", "How far the mouse moved since the previous frame (pixels)");
	ScriptNodeTypeRegistry::RegisterType<GetDeltaTimeNode>("Common/Get Delta Time", "Seconds since the previous frame");
}
