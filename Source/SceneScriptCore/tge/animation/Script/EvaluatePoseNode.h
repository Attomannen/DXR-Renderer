#include <tge/animation/Pose.h>
#include <tge/script/ScriptCommon.h>
#include <tge/script/ScriptNodeBase.h>

namespace Tga
{
	class EvaluatePoseNode : public ScriptNodeBase
	{
		ScriptPinId myPoseInPin;
		ScriptPinId myModelPropertyNameIn;

	public:
		void Init(const ScriptCreationContext& context) override;
		ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId) const override;
		bool ShouldExecuteAtStart() const override { return true; }
	};
}