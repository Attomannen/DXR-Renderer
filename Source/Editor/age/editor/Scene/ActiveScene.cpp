#include <age/editor/Scene/ActiveScene.h>

#include <age/scene/Scene.h>

using namespace Ag;

static Scene* locActiveScene;

Scene* Ag::GetActiveScene()
{
	return locActiveScene;
}

void Ag::SetActiveScene(Scene* aScene)
{
	locActiveScene = aScene;
}