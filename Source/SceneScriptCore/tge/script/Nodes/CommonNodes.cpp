#include <stdafx.h>
#include "CommonNodes.h"

#include <tge/script/ScriptNodeTypeRegistry.h>
#include <tge/stringRegistry/StringRegistry.h>
#include <tge/script/ScriptCommon.h>
#include <tge/script/ScriptNodeBase.h>
#include <tge/script/Contexts/ScriptUpdateContext.h>
#include <tge/script/BaseProperties.h>
#include <tge/scene/ScenePropertyTypes.h>
#include <tge/log/Log.h>

using namespace Tga;

// used when nodes fail to load
class ErrorNode : public ScriptNodeBase
{
	virtual void Init(const ScriptCreationContext&) {}

};

class StartNode : public ScriptNodeBase
{
	ScriptPinId myOutputPin;

public:
	void Init(const ScriptCreationContext& context) override
	{
		ScriptPin outputPin = {};
		outputPin.type = ScriptLinkType::Flow;
		outputPin.name = "Start"_tgaid;
		outputPin.node = context.GetNodeId();
		outputPin.role = ScriptPinRole::Output;

		myOutputPin = context.FindOrCreatePin(outputPin);
	}

	ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId) const override
	{
		context.TriggerOutputPin(myOutputPin);

		return ScriptNodeResult::Finished;
	}

	bool ShouldExecuteAtStart() const override { return true; }
};

class TickNode : public ScriptNodeBase
{
	ScriptPinId myOutTickPinId;

public:
	void Init(const ScriptCreationContext& ctx) override
	{
		ScriptPin outputPin = {};
		outputPin.type = ScriptLinkType::Flow;
		outputPin.name = "Tick"_tgaid;
		outputPin.node = ctx.GetNodeId();
		outputPin.role = ScriptPinRole::Output;
		myOutTickPinId = ctx.FindOrCreatePin(outputPin);
	}

	ScriptNodeResult Execute(ScriptExecutionContext& ctx, ScriptPinId) const override
	{
		ctx.TriggerOutputPin(myOutTickPinId);
		return ScriptNodeResult::KeepRunning;
	}
	bool ShouldExecuteAtStart() const override { return true; }
};

class TriggerNode : public ScriptNodeBase
{
	ScriptPinId myOutputPin;

public:
	void Init(const ScriptCreationContext& context) override
	{
		ScriptPin outputPin = {};
		outputPin.type = ScriptLinkType::Flow;
		outputPin.name = "Start"_tgaid;
		outputPin.node = context.GetNodeId();
		outputPin.role = ScriptPinRole::Output;

		myOutputPin = context.FindOrCreatePin(outputPin);
	}

	ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId) const override
	{
		context.TriggerOutputPin(myOutputPin);

		return ScriptNodeResult::Finished;
	}
};

class BranchNode : public ScriptNodeBase
{
	ScriptPinId myFlowInPinId;
	ScriptPinId myTrueOutputPinId;
	ScriptPinId myFalseOutputPinId;
	ScriptPinId myInConditionPinId;

public:
	void Init(const ScriptCreationContext& context) override
	{
		{
			ScriptPin flowInPin = {};
			flowInPin.type = ScriptLinkType::Flow;
			flowInPin.name = "In"_tgaid;
			flowInPin.node = context.GetNodeId();
			flowInPin.role = ScriptPinRole::Input;
			myFlowInPinId = context.FindOrCreatePin(flowInPin);
		}
		{
			ScriptPin outputTruePin = {};
			outputTruePin.type = ScriptLinkType::Flow;
			outputTruePin.name = "True"_tgaid;
			outputTruePin.node = context.GetNodeId();
			outputTruePin.role = ScriptPinRole::Output;
			myTrueOutputPinId = context.FindOrCreatePin(outputTruePin);
		}
		{
			ScriptPin outputFalsePin = {};
			outputFalsePin.type = ScriptLinkType::Flow;
			outputFalsePin.name = "False"_tgaid;
			outputFalsePin.node = context.GetNodeId();
			outputFalsePin.role = ScriptPinRole::Output;
			myFalseOutputPinId = context.FindOrCreatePin(outputFalsePin);
		}
		{
			ScriptPin inConditionPin = {};
			inConditionPin.type = ScriptLinkType::Property;
			inConditionPin.dataType = GetPropertyType<bool>();
			inConditionPin.name = "Condition"_tgaid;
			inConditionPin.node = context.GetNodeId();
			inConditionPin.role = ScriptPinRole::Input;
			inConditionPin.defaultValue = Property::Create<bool>();
			myInConditionPinId = context.FindOrCreatePin(inConditionPin);
		}
	}

	ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId) const override
	{
		bool condition = *context.ReadInputPin(myInConditionPinId).Get<bool>();

		context.TriggerOutputPin(condition==true ? myTrueOutputPinId : myFalseOutputPinId);

		return ScriptNodeResult::Finished;
	}
};

