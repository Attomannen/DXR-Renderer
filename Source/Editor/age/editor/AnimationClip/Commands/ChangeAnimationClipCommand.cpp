#include <age/editor/AnimationClip/Commands/ChangeAnimationClipCommand.h>

#include <age/editor/CommandManager/CommandManager.h>

#include <algorithm>

using namespace Ag;

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