#pragma once

#include <memory>
#include <vector>

#include <tge/Math/Matrix4x4.h>
#include <tge/editor/Tools/ToolsInterface.h>

#include <tge/editor/Commands/TransformCommand.h>

namespace Tga
{
	struct Transform;
	class ViewportInterface;
	class ModelInstance;

	class Gizmos : public ToolsInterface {
	public:
		struct Snap 
		{ 
			bool snapPos = false;
			bool snapRot = false;
			bool snapScale = false;

			float pos = 100.f; 
			float rot = 45.f;
			float scale = 0.1f; 
		};
		Gizmos() = default;

		virtual void Draw() override;

		void DrawGizmos(const Camera& camera, ViewportInterface& aViewportInterface, Vector2i aViewportPos, Vector2i aViewportSize);

		uint16_t GetCurrentOperation() const { return myCurrentOperation; }
		void SetCurrentOperation(uint16_t currentMode) { myCurrentOperation = currentMode; }

		const Snap& GetSnappingInfo() const { return mySnap; }

	private:
		Vector3f myManipulationStartPos;
		Matrix4x4f myManipulationCurrentTransform;
		Matrix4x4f myManipulationInitialTransform;

		uint16_t myCurrentOperation = 7;
		uint16_t myCurrentMode = 0;

		bool myIsManipulating = false;

		Snap mySnap;
		
	};
}