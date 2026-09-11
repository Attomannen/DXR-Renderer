#include <tge/editor/Commands/TransformCommand.h>

#include <tge/Math/Matrix4x4.h>
#include <tge/editor/CommandManager/CommandManager.h>
#include <tge/editor/Scene/ActiveScene.h>

using namespace Tga;

void TransformCommand::Begin(const std::span<const uint32_t>& someObjectIds)
{
	myFromTRS.clear();
	myToTRS.clear();
	for (const uint32_t id : someObjectIds)
	{
		myFromTRS.push_back(std::make_pair(id, GetActiveScene()->GetSceneObject(id)->GetTRS()));
	}
}

void TransformCommand::End()
{
	for (auto p : myFromTRS)
	{
		myToTRS.push_back(std::make_pair(p.first, GetActiveScene()->GetSceneObject(p.first)->GetTRS()));
	}

	CommandManager::DoCommand(std::make_shared<TransformCommand>(*this));
}

void TransformCommand::Execute()
{
	for (const auto& object : myToTRS) 
	{
		GetActiveScene()->GetSceneObject(object.first)->SetTRS(object.second);
	}
}

void TransformCommand::Undo() 
{
	for (const auto& object : myFromTRS) 
	{
		GetActiveScene()->GetSceneObject(object.first)->SetTRS(object.second);
	}
}

void TransformCommand::GetModifiedObjects(std::vector<uint32_t>& outModifiedObjects, bool& outHasModifedSceneFile) const
{
	for (auto& p : myToTRS)
	{
		outModifiedObjects.push_back(p.first);
	}

	outHasModifedSceneFile = false;
}