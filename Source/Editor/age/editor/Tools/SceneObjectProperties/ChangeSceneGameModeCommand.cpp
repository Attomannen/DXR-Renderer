#include <age/editor/Tools/SceneObjectProperties/ChangeSceneGameModeCommand.h>

#include <age/editor/CommandManager/CommandManager.h>
#include <age/editor/Scene/ActiveScene.h>

using namespace Ag;

ChangeSceneGameModeCommand::ChangeSceneGameModeCommand(const std::string& aNewPath, const std::string& aOldPath)
	: myNewPath(aNewPath)
	, myOldPath(aOldPath)
{
}

void ChangeSceneGameModeCommand::Execute()
{
	if (Scene* scene = GetActiveScene())
		scene->SetGameMode(myNewPath);
}

void ChangeSceneGameModeCommand::Undo()
{
	if (Scene* scene = GetActiveScene())
		scene->SetGameMode(myOldPath);
}

void ChangeSceneGameModeCommand::GetModifiedObjects(std::vector<uint32_t>& outModifiedObjects, bool& outHasModifedSceneFile) const
{
	(void)outModifiedObjects;
	outHasModifedSceneFile = true;
}
