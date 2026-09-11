#pragma once

#include <memory>

namespace Tga
{
	class AbstractCommand;



	class CommandManager
	{
	public:
		enum class Action
		{
			Do,
			PreUndo,
			PostUndo,
			PreRedo,
			PostRedo,
			Clear,
		};

		typedef void (*OnActionCallback)(CommandManager::Action action);

		class CallbackRegistration
		{
		// Sets up a linked list of Callback Registrations using static initialization
		// To use, declare a CallbackRegistration static variable in a cpp-file, with the appropiate callback
		private:
			static CallbackRegistration* ourLatestRegisteredCallback;
			CallbackRegistration* myPreviousRegisteredCallback;
			OnActionCallback myCallback;
		public:
			CallbackRegistration(OnActionCallback callback);
			static void CallCallbacks(Action action);
		};


		static void DoCommand(const std::shared_ptr<AbstractCommand>& aCommand);
		static const AbstractCommand* GetTopOfUndoStack();

		static void Clear();
		static void Undo();
		static void Redo();
		static bool CanUndo();
		static bool CanRedo();
	};



}