#pragma once

#include <span>

#include <age/editor/CommandManager/AbstractCommand.h>
#include <age/scene/Scene.h>

namespace Ag
{
	class ChangePropertiesCommand : public AbstractCommand
	{
	public:
		enum class Action
		{
			Invalid,
			Edit,
			Add, 
			Remove
		};
		ChangePropertiesCommand(SceneObjectDefinition& aSceneObjectDefinition, ChangePropertiesCommand::Action aAction, const ScenePropertyDefinition& aNewPropertyDefinition, const ScenePropertyDefinition& aOldPropertyDefinition);

		void Execute() override;
		void Undo() override;

	private:
		SceneObjectDefinition* mySceneObjectDefinition;
		Action myAction;
		ScenePropertyDefinition myOldValue;
		ScenePropertyDefinition myNewValue;
	};
}
