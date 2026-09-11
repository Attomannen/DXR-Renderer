#pragma once

#include <memory>
#include <tge/script/Contexts/ScriptCreationContext.h>
#include <tge/script/Contexts/ScriptExecutionContext.h>
#include <tge/script/ScriptCommon.h>

namespace Tga
{

	class ScriptNodeBase
	{
	public:
		virtual ~ScriptNodeBase() {}

		// todo custom editor UI api? also allow it to edit number of pins and be notified on connections
		// would be nice to be able to have a variable number of pins for example!

		virtual void Init(const ScriptCreationContext& context) = 0;

		virtual int GetRuntimeDataSize() const { return 0; }
		virtual void CreateRuntimeData(void* dataPtr) const { dataPtr; }
		virtual void DestroyRuntimeData(void* dataPtr) const { dataPtr; }


		virtual Property ReadPin(ScriptExecutionContext&, ScriptPinId) const { return {}; }

		virtual void LoadFromJson(const JsonData&) {}
		virtual void WriteToJson(JsonData&) const { return; }

		virtual ScriptNodeResult Execute(ScriptExecutionContext&, ScriptPinId) const { return ScriptNodeResult::Finished; }
		virtual bool ShouldExecuteAtStart() const { return false; }
	};

	template<typename T>
	class ScriptNodeWithRuntimeData : public ScriptNodeBase
	{
	public:
		int GetRuntimeDataSize() const override { return sizeof(T); }
		void CreateRuntimeData(void* dataPtr) const override { new(dataPtr)T(); }
		void DestroyRuntimeData(void* dataPtr) const override { static_cast<T*>(dataPtr)->~T(); }

		static T& GetRuntimeData(ScriptExecutionContext& context)
		{
			return *static_cast<T*>(context.GetNodeRuntimeDataPtr());
		}
	};

}