#include <age/editor/Tools/SceneObjectProperties/ChangeSceneEnvironmentTextureCommand.h>

#include <age/editor/CommandManager/CommandManager.h>
#include <age/editor/Scene/ActiveScene.h>

using namespace Ag;

ChangeSceneEnvironmentTextureCommand::ChangeSceneEnvironmentTextureCommand(const std::string& aNewPath, const std::string& aOldPath)
	: myNewPath(aNewPath)
	, myOldPath(aOldPath)
{
}

void ChangeSceneEnvironmentTextureCommand::Execute()
{
	if (Scene* scene = GetActiveScene())
		scene->SetEnvironmentTexturePath(myNewPath);
}

void ChangeSceneEnvironmentTextureCommand::Undo()
{
	if (Scene* scene = GetActiveScene())
		scene->SetEnvironmentTexturePath(myOldPath);
}

void ChangeSceneEnvironmentTextureCommand::GetModifiedObjects(std::vector<uint32_t>& outModifiedObjects, bool& outHasModifedSceneFile) const
{
	(void)outModifiedObjects;
	outHasModifedSceneFile = true;
}
