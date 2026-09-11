#include <tge/editor/EditorGraphics/NullEditorGraphics.h>

using namespace Tga;


void NullSceneEditorGraphics::Draw(const SceneDrawParameters& someParameters)
{
	someParameters;
}

void NullAnimationClipEditorGraphics::Draw(const AnimationClipDrawParameters& someParameters)
{
	someParameters;
}
void Tga::NullObjectDefinitionEditorGraphics::Draw(ObjectDefinitionDrawParameters& someParameters)
{
	someParameters;
}

void NullMaterialEditorGraphics::Draw(const MaterialEditorDrawParameters& someParameters)
{
	someParameters;
}

std::unique_ptr<ObjectDefinitionEditorGraphicsBase> NullEditorGraphics::CreateObjectDefinitionGraphicsInterface() const
{
	return std::make_unique<NullObjectDefinitionEditorGraphics>();
}

std::unique_ptr<SceneEditorGraphicsBase> NullEditorGraphics::CreateSceneGraphicsInterface() const
{
	return std::make_unique<NullSceneEditorGraphics>();
}

std::unique_ptr<AnimationClipEditorGraphicsBase> NullEditorGraphics::CreateAnimationClipGraphicsInterface() const
{
	return std::make_unique<NullAnimationClipEditorGraphics>();
}

std::unique_ptr<MaterialEditorGraphicsBase> NullEditorGraphics::CreateMaterialGraphicsInterface() const
{
	return std::make_unique<NullMaterialEditorGraphics>();
}
