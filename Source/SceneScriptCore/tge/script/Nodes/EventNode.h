#pragma once

#include <tge/script/ScriptNodeBase.h>
#include <tge/script/BaseProperties.h>
#include <tge/stringRegistry/StringRegistry.h>

namespace Tga
{
	// Events the game raises on an object while it runs. Each has an event node in the graph that
	// starts a chain when it happens.
	enum class ScriptEventKind : int
	{
		CollisionEnter, // this object touched another one
		TriggerEnter,   // this object entered a trigger volume, or something entered it
	};

	class EventNode : public ScriptNodeBase
	{
		ScriptEventKind myKind;
		ScriptPinId myFlowOutput;
		ScriptPinId myOtherPin;

	public:
		explicit EventNode(ScriptEventKind kind) : myKind(kind) {}

		ScriptEventKind GetKind() const { return myKind; }
		ScriptPinId GetFlowOutput() const { return myFlowOutput; }

		void Init(const ScriptCreationContext& context) override
		{
			ScriptPin flow = {};
			flow.type = ScriptLinkType::Flow;
			flow.role = ScriptPinRole::Output;
			flow.name = StringRegistry::RegisterOrGetString("Then");
			flow.node = context.GetNodeId();
			myFlowOutput = context.FindOrCreatePin(flow);

			// Which object was on the other side (its index in the scene), for scripts that care.
			ScriptPin other = {};
			other.type = ScriptLinkType::Property;
			other.role = ScriptPinRole::Output;
			other.dataType = GetPropertyType<int>();
			other.name = StringRegistry::RegisterOrGetString("Other Object");
			other.node = context.GetNodeId();
			myOtherPin = context.FindOrCreatePin(other);
		}

		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override;
	};

	class CollisionEnterNode : public EventNode { public: CollisionEnterNode() : EventNode(ScriptEventKind::CollisionEnter) {} };
	class TriggerEnterNode : public EventNode { public: TriggerEnterNode() : EventNode(ScriptEventKind::TriggerEnter) {} };
}
