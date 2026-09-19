#include <stdafx.h>
#include "FlowNodes.h"

#include "NodeHelpers.h"
#include <age/log/Log.h>

#include <algorithm>

using namespace Ag;
using namespace Ag::NodeHelpers;

namespace
{
	// A loop that never ends would freeze the game; stop it and say so.
	constexpr int kMaxLoopIterations = 100000;

	// ---------------------------------------------------------------- loops

	struct LoopData
	{
		int index = 0;
		bool broken = false;
	};

	class ForLoopNode : public ScriptNodeWithRuntimeData<LoopData>
	{
		ScriptPinId myBreak, myFirst, myLast, myBody, myCompleted;

	public:
		void Init(const ScriptCreationContext& context) override
		{
			FlowIn(context, "In");
			myBreak = FlowIn(context, "Break");
			myFirst = In<int>(context, "First Index", 0);
			myLast = In<int>(context, "Last Index", 9);
			myBody = FlowOut(context, "Loop Body");
			Out<int>(context, "Index");
			myCompleted = FlowOut(context, "Completed");
		}

		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			return Make<int>(GetRuntimeData(context).index);
		}

		ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId pin) const override
		{
			LoopData& data = GetRuntimeData(context);
			if (pin == myBreak)
			{
				data.broken = true;
				return ScriptNodeResult::Finished;
			}

			const int first = Read<int>(context, myFirst), last = Read<int>(context, myLast);
			data.broken = false;
			int iterations = 0;
			for (int i = first; i <= last && !data.broken; ++i)
			{
				if (++iterations > kMaxLoopIterations)
				{
					ERROR_PRINT("For Loop stopped after %d iterations", kMaxLoopIterations);
					break;
				}
				data.index = i;
				context.RunOutputPin(myBody);
			}
			context.TriggerOutputPin(myCompleted);
			return ScriptNodeResult::Finished;
		}
	};

	class WhileLoopNode : public ScriptNodeWithRuntimeData<LoopData>
	{
		ScriptPinId myBreak, myCondition, myBody, myCompleted;

	public:
		void Init(const ScriptCreationContext& context) override
		{
			FlowIn(context, "In");
			myBreak = FlowIn(context, "Break");
			myCondition = In<bool>(context, "Condition", false);
			myBody = FlowOut(context, "Loop Body");
			myCompleted = FlowOut(context, "Completed");
		}

		ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId pin) const override
		{
			LoopData& data = GetRuntimeData(context);
			if (pin == myBreak)
			{
				data.broken = true;
				return ScriptNodeResult::Finished;
			}

			data.broken = false;
			int iterations = 0;
			// The condition is read again every time round, so whatever feeds it is recalculated.
			while (!data.broken && Read<bool>(context, myCondition))
			{
				if (++iterations > kMaxLoopIterations)
				{
					ERROR_PRINT("While Loop stopped after %d iterations", kMaxLoopIterations);
					break;
				}
				context.RunOutputPin(myBody);
			}
			context.TriggerOutputPin(myCompleted);
			return ScriptNodeResult::Finished;
		}
	};

	// ---------------------------------------------------------------- run-once and toggles

	struct DoOnceData
	{
		bool initialised = false;
		bool done = false;
	};

	class DoOnceNode : public ScriptNodeWithRuntimeData<DoOnceData>
	{
		ScriptPinId myReset, myStartClosed, myCompleted;

	public:
		void Init(const ScriptCreationContext& context) override
		{
			FlowIn(context, "In");
			myReset = FlowIn(context, "Reset");
			myStartClosed = In<bool>(context, "Start Closed", false);
			myCompleted = FlowOut(context, "Completed");
		}

		ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId pin) const override
		{
			DoOnceData& data = GetRuntimeData(context);
			if (!data.initialised)
			{
				data.initialised = true;
				data.done = Read<bool>(context, myStartClosed);
			}
			if (pin == myReset)
			{
				data.done = false;
				return ScriptNodeResult::Finished;
			}
			if (!data.done)
			{
				data.done = true;
				context.TriggerOutputPin(myCompleted);
			}
			return ScriptNodeResult::Finished;
		}
	};

	struct DoNData
	{
		int count = 0;
	};

	class DoNNode : public ScriptNodeWithRuntimeData<DoNData>
	{
		ScriptPinId myReset, myN, myExit;

	public:
		void Init(const ScriptCreationContext& context) override
		{
			FlowIn(context, "In");
			myReset = FlowIn(context, "Reset");
			myN = In<int>(context, "N", 2);
			myExit = FlowOut(context, "Exit");
			Out<int>(context, "Counter");
		}

		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			return Make<int>(GetRuntimeData(context).count);
		}

		ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId pin) const override
		{
			DoNData& data = GetRuntimeData(context);
			if (pin == myReset)
			{
				data.count = 0;
				return ScriptNodeResult::Finished;
			}
			if (data.count < Read<int>(context, myN))
			{
				++data.count;
				context.TriggerOutputPin(myExit);
			}
			return ScriptNodeResult::Finished;
		}
	};

	struct FlipFlopData
	{
		bool nextIsB = false;
		bool lastWasA = false;
	};

	class FlipFlopNode : public ScriptNodeWithRuntimeData<FlipFlopData>
	{
		ScriptPinId myA, myB;

	public:
		void Init(const ScriptCreationContext& context) override
		{
			FlowIn(context, "In");
			myA = FlowOut(context, "A");
			myB = FlowOut(context, "B");
			Out<bool>(context, "Is A");
		}

		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			return Make<bool>(GetRuntimeData(context).lastWasA);
		}

		ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId) const override
		{
			FlipFlopData& data = GetRuntimeData(context);
			data.lastWasA = !data.nextIsB;
			context.TriggerOutputPin(data.lastWasA ? myA : myB);
			data.nextIsB = !data.nextIsB;
			return ScriptNodeResult::Finished;
		}
	};

	struct GateData
	{
		bool initialised = false;
		bool open = true;
	};

	class GateNode : public ScriptNodeWithRuntimeData<GateData>
	{
		ScriptPinId myEnter, myOpen, myClose, myToggle, myStartClosed, myExit;

	public:
		void Init(const ScriptCreationContext& context) override
		{
			myEnter = FlowIn(context, "Enter");
			myOpen = FlowIn(context, "Open");
			myClose = FlowIn(context, "Close");
			myToggle = FlowIn(context, "Toggle");
			myStartClosed = In<bool>(context, "Start Closed", false);
			myExit = FlowOut(context, "Exit");
		}

		ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId pin) const override
		{
			GateData& data = GetRuntimeData(context);
			if (!data.initialised)
			{
				data.initialised = true;
				data.open = !Read<bool>(context, myStartClosed);
			}
			if (pin == myOpen) data.open = true;
			else if (pin == myClose) data.open = false;
			else if (pin == myToggle) data.open = !data.open;
			else if (pin == myEnter && data.open) context.TriggerOutputPin(myExit);
			return ScriptNodeResult::Finished;
		}
	};

	// ---------------------------------------------------------------- switch and select

	class SwitchIntNode : public ScriptNodeBase
	{
		static constexpr int kCases = 5;
		ScriptPinId mySelection, myCases[kCases], myDefault;

	public:
		void Init(const ScriptCreationContext& context) override
		{
			FlowIn(context, "In");
			mySelection = In<int>(context, "Selection", 0);
			static const char* names[kCases] = { "0", "1", "2", "3", "4" };
			for (int i = 0; i < kCases; ++i) myCases[i] = FlowOut(context, names[i]);
			myDefault = FlowOut(context, "Default");
		}

		ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const int selection = Read<int>(context, mySelection);
			context.TriggerOutputPin(selection >= 0 && selection < kCases ? myCases[selection] : myDefault);
			return ScriptNodeResult::Finished;
		}
	};

	class SwitchStringNode : public ScriptNodeBase
	{
		static constexpr int kCases = 4;
		ScriptPinId mySelection, myValues[kCases], myCases[kCases], myDefault;

	public:
		void Init(const ScriptCreationContext& context) override
		{
			FlowIn(context, "In");
			mySelection = In<StringId>(context, "Selection");
			static const char* valueNames[kCases] = { "Value 0", "Value 1", "Value 2", "Value 3" };
			static const char* caseNames[kCases] = { "Case 0", "Case 1", "Case 2", "Case 3" };
			for (int i = 0; i < kCases; ++i) myValues[i] = In<StringId>(context, valueNames[i]);
			for (int i = 0; i < kCases; ++i) myCases[i] = FlowOut(context, caseNames[i]);
			myDefault = FlowOut(context, "Default");
		}

		ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const StringId selection = Read<StringId>(context, mySelection);
			for (int i = 0; i < kCases; ++i)
			{
				if (Read<StringId>(context, myValues[i]) == selection)
				{
					context.TriggerOutputPin(myCases[i]);
					return ScriptNodeResult::Finished;
				}
			}
			context.TriggerOutputPin(myDefault);
			return ScriptNodeResult::Finished;
		}
	};

	template <typename T>
	class SelectNode : public ScriptNodeBase
	{
		ScriptPinId myCondition, myA, myB;

	public:
		void Init(const ScriptCreationContext& context) override
		{
			myCondition = In<bool>(context, "Pick A", true);
			myA = In<T>(context, "A");
			myB = In<T>(context, "B");
			Out<T>(context, "Result");
		}

		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			return Make<T>(Read<T>(context, Read<bool>(context, myCondition) ? myA : myB));
		}
	};

	// ---------------------------------------------------------------- time

	struct RetriggerData
	{
		float time = 0.f;
	};

	// Like Delay, but calling it again starts the wait over instead of running twice.
	class RetriggerableDelayNode : public ScriptNodeWithRuntimeData<RetriggerData>
	{
		ScriptPinId myDuration, myCompleted;

	public:
		void Init(const ScriptCreationContext& context) override
		{
			FlowIn(context, "In");
			myDuration = In<float>(context, "Duration", 0.2f);
			myCompleted = FlowOut(context, "Completed");
		}

		ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId pin) const override
		{
			RetriggerData& data = GetRuntimeData(context);
			if (pin.id != ScriptPinId::InvalidId)
			{
				data.time = 0.f;
				return ScriptNodeResult::KeepRunning;
			}
			data.time += context.GetUpdateContext().deltaTime;
			if (data.time >= Read<float>(context, myDuration))
			{
				context.TriggerOutputPin(myCompleted);
				return ScriptNodeResult::Finished;
			}
			return ScriptNodeResult::KeepRunning;
		}
	};

	struct TimerData
	{
		bool active = false;
		float time = 0.f;
	};

	// Fires every Interval seconds (or once) after Start, until Cancel.
	class SetTimerNode : public ScriptNodeWithRuntimeData<TimerData>
	{
		ScriptPinId myStart, myCancel, myInterval, myLooping, myFired;

	public:
		void Init(const ScriptCreationContext& context) override
		{
			myStart = FlowIn(context, "Start");
			myCancel = FlowIn(context, "Cancel");
			myInterval = In<float>(context, "Interval", 1.f);
			myLooping = In<bool>(context, "Looping", true);
			myFired = FlowOut(context, "Fired");
		}

		ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId pin) const override
		{
			TimerData& data = GetRuntimeData(context);
			if (pin == myCancel)
			{
				data.active = false;
				return ScriptNodeResult::Finished;
			}
			if (pin == myStart)
			{
				data.active = true;
				data.time = 0.f;
				return ScriptNodeResult::KeepRunning;
			}
			if (!data.active)
				return ScriptNodeResult::Finished;

			data.time += context.GetUpdateContext().deltaTime;
			const float interval = std::max(0.001f, Read<float>(context, myInterval));
			if (data.time >= interval)
			{
				data.time -= interval;
				context.TriggerOutputPin(myFired);
				if (!Read<bool>(context, myLooping))
				{
					data.active = false;
					return ScriptNodeResult::Finished;
				}
			}
			return ScriptNodeResult::KeepRunning;
		}
	};

	class GetTimeNode : public ScriptNodeBase
	{
	public:
		void Init(const ScriptCreationContext& context) override { Out<float>(context, "Seconds"); }
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			return Make<float>(context.GetUpdateContext().timeSeconds);
		}
	};
}