struct DelayNodeRuntimeData
{
	float time;
};

class DelayNode : public ScriptNodeWithRuntimeData<DelayNodeRuntimeData>
{
	ScriptPinId myFlowInPinId;
	ScriptPinId myOutputAtDelayPinId;
	ScriptPinId myOutputTickPinId;
	ScriptPinId myInDelayPinId;

public:
	void Init(const ScriptCreationContext& context) override
	{
		{
			ScriptPin flowInPin = {};
			flowInPin.type = ScriptLinkType::Flow;
			flowInPin.name = "In"_tgaid;
			flowInPin.node = context.GetNodeId();
			flowInPin.role = ScriptPinRole::Input;
			myFlowInPinId = context.FindOrCreatePin(flowInPin);
		}
		{
			ScriptPin tickPin = {};
			tickPin.type = ScriptLinkType::Flow;
			tickPin.name = "Tick"_tgaid;
			tickPin.node = context.GetNodeId();
			tickPin.role = ScriptPinRole::Output;
			myOutputTickPinId = context.FindOrCreatePin(tickPin);
		}
		{
			ScriptPin outputAtDelayPin = {};
			outputAtDelayPin.type = ScriptLinkType::Flow;
			outputAtDelayPin.name = "Fire"_tgaid;
			outputAtDelayPin.node = context.GetNodeId();
			outputAtDelayPin.role = ScriptPinRole::Output;
			myOutputAtDelayPinId = context.FindOrCreatePin(outputAtDelayPin);
		}
		{
			ScriptPin inDelayPin = {};
			inDelayPin.type = ScriptLinkType::Property;
			inDelayPin.dataType = GetPropertyType<float>();
			inDelayPin.name = "Delay"_tgaid;
			inDelayPin.node = context.GetNodeId();
			inDelayPin.role = ScriptPinRole::Input;
			inDelayPin.defaultValue = Property::Create<float>(0.f);
			myInDelayPinId = context.FindOrCreatePin(inDelayPin);
		}
	}

	ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId) const override
	{
		DelayNodeRuntimeData& runtimeData = GetRuntimeData(context);

		runtimeData.time += context.GetUpdateContext().deltaTime;

		if(runtimeData.time >= *context.ReadInputPin(myInDelayPinId).Get<float>())
		{
			runtimeData.time = 0.f;
			context.TriggerOutputPin(myOutputAtDelayPinId);
			return ScriptNodeResult::Finished;
		}

		context.TriggerOutputPin(myOutputTickPinId);
		return ScriptNodeResult::KeepRunning;
	}
};

