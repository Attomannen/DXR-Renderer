#include <tge/editor/ObjectDefinition/Commands/ChangePropertiesCommand.h>

#include <tge/editor/CommandManager/CommandManager.h>
#include <tge/log/Log.h>
#include <iostream>

#include <algorithm>

using namespace Tga;

ChangePropertiesCommand::ChangePropertiesCommand(SceneObjectDefinition& aSceneObjectDefinition, ChangePropertiesCommand::Action aAction, const ScenePropertyDefinition& aNewPropertyDefinition, const ScenePropertyDefinition& aOldPropertyDefinition)
	: mySceneObjectDefinition(&aSceneObjectDefinition)
	, myAction(aAction)
	, myNewValue(aNewPropertyDefinition)
	, myOldValue(aOldPropertyDefinition)
{}

static void SortByName(std::vector<ScenePropertyDefinition>& aProperties)
{
	std::sort(aProperties.begin(), aProperties.end(), [](const ScenePropertyDefinition& a, const ScenePropertyDefinition& b)
		{
			if (a.groupName == b.groupName)
			{
				return strcmp(a.name.GetString(), b.name.GetString()) < 0;
			}

			return strcmp(a.groupName.GetString(), b.groupName.GetString()) < 0;
		});
}

static bool IsNameAvailable(std::vector<ScenePropertyDefinition>& aProperties, StringId aId)
{
	for (int i = 0; i < aProperties.size(); i++)
	{
		if (aProperties[i].name == aId)
		{
			return false;
		}
	}

	return true;
}

void ChangePropertiesCommand::Execute()
{
	std::vector<ScenePropertyDefinition>& properties = mySceneObjectDefinition->EditProperties();

	switch (myAction)
	{
		case Action::Add:
		{
			if (!IsNameAvailable(properties, myNewValue.name))
			{
				myAction = Action::Invalid;
				ERROR_PRINT("Name is not unique");

				return;
				// Todo: need a way to report errors in the tool.
			}

			properties.push_back(myNewValue);
			SortByName(properties);

			break;
		}
		case Action::Remove:
		{
			for (int i = 0; i < properties.size(); i++)
			{
				if (properties[i].name == myOldValue.name)
				{
					properties.erase(properties.begin() + i);
					break;
				}
			}
			break;
		}
		case Action::Edit:
		{
			bool needToValidateAndSort = myNewValue.name != myOldValue.name;
			bool needToSort = needToValidateAndSort || myNewValue.groupName != myOldValue.groupName;

			if (needToValidateAndSort)
			{
				if (!IsNameAvailable(properties, myNewValue.name))
				{
					myAction = Action::Invalid;
					ERROR_PRINT("Name is not unique");

					return;
					// Todo: need a way to report errors in the tool.
				}
			}

			for (int i = 0; i < properties.size(); i++)
			{
				if (properties[i].name == myOldValue.name)
				{
					properties[i] = myNewValue;
					break;
				}
			}

			if (needToSort)
			{
				SortByName(properties);
			}

			break;
		}
	}
}

void ChangePropertiesCommand::Undo()
{
	std::vector<ScenePropertyDefinition>& properties = mySceneObjectDefinition->EditProperties();

	switch (myAction)
	{
	case Action::Add:
	{
		for (int i = 0; i < properties.size(); i++)
		{
			if (properties[i].name == myNewValue.name)
			{
				properties.erase(properties.begin() + i);
				break;
			}
		}
		break;
	}
	case Action::Remove:
	{
		properties.push_back(myOldValue);
		SortByName(properties);

		break;
	}
	case Action::Edit:
	{
		bool needToSort = myNewValue.name != myOldValue.name;

		for (int i = 0; i < properties.size(); i++)
		{
			if (properties[i].name == myNewValue.name)
			{
				properties[i] = myOldValue;
				break;
			}
		}

		if (needToSort)
		{
			SortByName(properties);
		}

		break;
	}
	}
}