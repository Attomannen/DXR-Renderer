#include <stdafx.h>
#include <memory>
#include "SceneObject.h"
#include <tge/scene/SceneObjectDefinitionManager.h>
#include <tge/scene/Scene.h>

using namespace Tga;

void SceneObject::SetPath(Scene* aScene, StringId aPath)
{ 
	if (aScene !=  nullptr)
		aScene->UpdateFolderCounts(myPath, aPath);

	myPath = aPath;
}

void SceneObject::CalculateEditablePropertySet(SceneObjectDefinitionManager& objectDefinitionManager, std::vector<PropertySourceAndOveride>& outResult)  const
{
	SceneObjectDefinition* objectDefinition = objectDefinitionManager.Get(mySceneObjectDefinitionName);

	if (objectDefinition == nullptr)
		return;

	for (const ScenePropertyDefinition& originalProperty : objectDefinition->GetProperties())
	{
		if ((originalProperty.flags & ScenePropertyFlags::IsPerInstance) == ScenePropertyFlags::None)
			continue;

		PropertySourceAndOveride& sourceAndOverride = outResult.emplace_back();
		sourceAndOverride.source = originalProperty;
		for (const SceneProperty& overrideProperty : myPropertyOverrides)
		{
			if (overrideProperty.name == originalProperty.name && overrideProperty.type == originalProperty.type)
			{
				sourceAndOverride.override = overrideProperty;
				break;
			}
		}
	}
}

void SceneObject::CalculateCombinedPropertySet(SceneObjectDefinitionManager& objectDefinitionManager, std::vector<ScenePropertyDefinition>& outResult) const
{
	SceneObjectDefinition* objectDefinition = objectDefinitionManager.Get(mySceneObjectDefinitionName);

	if (objectDefinition == nullptr)
		return;

	for (const ScenePropertyDefinition& originalProperty : objectDefinition->GetProperties())
	{
		bool hasOverride = false;

		if ((originalProperty.flags & ScenePropertyFlags::IsPerInstance) != ScenePropertyFlags::None)
		{
			for (const SceneProperty& overrideProperty : myPropertyOverrides)
			{
				if (overrideProperty.name == originalProperty.name)
				{
					if (overrideProperty.type == originalProperty.type)
					{
						ScenePropertyDefinition& combined = outResult.emplace_back(originalProperty);

						combined.value = overrideProperty.value;

						hasOverride = true;
					}
					else
					{
						// todo: error! Probably due to objectDefinition changing without the scene being updated
					}

					break;
				}
			}
		}

		if (!hasOverride)
			outResult.push_back(originalProperty);
	}
}