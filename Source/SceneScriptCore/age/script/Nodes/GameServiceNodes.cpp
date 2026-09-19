#include <stdafx.h>
#include "GameServiceNodes.h"

#include "NodeHelpers.h"
#include <age/script/Contexts/GameScriptContext.h>

#include <type_traits>

using namespace Ag;
using namespace Ag::NodeHelpers;

namespace
{
	GameScriptContext* GetGame(ScriptExecutionContext& context)
	{
		return dynamic_cast<GameScriptContext*>(&context.GetUpdateContext());
	}

	// ---------------------------------------------------------------- objects

	class GetSelfNode : public ScriptNodeBase
	{
	public:
		void Init(const ScriptCreationContext& context) override { Out<int>(context, "Object"); }
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const GameScriptContext* game = GetGame(context);
			return Make<int>(game ? game->GetSelfObject() : -1);
		}
	};

	class GetPlayerPawnNode : public ScriptNodeBase
	{
	public:
		void Init(const ScriptCreationContext& context) override { Out<int>(context, "Object"); }
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const GameScriptContext* game = GetGame(context);
			return Make<int>(game ? game->GetPlayerPawn() : -1);
		}
	};

	class FindObjectNode : public ScriptNodeBase
	{
		ScriptPinId myName;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myName = In<StringId>(context, "Name");
			Out<int>(context, "Object");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const GameScriptContext* game = GetGame(context);
			return Make<int>(game ? game->FindObject(Read<StringId>(context, myName).GetString()) : -1);
		}
	};

	class FindObjectByDefinitionNode : public ScriptNodeBase
	{
		ScriptPinId myDefinition, myIndex;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myDefinition = In<StringId>(context, "Definition");
			myIndex = In<int>(context, "Index", 0);
			Out<int>(context, "Object");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const GameScriptContext* game = GetGame(context);
			return Make<int>(game ? game->FindObjectByDefinition(Read<StringId>(context, myDefinition).GetString(), Read<int>(context, myIndex)) : -1);
		}
	};

	class CountObjectsNode : public ScriptNodeBase
	{
		ScriptPinId myDefinition;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myDefinition = In<StringId>(context, "Definition");
			Out<int>(context, "Count");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const GameScriptContext* game = GetGame(context);
			return Make<int>(game ? game->CountObjects(Read<StringId>(context, myDefinition).GetString()) : 0);
		}
	};

	class IsObjectValidNode : public ScriptNodeBase
	{
		ScriptPinId myObject;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myObject = In<int>(context, "Object", -1);
			Out<bool>(context, "Valid");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const GameScriptContext* game = GetGame(context);
			return Make<bool>(game && game->IsObjectValid(Read<int>(context, myObject)));
		}
	};

	class GetLocationOfNode : public ScriptNodeBase
	{
		ScriptPinId myObject;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myObject = In<int>(context, "Object", -1);
			Out<Vector3f>(context, "Location");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const GameScriptContext* game = GetGame(context);
			return Make<Vector3f>(game ? game->GetObjectLocation(Read<int>(context, myObject)) : Vector3f(0.f, 0.f, 0.f));
		}
	};

	class GetForwardOfNode : public ScriptNodeBase
	{
		ScriptPinId myObject;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myObject = In<int>(context, "Object", -1);
			Out<Vector3f>(context, "Forward");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const GameScriptContext* game = GetGame(context);
			return Make<Vector3f>(game ? game->GetObjectForward(Read<int>(context, myObject)) : Vector3f(0.f, 0.f, 1.f));
		}
	};

	class SetLocationOfNode : public ScriptNodeBase
	{
		ScriptPinId myObject, myLocation, myOut;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			FlowIn(context, "Set");
			myObject = In<int>(context, "Object", -1);
			myLocation = In<Vector3f>(context, "Location", Vector3f(0.f, 0.f, 0.f));
			myOut = FlowOut(context, "");
		}
		ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId) const override
		{
			if (GameScriptContext* game = GetGame(context))
				game->SetObjectLocation(Read<int>(context, myObject), Read<Vector3f>(context, myLocation));
			context.TriggerOutputPin(myOut);
			return ScriptNodeResult::Finished;
		}
	};

	class DistanceBetweenNode : public ScriptNodeBase
	{
		ScriptPinId myA, myB;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myA = In<int>(context, "A", -1);
			myB = In<int>(context, "B", -1);
			Out<float>(context, "Distance");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const GameScriptContext* game = GetGame(context);
			if (!game) return Make<float>(0.f);
			return Make<float>((game->GetObjectLocation(Read<int>(context, myA)) - game->GetObjectLocation(Read<int>(context, myB))).Length());
		}
	};

	// ---------------------------------------------------------------- particles

	enum class ParticleAction { Activate, Deactivate, Reset, Burst };

	template <ParticleAction Action>
	class ParticleControlNode : public ScriptNodeBase
	{
		ScriptPinId myObject, myCount, myOut;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			FlowIn(context, "Run");
			myObject = In<int>(context, "Object", -1);
			if constexpr (Action == ParticleAction::Burst) myCount = In<int>(context, "Count", 10);
			myOut = FlowOut(context, "");
		}
		ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId) const override
		{
			if (GameScriptContext* game = GetGame(context))
			{
				const int object = Read<int>(context, myObject);
				if constexpr (Action == ParticleAction::Activate) game->SetParticlesActive(object, true);
				else if constexpr (Action == ParticleAction::Deactivate) game->SetParticlesActive(object, false);
				else if constexpr (Action == ParticleAction::Reset) game->ResetParticles(object);
				else game->BurstParticles(object, Read<int>(context, myCount));
			}
			context.TriggerOutputPin(myOut);
			return ScriptNodeResult::Finished;
		}
	};

	template <typename T>
	class SetParticleParameterNode : public ScriptNodeBase
	{
		ScriptPinId myObject, myName, myValue, myOut;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			FlowIn(context, "Set");
			myObject = In<int>(context, "Object", -1);
			myName = In<StringId>(context, "Parameter");
			myValue = In<T>(context, "Value");
			myOut = FlowOut(context, "");
		}
		ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId) const override
		{
			if (GameScriptContext* game = GetGame(context))
			{
				const int object = Read<int>(context, myObject);
				const char* name = Read<StringId>(context, myName).GetString();
				const T value = Read<T>(context, myValue);
				if constexpr (std::is_same_v<T, float>) game->SetParticleFloat(object, name, value);
				else if constexpr (std::is_same_v<T, Vector3f>) game->SetParticleVector(object, name, value);
				else game->SetParticleColor(object, name, value.myR, value.myG, value.myB, value.myA);
			}
			context.TriggerOutputPin(myOut);
			return ScriptNodeResult::Finished;
		}
	};

	// ---------------------------------------------------------------- sound

	class PlaySoundNode : public ScriptNodeBase
	{
		ScriptPinId mySound, myVolume, myLoop, myOut;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			FlowIn(context, "Play");
			mySound = In<StringId>(context, "Sound");
			myVolume = In<float>(context, "Volume", 1.f);
			myLoop = In<bool>(context, "Loop", false);
			myOut = FlowOut(context, "");
		}
		ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId) const override
		{
			if (GameScriptContext* game = GetGame(context))
				game->PlaySound(Read<StringId>(context, mySound).GetString(), Read<float>(context, myVolume), Read<bool>(context, myLoop));
			context.TriggerOutputPin(myOut);
			return ScriptNodeResult::Finished;
		}
	};

	class StopSoundNode : public ScriptNodeBase
	{
		ScriptPinId mySound, myOut;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			FlowIn(context, "Stop");
			mySound = In<StringId>(context, "Sound");
			myOut = FlowOut(context, "");
		}
		ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId) const override
		{
			if (GameScriptContext* game = GetGame(context))
				game->StopSound(Read<StringId>(context, mySound).GetString());
			context.TriggerOutputPin(myOut);
			return ScriptNodeResult::Finished;
		}
	};

	class SetSoundVolumeNode : public ScriptNodeBase
	{
		ScriptPinId mySound, myVolume, myOut;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			FlowIn(context, "Set");
			mySound = In<StringId>(context, "Sound");
			myVolume = In<float>(context, "Volume", 1.f);
			myOut = FlowOut(context, "");
		}
		ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId) const override
		{
			if (GameScriptContext* game = GetGame(context))
				game->SetSoundVolume(Read<StringId>(context, mySound).GetString(), Read<float>(context, myVolume));
			context.TriggerOutputPin(myOut);
			return ScriptNodeResult::Finished;
		}
	};

	class IsSoundPlayingNode : public ScriptNodeBase
	{
		ScriptPinId mySound;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			mySound = In<StringId>(context, "Sound");
			Out<bool>(context, "Playing");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const GameScriptContext* game = GetGame(context);
			return Make<bool>(game && game->IsSoundPlaying(Read<StringId>(context, mySound).GetString()));
		}
	};
}

