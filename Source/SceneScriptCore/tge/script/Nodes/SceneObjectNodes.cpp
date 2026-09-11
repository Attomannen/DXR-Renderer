#include <stdafx.h>
#include "SceneObjectNodes.h"

#include <tge/script/ScriptNodeTypeRegistry.h>
#include <tge/stringRegistry/StringRegistry.h>

#include <tge/script/ScriptCommon.h>
#include <tge/script/ScriptNodeBase.h>

#include <tge/script/BaseProperties.h>

#include <tge/scene/Scene.h>

using namespace Tga;


#if false

class TranslateSceneObjectXNode : public ScriptNodeBase
{
	ScriptPinId myInputPin;
	ScriptPinId myValueInPin;

public:
	void Init(const ScriptCreationContext& context) override
	{
		ScriptPin inputPin = {};
		inputPin.type = ScriptLinkType::Flow;
		inputPin.name = "Translate X"_tgaid;
		inputPin.node = context.GetNodeId();
		inputPin.role = ScriptPinRole::Input;
		myInputPin = context.FindOrCreatePin(inputPin);

		ScriptPin valueInPin = {};
		valueInPin.type = ScriptLinkType::Property;
		valueInPin.dataType = GetPropertyType<float>();
		valueInPin.name = "Amount"_tgaid;
		valueInPin.node = context.GetNodeId();
		valueInPin.role = ScriptPinRole::Input;
		valueInPin.defaultValue = Property::Create<float>(0.f);
		myValueInPin = context.FindOrCreatePin(valueInPin);
	}


	ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId) const override
	{
		const GameUpdateContext& ctx = static_cast<const GameUpdateContext&>(context.GetUpdateContext());
		SceneObject &sceneObject = *GetActiveScene()->GetSceneObject(ctx.sceneObjectId);

		float value = *context.ReadInputPin(myValueInPin).Get<float>();

		sceneObject.GetPosition().x += value;
		//sceneObject.GetTransform().Translate({ value, 0.f, 0.f });

		return ScriptNodeResult::Finished;
	}

	bool ShouldExecuteAtStart() const override { return false; }
};

class TranslateSceneObjectYNode : public ScriptNodeBase
{
	ScriptPinId myInputPin;
	ScriptPinId myValueInPin;

public:
	void Init(const ScriptCreationContext& context) override
	{
		ScriptPin inputPin = {};
		inputPin.type = ScriptLinkType::Flow;
		inputPin.name = "Translate Y"_tgaid;
		inputPin.node = context.GetNodeId();
		inputPin.role = ScriptPinRole::Input;
		myInputPin = context.FindOrCreatePin(inputPin);

		ScriptPin valueInPin = {};
		valueInPin.type = ScriptLinkType::Property;
		valueInPin.dataType = GetPropertyType<float>();
		valueInPin.name = "Amount"_tgaid;
		valueInPin.node = context.GetNodeId();
		valueInPin.role = ScriptPinRole::Input;
		valueInPin.defaultValue = Property::Create<float>(0.f);
		myValueInPin = context.FindOrCreatePin(valueInPin);
	}


	ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId) const override
	{
		const GameUpdateContext& ctx = static_cast<const GameUpdateContext&>(context.GetUpdateContext());
		SceneObject &sceneObject = *GetActiveScene()->GetSceneObject(ctx.sceneObjectId);

		float value = *context.ReadInputPin(myValueInPin).Get<float>();

		sceneObject.GetPosition().y += value;

		return ScriptNodeResult::Finished;
	}

	bool ShouldExecuteAtStart() const override { return false; }
};

class TranslateSceneObjectXYNode : public ScriptNodeBase
{
	ScriptPinId myInputPin;
	ScriptPinId myValueInPin;

public:
	void Init(const ScriptCreationContext& context) override
	{
		ScriptPin inputPin = {};
		inputPin.type = ScriptLinkType::Flow;
		inputPin.name = "Translate XY"_tgaid;
		inputPin.node = context.GetNodeId();
		inputPin.role = ScriptPinRole::Input;
		myInputPin = context.FindOrCreatePin(inputPin);

		ScriptPin valueInPin = {};
		valueInPin.type = ScriptLinkType::Property;
		valueInPin.dataType = GetPropertyType<Vector2f>();
		valueInPin.name = StringRegistry::RegisterOrGetString("");
		valueInPin.node = context.GetNodeId();
		valueInPin.role = ScriptPinRole::Input;
		valueInPin.defaultValue = Property::Create<Vector2f>(0.f);
		myValueInPin = context.FindOrCreatePin(valueInPin);
	}


	ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId) const override
	{
		const GameUpdateContext& ctx = static_cast<const GameUpdateContext&>(context.GetUpdateContext());
		SceneObject &sceneObject = *GetActiveScene()->GetSceneObject(ctx.sceneObjectId);

		Vector2f value = *context.ReadInputPin(myValueInPin).Get<Vector2f>();

		sceneObject.GetTRS().translation += {value.x, value.y, 0.f};

		return ScriptNodeResult::Finished;
	}

	bool ShouldExecuteAtStart() const override { return false; }
};

class TranslateSceneObjectXYZNode : public ScriptNodeBase
{
	ScriptPinId myInputPin;
	ScriptPinId myValueInPin;

public:
	void Init(const ScriptCreationContext& context) override
	{
		ScriptPin inputPin = {};
		inputPin.type = ScriptLinkType::Flow;
		inputPin.name = "Translate XYZ"_tgaid;
		inputPin.node = context.GetNodeId();
		inputPin.role = ScriptPinRole::Input;
		myInputPin = context.FindOrCreatePin(inputPin);

		ScriptPin valueInPin = {};
		valueInPin.type = ScriptLinkType::Property;
		valueInPin.dataType = GetPropertyType<Vector3f>();
		valueInPin.name = StringRegistry::RegisterOrGetString("");
		valueInPin.node = context.GetNodeId();
		valueInPin.role = ScriptPinRole::Input;
		valueInPin.defaultValue = Property::Create<Vector3f>(0.f);
		myValueInPin = context.FindOrCreatePin(valueInPin);
	}


	ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId) const override
	{
		const GameUpdateContext& ctx = static_cast<const GameUpdateContext&>(context.GetUpdateContext());
		SceneObject &sceneObject = *GetActiveScene()->GetSceneObject(ctx.sceneObjectId);

		Vector3f value = *context.ReadInputPin(myValueInPin).Get<Vector3f>();

		sceneObject.GetPosition().x += value.x;
		sceneObject.GetPosition().y += value.y;
		sceneObject.GetPosition().z += value.z;

		return ScriptNodeResult::Finished;
	}

	bool ShouldExecuteAtStart() const override { return false; }
};

#endif

void Tga::RegisterSceneObjectNodes()
{
	//ScriptNodeTypeRegistry::RegisterType<TranslateSceneObjectXNode>("Scene Object/Transform/Translate X", "Translate the SceneObject on the X-axis");
	//ScriptNodeTypeRegistry::RegisterType<TranslateSceneObjectYNode>("Scene Object/Transform/Translate Y", "Translate the SceneObject on the Y-axis");
	//ScriptNodeTypeRegistry::RegisterType<TranslateSceneObjectXYNode>("Scene Object/Transform/Translate XY", "Translate the SceneObject in the XY-plane");
	//ScriptNodeTypeRegistry::RegisterType<TranslateSceneObjectXYZNode>("Scene Object/Transform/Translate XYZ", "Translate the SceneObject in 3D-space");
}


