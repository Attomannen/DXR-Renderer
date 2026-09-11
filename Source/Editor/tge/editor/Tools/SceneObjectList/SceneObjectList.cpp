#include <tge/editor/Tools/SceneObjectList/SceneObjectList.h>

#include <tge/scene/Scene.h>

#include <tge/editor/Scene/SceneSelection.h>
#include <tge/editor/Scene/ActiveScene.h>

#include <imgui.h>
#include <algorithm>
#include <ranges>
#include <unordered_set>
#include <tge/editor/Editor.h>

#include <IconFontHeaders\IconsLucide.h>
#include <tge/editor/Tools/SceneObjectProperties/ChangeSceneObjectNameCommand.h>
#include <tge/editor/Tools/SceneObjectProperties/ChangeSceneObjectFolderCommand.h>

using namespace Tga;

void SceneObjectList::Draw()
{
	std::vector<bool> isFolderOpenStack;
	const auto& allObjects = GetActiveScene()->GetSceneObjects();

	const bool hasFilter = !myRequiredPropertyTypeIds.empty();
	const bool hasSearch = mySearchBuffer[0] != '\0';
	const bool searchDirty = myLastSearch != mySearchBuffer;
	const bool needsRebuild = searchDirty || mySceneDirty;

	SearchAndFilterBar(allObjects);

	if (needsRebuild)
	{
		BuildObjectList(allObjects, hasSearch, hasFilter);
	}

	const ImGuiTreeNodeFlags categoryFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth;
	const ImGuiTreeNodeFlags itemFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth;

	const std::vector<StringId>* previousPath = &myFolderPaths[StringId{}];

	bool isParentOpen = true;

	char buffer[128];

	isFolderOpenStack.clear();

	int selectEveryThingBeyondLevel = INT_MAX;

	const ImGuiPayload* pl = ImGui::GetDragDropPayload();
	if (pl != nullptr && pl->IsDataType("scene-object-list-item"))
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.f, 0.f, 1.f));
		ImGui::PushStyleColor(ImGuiCol_DragDropTarget, ImVec4(0.5f, 0.0f, 0.0f, 1.0f));

		ImGui::Selectable(ICON_LC_FOLDER_X " ungroup", false, ImGuiSelectableFlags_SpanAllColumns);
		if(ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("scene-object-list-item")) 
			{
				uint32_t *dropped = (uint32_t*)payload->Data;
				const size_t size = payload->DataSize / sizeof(uint32_t);

				Tga::StringId ungroup = Tga::StringRegistry::RegisterOrGetString("");

				for (size_t i=0; i<size; ++i)
				{
					auto &object = *allObjects.find(dropped[i])->second;

					if (ungroup != object.GetPath())
					{
						std::shared_ptr<ChangeSceneObjectFolderCommand> command = std::make_shared<ChangeSceneObjectFolderCommand>(
							dropped[i], ungroup, object.GetPath());
						CommandManager::DoCommand(command);
					}
				}
			}
			ImGui::EndDragDropTarget();
		}
		ImGui::PopStyleColor(2);
	}

	for (const std::pair<uint32_t, SceneObject*> p : mySortedObjects)
	{
		const std::vector<StringId>* path = &myFolderPaths[p.second->GetPath()];

		int sameSubsetCount = 0;
		if (previousPath == path)
		{
			sameSubsetCount = (int)path->size();
		}
		else
		{
			const int maxSubSetCount = (int)std::min(previousPath->size(), path->size());
			for (sameSubsetCount = 0; sameSubsetCount < maxSubSetCount; sameSubsetCount++)
			{
				if ((*path)[sameSubsetCount] != (*previousPath)[sameSubsetCount])
					break;
			}
		}

		if (sameSubsetCount != path->size())
		{
			while (isFolderOpenStack.size() > sameSubsetCount)
			{
				const bool isOpen = isFolderOpenStack.back();
				isFolderOpenStack.pop_back();

				if (isOpen)
				{
					ImGui::TreePop();
				}

				if (selectEveryThingBeyondLevel > isFolderOpenStack.size())
				{
					selectEveryThingBeyondLevel = INT_MAX;
				}
			}

			isParentOpen = true;
			if (!isFolderOpenStack.empty())
				isParentOpen = isFolderOpenStack.back();

			while (isFolderOpenStack.size() < path->size())
			{
				if (isParentOpen)
				{
					sprintf_s(buffer, ICON_LC_FOLDER " %s", (*path)[isFolderOpenStack.size()].GetString());

					isFolderOpenStack.push_back(ImGui::TreeNodeEx(buffer, categoryFlags));
					if(ImGui::BeginDragDropTarget())
					{
						if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("scene-object-list-item")) 
						{
							uint32_t *dropped = (uint32_t*)payload->Data;
							const size_t size = payload->DataSize / sizeof(uint32_t);

							for (size_t i=0; i<size; ++i)
							{
								auto &object = *allObjects.find(dropped[i])->second;

								char folder[128]{};
								strcpy_s(folder, (*path)[0].GetString());
								for (size_t j=1; j<isFolderOpenStack.size(); ++j)
								{
									sprintf_s(folder, "%s/%s", folder, (*path)[j].GetString());
								}
								Tga::StringId pathbufferid = Tga::StringRegistry::RegisterOrGetString(folder);

								if (Tga::StringRegistry::RegisterOrGetString(folder) != object.GetPath())
								{
									std::shared_ptr<ChangeSceneObjectFolderCommand> command = std::make_shared<ChangeSceneObjectFolderCommand>(
										dropped[i], pathbufferid, object.GetPath());
									CommandManager::DoCommand(command);
								}
							}
						}
						ImGui::EndDragDropTarget();
					}
					if(ImGui::BeginDragDropSource()) 
					{
						ImGui::SetDragDropPayload("scene-object-list-item", (void*)SceneSelection::GetActiveSceneSelection()->GetSelection().data(), SceneSelection::GetActiveSceneSelection()->GetSelection().size_bytes());
						ImGui::Text(buffer);
						ImGui::EndDragDropSource();
					}
					else if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
					{
						selectEveryThingBeyondLevel = (int)isFolderOpenStack.size();

						if (ImGui::GetIO().KeyShift == false) 
						{
							SceneSelection::GetActiveSceneSelection()->ClearSelection();
						}
					}

					isParentOpen = isFolderOpenStack.back();
				}
				else
				{
					isFolderOpenStack.push_back(false);
				}
			}
		}

		if (isParentOpen)
		{
			ImGuiTreeNodeFlags flags = itemFlags;
			if (SceneSelection::GetActiveSceneSelection()->Contains(p.first))
				flags |= ImGuiTreeNodeFlags_Selected;

			sprintf_s(buffer, ICON_LC_BOX " %s", p.second->GetName());

			ImGui::PushID(p.first);
			ImGui::TreeNodeEx(buffer, flags);
			if(ImGui::BeginDragDropSource()) 
			{
				if (!SceneSelection::GetActiveSceneSelection()->Contains(p.first))
				{
					SceneSelection::GetActiveSceneSelection()->ClearSelection();
					SceneSelection::GetActiveSceneSelection()->AddToSelection(p.first);
				}
				ImGui::SetDragDropPayload("scene-object-list-item", (void*)SceneSelection::GetActiveSceneSelection()->GetSelection().data(), SceneSelection::GetActiveSceneSelection()->GetSelection().size_bytes());
				ImGui::Text(buffer);
				ImGui::EndDragDropSource();
			}
			else if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
			{
				if (ImGui::GetIO().KeyShift == false) 
				{
					SceneSelection::GetActiveSceneSelection()->ClearSelection();
				}
				SceneSelection::GetActiveSceneSelection()->ToggleSelect(p.first);
			}
			ImGui::PopID();

			if (selectEveryThingBeyondLevel != INT_MAX)
			{
				SceneSelection::GetActiveSceneSelection()->AddToSelection(p.first);
			}
		}
		previousPath = path;
	}

	while (!isFolderOpenStack.empty())
	{
		const bool isOpen = isFolderOpenStack.back();
		isFolderOpenStack.pop_back();

		if (isOpen)
		{
			ImGui::TreePop();
		}
	}

	// TODO: this should use a tree view table instead!

	/*
	char buffer[512];

	if (ImGui::BeginListBox("Scene", ImGui::GetContentRegionAvail()))
	{
		for (uint32_t id : sortedObjects)
		{
			auto it = allObjects.find(id);

			sprintf_s(buffer, "%s: %s", it->second->GetPath().GetString(), it->second->GetName());

			ImGui::Selectable(buffer, SceneSelection::GetActiveSceneSelection()->Contains(it->first));
			if (ImGui::IsItemClicked())
			{
				if (ImGui::GetIO().KeyShift == false) {
					SceneSelection::GetActiveSceneSelection()->ClearSelection();
				}
				SceneSelection::GetActiveSceneSelection()->ToggleSelect(it->first);
			}
		}
		ImGui::EndListBox();
	}*/
}

