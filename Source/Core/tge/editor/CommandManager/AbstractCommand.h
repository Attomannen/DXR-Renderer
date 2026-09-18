#pragma once

namespace Tga
{
	class AbstractCommand
	{
	public:
		virtual void Execute() = 0;
		virtual void Undo() = 0;
		virtual ~AbstractCommand() {};

		// For the Undo History panel (Editor.cpp). Defaults to a generic
		// label rather than being pure virtual, so existing command classes
		// keep compiling unchanged and can be given a real name later
		// incrementally instead of all at once.
		virtual const char* GetName() const { return "Edit"; }
	};
}