#include <stdafx.h>

#include <tge/stringRegistry/StringRegistry.h>

#include <tge/Script/ScriptCommon.h>
#include <tge/Script/ScriptNodeBase.h>
#include <tge/Script/ScriptNodeTypeRegistry.h>
#include <tge/script/BaseProperties.h>
#include <tge/log/Log.h>
#include <iostream>

class ReturnFiveNode : public Tga::ScriptNodeBase
{
public:
	void Init(const Tga::ScriptCreationContext& context) override
	{
		using namespace Tga;
		ScriptPin outputPin = {};
		outputPin.type = ScriptLinkType::Property;
		outputPin.dataType = GetPropertyType<int>();

		outputPin.name = "Value"_tgaid;
		outputPin.node = context.GetNodeId();
		outputPin.role = ScriptPinRole::Output;

		context.FindOrCreatePin(outputPin);
	}

	Tga::Property ReadPin(Tga::ScriptExecutionContext&, Tga::ScriptPinId) const override
	{
		return Tga::Property::Create<int>(5);
	}
};

class PrintIntNode : public Tga::ScriptNodeBase
{
	Tga::ScriptPinId myIntPinId;
	Tga::ScriptPinId myOutPinId;

public:
	void Init(const Tga::ScriptCreationContext& context) override
	{
		using namespace Tga;

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

	Tga::ScriptNodeResult Execute(Tga::ScriptExecutionContext& context, Tga::ScriptPinId) const override
	{
		using namespace Tga;

		Property data = context.ReadInputPin(myIntPinId);
		INFO_PRINT("%d", *data.Get<int>());

		context.TriggerOutputPin(myOutPinId);

		return ScriptNodeResult::Finished;
	}
};

class PrintFloatNode : public Tga::ScriptNodeBase
{
	Tga::ScriptPinId myFloatPinId;
	Tga::ScriptPinId myOutPinId;

public:
	void Init(const Tga::ScriptCreationContext& context) override
	{
		using namespace Tga;

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

	Tga::ScriptNodeResult Execute(Tga::ScriptExecutionContext& context, Tga::ScriptPinId) const override
	{
		using namespace Tga;

		Property data = context.ReadInputPin(myFloatPinId);
		INFO_PRINT("%f", *data.Get<float>());

		context.TriggerOutputPin(myOutPinId);

		return ScriptNodeResult::Finished;
	}
};

class PrintBoolNode : public Tga::ScriptNodeBase
{
	Tga::ScriptPinId myBoolPinId;
	Tga::ScriptPinId myOutPinId;

public:
	void Init(const Tga::ScriptCreationContext& context) override
	{
		using namespace Tga;

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

	Tga::ScriptNodeResult Execute(Tga::ScriptExecutionContext& context, Tga::ScriptPinId) const override
	{
		using namespace Tga;

		Property data = context.ReadInputPin(myBoolPinId);
		INFO_PRINT("%s", (*data.Get<bool>() == true ? "true" : "false"));

		context.TriggerOutputPin(myOutPinId);

		return ScriptNodeResult::Finished;
	}
};

class PrintStringNode : public Tga::ScriptNodeBase
{
	Tga::ScriptPinId myStringPinId;
	Tga::ScriptPinId myOutPinId;
public:
	void Init(const Tga::ScriptCreationContext& context) override
	{
		using namespace Tga;

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

	Tga::ScriptNodeResult Execute(Tga::ScriptExecutionContext& context, Tga::ScriptPinId) const override
	{
		using namespace Tga;

		Property data = context.ReadInputPin(myStringPinId);

		StringId stringId = *data.Get<StringId>();
		INFO_PRINT("%s", stringId.GetString());

		context.TriggerOutputPin(myOutPinId);

		return ScriptNodeResult::Finished;
	}
};


void RegisterExampleNodes()
{
	Tga::ScriptNodeTypeRegistry::RegisterType<PrintIntNode>("Examples/PrintInt", "Prints an integer");
	Tga::ScriptNodeTypeRegistry::RegisterType<PrintFloatNode>("Examples/PrintFloat", "Prints a float");
	Tga::ScriptNodeTypeRegistry::RegisterType<PrintStringNode>("Examples/PrintString", "Prints a string");
	Tga::ScriptNodeTypeRegistry::RegisterType<PrintBoolNode>("Examples/PrintBool", "Prints a bool");
	Tga::ScriptNodeTypeRegistry::RegisterType<ReturnFiveNode>("Examples/ReturnFive", "Returns five");
}