void SceneObjectList::SetSceneDirty()
{
	mySceneDirty = true;
}

void SceneObjectList::SearchAndFilterBar(const std::unordered_map<uint32_t, std::shared_ptr<SceneObject>>& aAllObjects)
{
	ImGui::SetNextItemWidth(150);
	ImGui::InputTextWithHint("##Search", ICON_LC_SEARCH " Search objects...", mySearchBuffer, IM_ARRAYSIZE(mySearchBuffer));
	ImGui::SameLine();
	
	std::unordered_set<uint32_t> seenPropertyTypeIds;
	std::vector<const PropertyTypeBase*> availablePropertyTypes;

	for (const auto& object : aAllObjects | std::views::values)
	{
		std::vector<ScenePropertyDefinition> props;
		object->CalculateCombinedPropertySet(Editor::GetEditor()->GetSceneObjectDefinitionManager(), props);
		for (const auto& prop : props)
		{
			if (seenPropertyTypeIds.insert(prop.type->GetTypeId().id).second)
			{
				availablePropertyTypes.push_back(prop.type);
			}
		}
	}
	
	const char* filterLabel = mySelectedPropertyTypeIndex >= 0 
		? availablePropertyTypes[mySelectedPropertyTypeIndex]->GetName().GetString() 
		: "All";
	
	if (ImGui::Button(filterLabel))
	{
		ImGui::OpenPopup("PropertyTypeFilterPopup");
	}
	
	if (ImGui::BeginPopup("PropertyTypeFilterPopup"))
	{
		if (ImGui::Selectable("All", mySelectedPropertyTypeIndex == -1))
		{
			mySelectedPropertyTypeIndex = -1;
			myRequiredPropertyTypeIds.clear();
			mySceneDirty = true;
			ImGui::CloseCurrentPopup();
		}
		
		for (int i = 0; i < static_cast<int>(availablePropertyTypes.size()); ++i)
		{
			bool isSelected = mySelectedPropertyTypeIndex == i;
			if (ImGui::Selectable(availablePropertyTypes[i]->GetName().GetString(), isSelected))
			{
				mySelectedPropertyTypeIndex = i;
				myRequiredPropertyTypeIds = { availablePropertyTypes[i]->GetTypeId() };
				mySceneDirty = true;
				ImGui::CloseCurrentPopup();
			}
		}

		ImGui::EndPopup();
	}
	
	ImGui::SameLine();
	if (ImGui::Button("Clear"))
	{
		mySearchBuffer[0] = '\0';
		myRequiredPropertyTypeIds.clear();
		mySelectedPropertyTypeIndex = -1;
		mySceneDirty = true;
	}
	ImGui::Separator();
}