template <typename T>
class ReadPropertyNode : public ScriptNodeBase
{
	ScriptPinId myPropertyNamePin;
	ScriptPinId myValueOutPin;

public:
	void Init(const ScriptCreationContext& context) override
	{
		{
			ScriptPin namePin = {};
			namePin.type = ScriptLinkType::Property;
			namePin.dataType = GetPropertyType<StringId>();
			namePin.defaultValue = Property::Create<StringId>();
			namePin.name = "Name"_tgaid;
			namePin.node = context.GetNodeId();
			namePin.role = ScriptPinRole::Input;
			myPropertyNamePin = context.FindOrCreatePin(namePin);
		}

		{
			ScriptPin valuePin = {};
			valuePin.type = ScriptLinkType::Property;
			valuePin.dataType = GetPropertyType<T>();
			valuePin.name = "Value"_tgaid;
			valuePin.node = context.GetNodeId();
			valuePin.role = ScriptPinRole::Output;

			myValueOutPin = context.FindOrCreatePin(valuePin);
		}
	}

	Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override 
	{ 
		if (context.GetUpdateContext().dynamicProperties)
		{
			Property nameProperty = context.ReadInputPin(myPropertyNamePin);

			auto it = context.GetUpdateContext().dynamicProperties->find(*nameProperty.Get<StringId>());

			if (it != context.GetUpdateContext().dynamicProperties->end())
			{
				if (it->second.GetType() != nullptr && it->second.GetType() != GetPropertyType<T>())
				{
					ERROR_PRINT("Type mismatch in property");
				}

				return it->second;
			}
		}

		if (context.GetUpdateContext().staticProperties)
		{
			Property nameProperty = context.ReadInputPin(myPropertyNamePin);

			auto it = context.GetUpdateContext().staticProperties->find(*nameProperty.Get<StringId>());

			if (it != context.GetUpdateContext().staticProperties->end())
			{
				if (it->second.GetType() != nullptr && it->second.GetType() != GetPropertyType<T>())
				{
					ERROR_PRINT("Type mismatch in property");
				}

				return it->second;
			}
		}

		return {};
	}


	bool ShouldExecuteAtStart() const override { return false; }
};

template <typename T>
class WritePropertyNode : public ScriptNodeBase
{
	ScriptPinId myPropertyNamePin;
	ScriptPinId myValueInPin;
	ScriptPinId myOutPin;

public:
	void Init(const ScriptCreationContext& context) override
	{
		{
			ScriptPin flowPin = {};
			flowPin.type = ScriptLinkType::Flow;
			flowPin.name = "Run"_tgaid;
			flowPin.node = context.GetNodeId();
			flowPin.role = ScriptPinRole::Input;

			context.FindOrCreatePin(flowPin);
		}

		{
			ScriptPin flowOutPin = {};
			flowOutPin.type = ScriptLinkType::Flow;
			flowOutPin.name = ""_tgaid;
			flowOutPin.node = context.GetNodeId();
			flowOutPin.role = ScriptPinRole::Output;

			myOutPin = context.FindOrCreatePin(flowOutPin);
		}

		{
			ScriptPin namePin = {};
			namePin.type = ScriptLinkType::Property;
			namePin.dataType = GetPropertyType<StringId>();
			namePin.defaultValue = Property::Create<StringId>();
			namePin.name = "Name"_tgaid;
			namePin.node = context.GetNodeId();
			namePin.role = ScriptPinRole::Input;
			myPropertyNamePin = context.FindOrCreatePin(namePin);
		}

		{
			ScriptPin valuePin = {};
			valuePin.type = ScriptLinkType::Property;
			valuePin.dataType = GetPropertyType<T>();
			valuePin.defaultValue = Property::Create<T>();
			valuePin.name = "Value"_tgaid;
			valuePin.node = context.GetNodeId();
			valuePin.role = ScriptPinRole::Input;

			myValueInPin = context.FindOrCreatePin(valuePin);
		}
	}

	ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId) const override
	{
		if (context.GetUpdateContext().dynamicProperties)
		{
			Property nameProperty = context.ReadInputPin(myPropertyNamePin);

			Property& property = (*context.GetUpdateContext().dynamicProperties)[*nameProperty.Get<StringId>()];

			if (property.GetType() != nullptr && property.GetType() != GetPropertyType<T>())
			{
					ERROR_PRINT("Type mismatch in property");
			}

			Property valueProperty = context.ReadInputPin(myValueInPin);
			property = valueProperty;
		}

		context.TriggerOutputPin(myOutPin);

		return ScriptNodeResult::Finished;
	}

	bool ShouldExecuteAtStart() const override { return false; }
};

