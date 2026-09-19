#include <stdafx.h>

#include <age/stringRegistry/StringRegistry.h>

#include <age/Script/ScriptCommon.h>
#include <age/Script/ScriptNodeBase.h>
#include <age/Script/ScriptNodeTypeRegistry.h>
#include <age/script/BaseProperties.h>
#include <age/log/Log.h>
#include <iostream>

class ReturnFiveNode : public Ag::ScriptNodeBase
{
public:
	void Init(const Ag::ScriptCreationContext& context) override
	{
		using namespace Ag;
		ScriptPin outputPin = {};
		outputPin.type = ScriptLinkType::Property;
		outputPin.dataType = GetPropertyType<int>();

		outputPin.name = "Value"_tgaid;
		outputPin.node = context.GetNodeId();
		outputPin.role = ScriptPinRole::Output;

		context.FindOrCreatePin(outputPin);
	}

	Ag::Property ReadPin(Ag::ScriptExecutionContext&, Ag::ScriptPinId) const override
	{
		return Ag::Property::Create<int>(5);
	}
};

class PrintIntNode : public Ag::ScriptNodeBase
{
	Ag::ScriptPinId myIntPinId;
	Ag::ScriptPinId myOutPinId;

public:
	void Init(const Ag::ScriptCreationContext& context) override
	{
		using namespace Ag;

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
			flowOutPin.name = StringRegistry::RegisterOrGetString("");
			flowOutPin.node = context.GetNodeId();
			flowOutPin.role = ScriptPinRole::Output;

			myOutPinId = context.FindOrCreatePin(flowOutPin);
		}

		{
			ScriptPin intPin = {};
			intPin.type = ScriptLinkType::Property;
			intPin.dataType = GetPropertyType<int>();
			intPin.name = "Value"_tgaid;
			intPin.node = context.GetNodeId();
			intPin.defaultValue = Property::Create<int>(0);
			intPin.role = ScriptPinRole::Input;

			myIntPinId = context.FindOrCreatePin(intPin);
		}
	}

	Ag::ScriptNodeResult Execute(Ag::ScriptExecutionContext& context, Ag::ScriptPinId) const override
	{
		using namespace Ag;

		Property data = context.ReadInputPin(myIntPinId);
		INFO_PRINT("%d", *data.Get<int>());

		context.TriggerOutputPin(myOutPinId);

		return ScriptNodeResult::Finished;
	}
};

class PrintFloatNode : public Ag::ScriptNodeBase
{
	Ag::ScriptPinId myFloatPinId;
	Ag::ScriptPinId myOutPinId;

public:
	void Init(const Ag::ScriptCreationContext& context) override
	{
		using namespace Ag;

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
			flowOutPin.name = StringRegistry::RegisterOrGetString("");
			flowOutPin.node = context.GetNodeId();
			flowOutPin.role = ScriptPinRole::Output;

			myOutPinId = context.FindOrCreatePin(flowOutPin);
		}

		{
			ScriptPin floatPin = {};
			floatPin.type = ScriptLinkType::Property;
			floatPin.dataType = GetPropertyType<float>();
			floatPin.name = "Value"_tgaid;
			floatPin.node = context.GetNodeId();
			floatPin.defaultValue = Property::Create<float>(0.f);
			floatPin.role = ScriptPinRole::Input;

			myFloatPinId = context.FindOrCreatePin(floatPin);
		}
	}

	Ag::ScriptNodeResult Execute(Ag::ScriptExecutionContext& context, Ag::ScriptPinId) const override
	{
		using namespace Ag;

		Property data = context.ReadInputPin(myFloatPinId);
		INFO_PRINT("%f", *data.Get<float>());

		context.TriggerOutputPin(myOutPinId);

		return ScriptNodeResult::Finished;
	}
};

