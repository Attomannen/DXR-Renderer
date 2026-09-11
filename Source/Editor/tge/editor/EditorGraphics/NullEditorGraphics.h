#pragma once

#include <tge/editor/EditorGraphics/EditorGraphicsBase.h>

namespace Tga
{

class NullObjectDefinitionEditorGraphics : public ObjectDefinitionEditorGraphicsBase
{
public:
	void Draw(ObjectDefinitionDrawParameters& someParameters) override;

};

class  NullSceneEditorGraphics : public SceneEditorGraphicsBase
{
public:
	void Draw(const SceneDrawParameters& someParameters) override;


};

class  NullAnimationClipEditorGraphics : public AnimationClipEditorGraphicsBase
{
public:
	void Draw(const AnimationClipDrawParameters& someParameters) override;


};

class NullMaterialEditorGraphics : public MaterialEditorGraphicsBase
{
public:
	void Draw(const MaterialEditorDrawParameters& someParameters) override;
};


class NullEditorGraphics : public EditorGraphicsBase
{
public:
	std::unique_ptr<ObjectDefinitionEditorGraphicsBase> CreateObjectDefinitionGraphicsInterface() const override;
	std::unique_ptr<SceneEditorGraphicsBase> CreateSceneGraphicsInterface() const override;
	std::unique_ptr<AnimationClipEditorGraphicsBase> CreateAnimationClipGraphicsInterface() const override;
	std::unique_ptr<MaterialEditorGraphicsBase> CreateMaterialGraphicsInterface() const override;

};

}