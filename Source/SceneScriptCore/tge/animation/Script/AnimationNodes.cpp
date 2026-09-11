#include "stdafx.h"

#include "AnimationNodes.h"
#include "EvaluatePoseNode.h"
#include "PlayClipNode.h"
#include "BlendAnimationNode.h"
#include "AdjustAnimationSpeedNode.h"

#include <tge/script/ScriptNodeTypeRegistry.h>

using namespace Tga;

void Tga::RegisterAnimationNodes()
{

	ScriptNodeTypeRegistry::RegisterType<PlayClipNode>("Animation/Play Clip", "Plays an animation clip");
	ScriptNodeTypeRegistry::RegisterType<EvaluatePoseNode>("Animation/Animate Model", "Sets pose of a model from an pose");
	ScriptNodeTypeRegistry::RegisterType<BlendPoseNode>("Animation/Blend Pose", "Blends two poses");
	ScriptNodeTypeRegistry::RegisterType<AdjustAnimationSpeedNode>("Animation/Adjust Speed", "Adjust Animation Speed");

}