void Tga::RegisterCommonNodes()
{
	ScriptNodeTypeRegistry::RegisterType<ErrorNode>("Common/ERROR", "Error node, is created when nodes can't load");

	ScriptNodeTypeRegistry::RegisterType<StartNode>("Common/Start", "A node that executes once when the script starts");
	ScriptNodeTypeRegistry::RegisterType<TriggerNode>("Common/Trigger", "A node that executes when triggered");


	ScriptNodeTypeRegistry::RegisterType<TickNode>("Common/Tick", "A node that executes every frame");
	ScriptNodeTypeRegistry::RegisterType<DelayNode>("Common/Delay", "A node that executes after a delays");
	ScriptNodeTypeRegistry::RegisterType<BranchNode>("Common/Branch", "A node that branches depending on a condition");



	// Read / Write Property nodes
	{
		ScriptNodeTypeRegistry::RegisterType<ReadPropertyNode<bool>>("Read Property/Read Bool Property", "Reads an bool property");
		ScriptNodeTypeRegistry::RegisterType<WritePropertyNode<bool>>("Write Property/Write Bool Property", "Writes an bool property");

		ScriptNodeTypeRegistry::RegisterType<ReadPropertyNode<int>>("Read Property/Read Int Property", "Reads an int property");
		ScriptNodeTypeRegistry::RegisterType<WritePropertyNode<int>>("Write Property/Write Int Property", "Writes an int property");

		ScriptNodeTypeRegistry::RegisterType<ReadPropertyNode<float>>("Read Property/Read Float Property", "Reads an float property");
		ScriptNodeTypeRegistry::RegisterType<WritePropertyNode<float>>("Write Property/Write Float Property", "Writes an float property");

		ScriptNodeTypeRegistry::RegisterType<ReadPropertyNode<Vector2f>>("Read Property/Read Float2 Property", "Reads an float2 property");
		ScriptNodeTypeRegistry::RegisterType<WritePropertyNode<Vector2f>>("Write Property/Write Float2 Property", "Writes an float2 property");

		ScriptNodeTypeRegistry::RegisterType<ReadPropertyNode<Vector3f>>("Read Property/Read Float3 Property", "Reads an float3 property");
		ScriptNodeTypeRegistry::RegisterType<WritePropertyNode<Vector3f>>("Write Property/Write Float3 Property", "Writes an float3 property");

		ScriptNodeTypeRegistry::RegisterType<ReadPropertyNode<Vector4f>>("Read Property/Read Float4 Property", "Reads an float4 property");
		ScriptNodeTypeRegistry::RegisterType<WritePropertyNode<Vector4f>>("Write Property/Write Float4 Property", "Writes an float4 property");

		ScriptNodeTypeRegistry::RegisterType<ReadPropertyNode<Color>>("Read Property/Read Color Property", "Reads an Color property");
		ScriptNodeTypeRegistry::RegisterType<WritePropertyNode<Color>>("Write Property/Write Color Property", "Writes an Color property");

		ScriptNodeTypeRegistry::RegisterType<ReadPropertyNode<StringId>>("Read Property/Read String Property", "Reads an String property");
		ScriptNodeTypeRegistry::RegisterType<WritePropertyNode<StringId>>("Write Property/Write String Property", "Writes an String property");

		ScriptNodeTypeRegistry::RegisterType<ReadPropertyNode<CopyOnWriteWrapper<AnimationClipReference>>>("Read Property/Read Animation Clip Property", "Reads an Animation Clip property");
		ScriptNodeTypeRegistry::RegisterType<WritePropertyNode<CopyOnWriteWrapper<AnimationClipReference>>>("Write Property/Write Animation Clip Property", "Writes an Animation Clip property");
	}
}
