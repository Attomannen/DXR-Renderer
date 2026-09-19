#include <stdafx.h>
#include "EvaluatePoseNode.h"

#include <age/animation/PoseGenerator.h>
#include <age/script/Contexts/ScriptUpdateContext.h>
#include <age/script/BaseProperties.h>
#include <age/scene/ScenePropertyTypes.h>
#include <age/log/Log.h>

using namespace Ag;

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
