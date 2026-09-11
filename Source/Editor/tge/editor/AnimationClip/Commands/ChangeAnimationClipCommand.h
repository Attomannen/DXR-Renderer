#pragma once

#include <span>

#include <tge/editor/CommandManager/AbstractCommand.h>
#include <tge/animation/AnimationClip.h>

namespace Tga
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