class PrintBoolNode : public Ag::ScriptNodeBase
{
	Ag::ScriptPinId myBoolPinId;
	Ag::ScriptPinId myOutPinId;

public:
	void Init(const Ag::ScriptCreationContext& context) override
	{
		using namespace Ag;

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
			flowOutPin.name = StringRegistry::RegisterOrGetString("");
			flowOutPin.node = context.GetNodeId();
			flowOutPin.role = ScriptPinRole::Output;

			myOutPinId = context.FindOrCreatePin(flowOutPin);
		}

		{
			ScriptPin boolPin = {};
			boolPin.type = ScriptLinkType::Property;
			boolPin.dataType = GetPropertyType<bool>();
			boolPin.name = "Value"_tgaid;
			boolPin.node = context.GetNodeId();
			boolPin.defaultValue = Property::Create<bool>(false);
			boolPin.role = ScriptPinRole::Input;

			myBoolPinId = context.FindOrCreatePin(boolPin);
		}
	}

	Ag::ScriptNodeResult Execute(Ag::ScriptExecutionContext& context, Ag::ScriptPinId) const override
	{
		using namespace Ag;

		Property data = context.ReadInputPin(myBoolPinId);
		INFO_PRINT("%s", (*data.Get<bool>() == true ? "true" : "false"));

		context.TriggerOutputPin(myOutPinId);

		return ScriptNodeResult::Finished;
	}
};

class PrintStringNode : public Ag::ScriptNodeBase
{
	Ag::ScriptPinId myStringPinId;
	Ag::ScriptPinId myOutPinId;
public:
	void Init(const Ag::ScriptCreationContext& context) override
	{
		using namespace Ag;

		{
			ScriptPin flowInPin = {};
			flowInPin.type = ScriptLinkType::Flow;
			flowInPin.name = "Run"_tgaid;
			flowInPin.node = context.GetNodeId();
			flowInPin.role = ScriptPinRole::Input;

			context.FindOrCreatePin(flowInPin);
		}

		{
			ScriptPin flowOutPin = {};
			flowOutPin.type = ScriptLinkType::Flow;
			flowOutPin.name = StringRegistry::RegisterOrGetString("");
			flowOutPin.node = context.GetNodeId();
			flowOutPin.role = ScriptPinRole::Output;

			myOutPinId = context.FindOrCreatePin(flowOutPin);
		}

		{
			ScriptPin intPin = {};
			intPin.type = ScriptLinkType::Property;
			intPin.dataType = GetPropertyType<StringId>();
			intPin.name = StringRegistry::RegisterOrGetString("");
			intPin.node = context.GetNodeId();
			intPin.defaultValue = Property::Create<StringId>("Text to print"_tgaid);
			intPin.role = ScriptPinRole::Input;

			myStringPinId = context.FindOrCreatePin(intPin);
		}
	}

	Ag::ScriptNodeResult Execute(Ag::ScriptExecutionContext& context, Ag::ScriptPinId) const override
	{
		using namespace Ag;

		Property data = context.ReadInputPin(myStringPinId);

		StringId stringId = *data.Get<StringId>();
		INFO_PRINT("%s", stringId.GetString());

		context.TriggerOutputPin(myOutPinId);

		return ScriptNodeResult::Finished;
	}
};


void RegisterExampleNodes()
{
	Ag::ScriptNodeTypeRegistry::RegisterType<PrintIntNode>("Examples/PrintInt", "Prints an integer");
	Ag::ScriptNodeTypeRegistry::RegisterType<PrintFloatNode>("Examples/PrintFloat", "Prints a float");
	Ag::ScriptNodeTypeRegistry::RegisterType<PrintStringNode>("Examples/PrintString", "Prints a string");
	Ag::ScriptNodeTypeRegistry::RegisterType<PrintBoolNode>("Examples/PrintBool", "Prints a bool");
	Ag::ScriptNodeTypeRegistry::RegisterType<ReturnFiveNode>("Examples/ReturnFive", "Returns five");
}
