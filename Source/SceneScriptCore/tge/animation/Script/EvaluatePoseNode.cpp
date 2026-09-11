#include <stdafx.h>
#include "EvaluatePoseNode.h"

#include <tge/animation/PoseGenerator.h>
#include <tge/script/Contexts/ScriptUpdateContext.h>
#include <tge/script/BaseProperties.h>
#include <tge/scene/ScenePropertyTypes.h>
#include <tge/log/Log.h>

using namespace Tga;

void EvaluatePoseNode::Init(const ScriptCreationContext& context)
{
	{
		ScriptPin namePin = {};
		namePin.type = ScriptLinkType::Property;
		namePin.dataType = GetPropertyType<StringId>();
		namePin.defaultValue = Property::Create<StringId>();
		namePin.name = "Model Property Name"_tgaid;
		namePin.node = context.GetNodeId();
		namePin.role = ScriptPinRole::Input;
		myModelPropertyNameIn = context.FindOrCreatePin(namePin);
	}

	{
		ScriptPin valuePin = {};
		valuePin.type = ScriptLinkType::Property;
		valuePin.dataType = GetPropertyType<PoseAndMotion>();
		valuePin.defaultValue = Property::Create<PoseAndMotion>();
		valuePin.name = "Value"_tgaid;
		valuePin.node = context.GetNodeId();
		valuePin.role = ScriptPinRole::Input;

		myPoseInPin = context.FindOrCreatePin(valuePin);
	}
}

ScriptNodeResult EvaluatePoseNode::Execute(ScriptExecutionContext& context, ScriptPinId) const
{
	if (!context.GetUpdateContext().dynamicProperties)
	{
		ERROR_PRINT("Can't use EvaluatePoseNode without dynamic properties when running the script");
	}

	StringId propertyName = *context.ReadInputPin(myModelPropertyNameIn).Get<StringId>();

	// todo: remove this per frame allocation...
	std::string resultNameString = propertyName.GetString();
	resultNameString += "_pose";

	StringId resultName = StringRegistry::RegisterOrGetString(resultNameString);

	(*context.GetUpdateContext().dynamicProperties)[resultName] = context.ReadInputPin(myPoseInPin);

	return ScriptNodeResult::KeepRunning;
}
