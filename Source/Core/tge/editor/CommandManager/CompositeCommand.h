#pragma once

#include <memory>
#include <vector>

#include "AbstractCommand.h"

namespace Tga
{
	// Several commands that undo and redo as one step. Build it with Do(), which runs each
	// command right away (later commands can use what earlier ones created), then hand the
	// composite to CommandManager::DoCommand, which skips the first execution.
	class CompositeCommand : public AbstractCommand
	{
	public:
		explicit CompositeCommand(const char* name = "Edit") : myName(name) {}

		void Do(const std::shared_ptr<AbstractCommand>& command)
		{
			command->Execute();
			myCommands.push_back(command);
		}

		bool IsEmpty() const { return myCommands.empty(); }

		void Execute() override
		{
			if (myIsFirstExecution)
			{
				myIsFirstExecution = false;
				return;
			}
			for (const std::shared_ptr<AbstractCommand>& command : myCommands)
				command->Execute();
		}

		void Undo() override
		{
			for (auto it = myCommands.rbegin(); it != myCommands.rend(); ++it)
				(*it)->Undo();
		}

		const char* GetName() const override { return myName; }

	private:
		const char* myName;
		bool myIsFirstExecution = true;
		std::vector<std::shared_ptr<AbstractCommand>> myCommands;
	};
}
