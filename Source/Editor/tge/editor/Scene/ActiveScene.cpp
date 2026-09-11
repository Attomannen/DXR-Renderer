#include <tge/editor/Scene/ActiveScene.h>

#include <tge/scene/Scene.h>

using namespace Tga;

static Scene* locActiveScene;

Scene* Tga::GetActiveScene()
{
	return locActiveScene;
}

void Tga::SetActiveScene(Scene* aScene)
{
	locActiveScene = aScene;
}