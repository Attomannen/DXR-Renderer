#include "stdafx.h"

#include "AnimationNodes.h"
#include "EvaluatePoseNode.h"
#include "PlayClipNode.h"
#include "BlendAnimationNode.h"
#include "AdjustAnimationSpeedNode.h"

#include <age/script/ScriptNodeTypeRegistry.h>

using namespace Ag;

void Ag::RegisterAnimationNodes()
{

	ScriptNodeTypeRegistry::RegisterType<PlayClipNode>("Animation/Play Clip", "Plays an animation clip");
	ScriptNodeTypeRegistry::RegisterType<EvaluatePoseNode>("Animation/Animate Model", "Sets pose of a model from an pose");
	ScriptNodeTypeRegistry::RegisterType<BlendPoseNode>("Animation/Blend Pose", "Blends two poses");
	ScriptNodeTypeRegistry::RegisterType<AdjustAnimationSpeedNode>("Animation/Adjust Speed", "Adjust Animation Speed");

}
