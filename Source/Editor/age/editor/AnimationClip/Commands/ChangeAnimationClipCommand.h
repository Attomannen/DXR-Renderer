#pragma once

#include <span>

#include <age/editor/CommandManager/AbstractCommand.h>
#include <age/animation/AnimationClip.h>

namespace Ag
{
	class ChangeAnimationClipCommand : public AbstractCommand
	{
	public:
		ChangeAnimationClipCommand(AnimationClip& aAnimationClipToModify, AnimationClip aModifiedClip);

		void Execute() override;
		void Undo() override;

	private:
		AnimationClip* myClipToModify;
		AnimationClip myModifiedClip;

	};
}
