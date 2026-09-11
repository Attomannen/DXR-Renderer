#pragma once
#include <tge/editor/EditorGraphics/EditorGraphicsBase.h>

namespace Tga
{

class ObjectDefinitionEditorGraphicsBase;
class SceneEditorGraphicsBase;
class AnimationClipEditorGraphicsBase;

class DefaultEditorGraphics : public EditorGraphicsBase
{
public:
	DefaultEditorGraphics();
	virtual std::unique_ptr<ObjectDefinitionEditorGraphicsBase> CreateObjectDefinitionGraphicsInterface() const override;
	virtual std::unique_ptr<SceneEditorGraphicsBase> CreateSceneGraphicsInterface() const override;
	virtual std::unique_ptr<AnimationClipEditorGraphicsBase> CreateAnimationClipGraphicsInterface() const override;
	virtual std::unique_ptr<MaterialEditorGraphicsBase> CreateMaterialGraphicsInterface() const override;

	ImTextureID GetTextureID(std::string_view aTexturePath) const override;
	void DrawLines(const Color* someColors, const Vector3f* someFromPositions, const Vector3f* someToPositions, unsigned int aCount) const override;
};

}
