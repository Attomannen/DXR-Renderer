#pragma once

#include <span>

#include <tge/editor/CommandManager/AbstractCommand.h>
#include <tge/scene/Scene.h>

namespace Tga
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
