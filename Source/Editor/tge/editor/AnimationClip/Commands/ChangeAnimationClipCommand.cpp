#include <tge/editor/AnimationClip/Commands/ChangeAnimationClipCommand.h>

#include <tge/editor/CommandManager/CommandManager.h>

#include <algorithm>

using namespace Tga;

ChangeAnimationClipCommand::ChangeAnimationClipCommand(AnimationClip& aAnimationClipToModify, AnimationClip aModifiedClip)
	: myClipToModify(&aAnimationClipToModify)
	, myModifiedClip(aModifiedClip)
{}

void ChangeAnimationClipCommand::Execute()
{
	std::swap(*myClipToModify, myModifiedClip);
}

void ChangeAnimationClipCommand::Undo()
{
	std::swap(*myClipToModify, myModifiedClip);
}