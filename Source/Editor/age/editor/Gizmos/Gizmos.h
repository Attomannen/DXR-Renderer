#pragma once

#include <memory>
#include <vector>

#include <age/Math/Matrix4x4.h>
#include <age/editor/Tools/ToolsInterface.h>

#include <age/editor/Commands/TransformCommand.h>

namespace Ag
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
		// Seeds mySnap from EditorSettings -- defined out-of-line (Gizmos.cpp)
		// so this header doesn't need to include EditorSettings.h.
		Gizmos();

		virtual void Draw() override;

		// Q/W/E/R, Space and Ctrl+` (Unreal's tool hotkeys). Call once per frame from the owning document.
		void UpdateShortcuts();

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