void SceneObjectList::BuildObjectList(const std::unordered_map<uint32_t, std::shared_ptr<SceneObject>>& aAllObjects, const bool aHasSearch, const bool aHasFilter)
{
	mySortedObjects.clear();
	myFolderPaths.clear();

	for (auto& object : aAllObjects)
	{
		if (aHasSearch)
		{
			std::string name = object.second->GetName();
			std::string searchStr = mySearchBuffer;
			std::ranges::transform(name, name.begin(), ::tolower);
			std::ranges::transform(searchStr, searchStr.begin(), ::tolower);

			if (name.find(searchStr) == std::string::npos)
			{
				continue;
			}
		}

		if (aHasFilter)
		{
			std::vector<ScenePropertyDefinition> sceneObjectProperties;
			object.second->CalculateCombinedPropertySet(Editor::GetEditor()->GetSceneObjectDefinitionManager(), sceneObjectProperties);

			bool matchesFilter = false;
			for (const auto& property : sceneObjectProperties)
			{
				if (std::ranges::find(myRequiredPropertyTypeIds, property.type->GetTypeId()) != myRequiredPropertyTypeIds.end())
				{
					matchesFilter = true;
					break;
				}
			}

			if (!matchesFilter)
			{
				continue;
			}
		}
		

		mySortedObjects.emplace_back(object.first, object.second.get());

		StringId folderPath = object.second->GetPath();
		auto it = myFolderPaths.find(folderPath);
		if (it == myFolderPaths.end())
		{
			std::vector<StringId>& path = myFolderPaths[folderPath];

			const char* remainingString = folderPath.GetString();

			while (true)
			{
				const char* nextSlash = strchr(remainingString, '/');

				if (nextSlash == nullptr)
				{
					if (remainingString[0] != 0)
						path.push_back(StringRegistry::RegisterOrGetString(remainingString));

					break;
				}

				std::string folder(remainingString, nextSlash);
				path.push_back(StringRegistry::RegisterOrGetString(folder));

				remainingString = nextSlash + 1;
			}
		}
		
		mySceneDirty = false;
	}

	std::ranges::sort(mySortedObjects, [&](const std::pair<uint32_t, SceneObject*>& a, const std::pair<uint32_t, SceneObject*>& b)
	{
		const StringId folderA = a.second->GetPath();
		const StringId folderB = b.second->GetPath();

		if (folderA == folderB)
		{
			return std::string_view(a.second->GetName()) < std::string_view(b.second->GetName());
		}
		return folderA < folderB;
	});

	myLastSearch = mySearchBuffer;
}

