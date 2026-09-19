#include <age/animation/Pose.h>
#include <age/script/ScriptCommon.h>
#include <age/script/ScriptNodeBase.h>

namespace Ag
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