void Ag::RegisterGameServiceNodes()
{
	using R = ScriptNodeTypeRegistry;
	R::RegisterType<GetSelfNode>("Object/Get Self", "This object, as a handle other nodes can use");
	R::RegisterType<GetPlayerPawnNode>("Object/Get Player Pawn", "The object the Game Mode spawned for the player, or -1");
	R::RegisterType<FindObjectNode>("Object/Find Object by Name", "The object with this name in the level, or -1");
	R::RegisterType<FindObjectByDefinitionNode>("Object/Find Object by Definition", "The Index-th object made from this .tgo (its name without folder or extension), or -1");
	R::RegisterType<CountObjectsNode>("Object/Count Objects of Definition", "How many objects were made from this .tgo");
	R::RegisterType<IsObjectValidNode>("Object/Is Object Valid", "True when the handle refers to an object in the level");
	R::RegisterType<GetLocationOfNode>("Object/Get Location Of", "Where an object is. Object -1 is this object");
	R::RegisterType<SetLocationOfNode>("Object/Set Location Of", "Moves an object. Object -1 is this object");
	R::RegisterType<GetForwardOfNode>("Object/Get Forward Of", "The direction an object faces");
	R::RegisterType<DistanceBetweenNode>("Object/Distance Between Objects", "How far apart two objects are");

	R::RegisterType<ParticleControlNode<ParticleAction::Activate>>("Particles/Activate Particles", "Starts an object's Particle System. Object -1 is this object");
	R::RegisterType<ParticleControlNode<ParticleAction::Deactivate>>("Particles/Deactivate Particles", "Stops spawning; the particles already alive finish");
	R::RegisterType<ParticleControlNode<ParticleAction::Reset>>("Particles/Reset Particles", "Removes every particle and starts again");
	R::RegisterType<ParticleControlNode<ParticleAction::Burst>>("Particles/Burst Particles", "Spawns Count particles at once from every emitter");
	R::RegisterType<SetParticleParameterNode<float>>("Particles/Set Particle Float", "Sets a float User Parameter of the Particle System");
	R::RegisterType<SetParticleParameterNode<Vector3f>>("Particles/Set Particle Vector", "Sets a vector User Parameter of the Particle System");
	R::RegisterType<SetParticleParameterNode<Color>>("Particles/Set Particle Color", "Sets a colour User Parameter of the Particle System");

	R::RegisterType<PlaySoundNode>("Audio/Play Sound", "Plays a sound file, e.g. Sounds/hit.wav. Volume is 0 to 1. Loop only counts the first time a sound is played");
	R::RegisterType<StopSoundNode>("Audio/Stop Sound", "Stops a sound");
	R::RegisterType<SetSoundVolumeNode>("Audio/Set Sound Volume", "Changes the volume of a sound, 0 to 1");
	R::RegisterType<IsSoundPlayingNode>("Audio/Is Sound Playing", "True while the sound plays");
}
