#include <age/editor/Scene/SceneLightSelection.h>

using namespace Ag;

static SceneLightSelection locSelection = SceneLightSelection::None;

SceneLightSelection Ag::GetSelectedSceneLight()
{
	return locSelection;
}

void Ag::SetSelectedSceneLight(SceneLightSelection aSelection)
{
	locSelection = aSelection;
}
