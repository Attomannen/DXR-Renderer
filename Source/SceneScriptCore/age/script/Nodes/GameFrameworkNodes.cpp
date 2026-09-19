#include <stdafx.h>
#include "GameFrameworkNodes.h"

#include "NodeHelpers.h"
#include <age/script/Contexts/GameScriptContext.h>

using namespace Ag;
using namespace Ag::NodeHelpers;

namespace
{
	GameScriptContext* GetGame(ScriptExecutionContext& context)
	{
		return dynamic_cast<GameScriptContext*>(&context.GetUpdateContext());
	}

	class GetPlayerStartNode : public ScriptNodeBase
	{
		ScriptPinId myTag, myFound, myLocation, myYaw;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myTag = In<StringId>(context, "Tag");
			myFound = Out<bool>(context, "Found");
			myLocation = Out<Vector3f>(context, "Location");
			myYaw = Out<float>(context, "Yaw");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId pin) const override
		{
			Vector3f location(0.f, 0.f, 0.f);
			float yaw = 0.f;
			bool found = false;
			if (GameScriptContext* game = GetGame(context))
				found = game->FindPlayerStart(Read<StringId>(context, myTag).GetString(), location, yaw);
			if (pin == myFound) return Make<bool>(found);
			if (pin == myLocation) return Make<Vector3f>(location);
			return Make<float>(yaw);
		}
	};

	// Moves this object to a Player Start and turns it to face the way the Player Start does.
	class TeleportToPlayerStartNode : public ScriptNodeBase
	{
		ScriptPinId myTag, myOut;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			FlowIn(context, "Teleport");
			myTag = In<StringId>(context, "Tag");
			myOut = FlowOut(context, "");
		}
		ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId) const override
		{
			if (GameScriptContext* game = GetGame(context))
			{
				Vector3f location(0.f, 0.f, 0.f);
				float yaw = 0.f;
				if (game->FindPlayerStart(Read<StringId>(context, myTag).GetString(), location, yaw))
				{
					game->SetLocation(location);
					game->SetRotation(Vector3f(0.f, yaw, 0.f));
				}
			}
			context.TriggerOutputPin(myOut);
			return ScriptNodeResult::Finished;
		}
	};

	class OpenLevelNode : public ScriptNodeBase
	{
		ScriptPinId myLevel;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			FlowIn(context, "Open");
			myLevel = In<StringId>(context, "Level");
		}
		ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId) const override
		{
			if (GameScriptContext* game = GetGame(context))
				game->OpenLevel(Read<StringId>(context, myLevel).GetString());
			return ScriptNodeResult::Finished;
		}
	};

	class RestartLevelNode : public ScriptNodeBase
	{
	public:
		void Init(const ScriptCreationContext& context) override { FlowIn(context, "Restart"); }
		ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId) const override
		{
			if (GameScriptContext* game = GetGame(context))
				game->OpenLevel("");
			return ScriptNodeResult::Finished;
		}
	};

	class QuitGameNode : public ScriptNodeBase
	{
	public:
		void Init(const ScriptCreationContext& context) override { FlowIn(context, "Quit"); }
		ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId) const override
		{
			if (GameScriptContext* game = GetGame(context))
				game->QuitGame();
			return ScriptNodeResult::Finished;
		}
	};

	class GetGameNameNode : public ScriptNodeBase
	{
	public:
		void Init(const ScriptCreationContext& context) override { Out<StringId>(context, "Name"); }
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const GameScriptContext* game = GetGame(context);
			return Make<StringId>(MakeString(game ? game->GetGameName() : ""));
		}
	};
}

void Ag::RegisterGameFrameworkNodes()
{
	using R = ScriptNodeTypeRegistry;
	R::RegisterType<GetPlayerStartNode>("Game/Get Player Start", "Finds a Player Start by tag (an empty tag finds the first): where it is and which way it faces");
	R::RegisterType<TeleportToPlayerStartNode>("Game/Teleport to Player Start", "Moves this object to a Player Start and turns it the same way. Use it to respawn the player");
	R::RegisterType<OpenLevelNode>("Game/Open Level", "Opens another level, e.g. Scenes/Main, at the start of the next frame");
	R::RegisterType<RestartLevelNode>("Game/Restart Level", "Starts the current level again, at the start of the next frame");
	R::RegisterType<QuitGameNode>("Game/Quit Game", "Closes the game");
	R::RegisterType<GetGameNameNode>("Game/Get Game Name", "The game's name from the project's game settings");
}
