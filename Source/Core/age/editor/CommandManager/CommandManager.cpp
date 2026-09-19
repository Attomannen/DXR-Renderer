#include "stdafx.h"
#include "AbstractCommand.h"
#include "CommandManager.h"

#include <stack>

using namespace Ag;

static std::stack<std::shared_ptr<AbstractCommand>> locUndoStack;
static std::stack<std::shared_ptr<AbstractCommand>> locRedoStack;


CommandManager::CallbackRegistration* CommandManager::CallbackRegistration::ourLatestRegisteredCallback;

CommandManager::CallbackRegistration::CallbackRegistration(OnActionCallback callback)
	: myCallback(callback)
	, myPreviousRegisteredCallback(ourLatestRegisteredCallback)
{
	ourLatestRegisteredCallback = this;
}


void CommandManager::CallbackRegistration::CallCallbacks(CommandManager::Action action)
{
	CallbackRegistration* current = ourLatestRegisteredCallback;

	while (current != nullptr)
	{
		current->myCallback(action);
		current = current->myPreviousRegisteredCallback;
	}
}

void CommandManager::Clear()
{
	while (!locUndoStack.empty())
		locUndoStack.pop();
	while (!locRedoStack.empty())
		locRedoStack.pop();

	CallbackRegistration::CallCallbacks(Action::Clear);
}

void CommandManager::DoCommand(const std::shared_ptr<AbstractCommand>& aCommand) 
{
	aCommand->Execute();
	locUndoStack.push(aCommand);
	while (locRedoStack.empty() == false)
	{
		locRedoStack.pop();
	}

	CallbackRegistration::CallCallbacks(Action::Do);
}

const AbstractCommand* CommandManager::GetTopOfUndoStack()
{
	if (locUndoStack.empty())
		return nullptr;

	return locUndoStack.top().get();
}

void CommandManager::Undo() 
{
	if (locUndoStack.empty() == false)
	{
		CallbackRegistration::CallCallbacks(Action::PreUndo);

		locUndoStack.top()->Undo();
		locRedoStack.push(locUndoStack.top());
		locUndoStack.pop();

		CallbackRegistration::CallCallbacks(Action::PostUndo);
	}
}

void CommandManager::Redo() 
{
	if (locRedoStack.empty() == false)
	{
		CallbackRegistration::CallCallbacks(Action::PreRedo);

		locRedoStack.top()->Execute();
		locUndoStack.push(locRedoStack.top());
		locRedoStack.pop();

		CallbackRegistration::CallCallbacks(Action::PostRedo);
	}
}

bool CommandManager::CanUndo()
{
	return !locUndoStack.empty();
}

bool CommandManager::CanRedo()
{
	return !locRedoStack.empty();
}

std::vector<const AbstractCommand*> CommandManager::GetUndoHistory()
{
	std::vector<const AbstractCommand*> newestFirst;
	newestFirst.reserve(locUndoStack.size());
	std::stack<std::shared_ptr<AbstractCommand>> copy = locUndoStack;
	while (!copy.empty()) { newestFirst.push_back(copy.top().get()); copy.pop(); }
	return std::vector<const AbstractCommand*>(newestFirst.rbegin(), newestFirst.rend());
}

std::vector<const AbstractCommand*> CommandManager::GetRedoHistory()
{
	std::vector<const AbstractCommand*> nearestFirst;
	nearestFirst.reserve(locRedoStack.size());
	std::stack<std::shared_ptr<AbstractCommand>> copy = locRedoStack;
	while (!copy.empty()) { nearestFirst.push_back(copy.top().get()); copy.pop(); }
	return nearestFirst;
}