void Ag::RegisterFlowNodes()
{
	using R = ScriptNodeTypeRegistry;
	R::RegisterType<ForLoopNode>("Flow/For Loop", "Runs Loop Body once for every index from First to Last, then Completed. Break stops it early");
	R::RegisterType<WhileLoopNode>("Flow/While Loop", "Runs Loop Body while Condition is true, then Completed. Break stops it early");
	R::RegisterType<DoOnceNode>("Flow/Do Once", "Lets the flow through the first time only. Reset opens it again");
	R::RegisterType<DoNNode>("Flow/Do N", "Lets the flow through the first N times. Reset starts counting again");
	R::RegisterType<FlipFlopNode>("Flow/Flip Flop", "Alternates between A and B every time it runs");
	R::RegisterType<GateNode>("Flow/Gate", "Passes the flow only while it is open. Open, Close and Toggle change that");
	R::RegisterType<SwitchIntNode>("Flow/Switch on Int", "Continues on the output that matches the Selection number");
	R::RegisterType<SwitchStringNode>("Flow/Switch on String", "Continues on the case whose Value equals the Selection");
	R::RegisterType<SelectNode<int>>("Flow/Select Int", "Result is A when Pick A is true, otherwise B");
	R::RegisterType<SelectNode<StringId>>("Flow/Select String", "Result is A when Pick A is true, otherwise B");
	R::RegisterType<SelectNode<bool>>("Flow/Select Bool", "Result is A when Pick A is true, otherwise B");
	R::RegisterType<SelectNode<Color>>("Flow/Select Color", "Result is A when Pick A is true, otherwise B");
	R::RegisterType<RetriggerableDelayNode>("Flow/Retriggerable Delay", "Completed runs Duration seconds after the last time In ran");
	R::RegisterType<SetTimerNode>("Flow/Set Timer", "Runs Fired every Interval seconds after Start, once or looping, until Cancel");
	R::RegisterType<GetTimeNode>("Flow/Get Game Time", "Seconds since the game started");
}
