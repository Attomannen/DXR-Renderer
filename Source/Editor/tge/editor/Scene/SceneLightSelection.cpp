#include <tge/editor/Scene/SceneLightSelection.h>

using namespace Tga;

static SceneLightSelection locSelection = SceneLightSelection::None;

SceneLightSelection Tga::GetSelectedSceneLight()
{
	return locSelection;
}

void Tga::SetSelectedSceneLight(SceneLightSelection aSelection)
{
	locSelection = aSelection;
}
