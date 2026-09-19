#pragma once
#include <age/editor/EditorGraphics/EditorGraphicsBase.h>

namespace Ag
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
	virtual std::unique_ptr<ParticleEditorGraphicsBase> CreateParticleGraphicsInterface() const override;

	ImTextureID GetTextureID(std::string_view aTexturePath) const override;
	void DrawLines(const Color* someColors, const Vector3f* someFromPositions, const Vector3f* someToPositions, unsigned int aCount) const override;
};

}
