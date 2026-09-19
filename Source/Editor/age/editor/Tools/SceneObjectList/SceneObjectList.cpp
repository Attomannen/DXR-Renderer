#include <age/editor/Tools/SceneObjectList/SceneObjectList.h>

#include <age/scene/Scene.h>

#include <age/editor/Scene/SceneSelection.h>
#include <age/editor/Scene/ActiveScene.h>
#include <age/editor/Scene/SceneLightSelection.h>

#include <imgui.h>
#include <algorithm>
#include <ranges>
#include <unordered_set>
#include <age/editor/Editor.h>

#include <IconFontHeaders\IconsLucide.h>
#include <age/editor/Tools/SceneObjectProperties/ChangeSceneObjectNameCommand.h>
#include <age/editor/Tools/SceneObjectProperties/ChangeSceneObjectFolderCommand.h>
#include <age/editor/Commands/AddSceneObjectsCommand.h>
#include <age/editor/Commands/RemoveSceneObjectsCommand.h>
#include <age/editor/Tools/SceneObjectProperties/ChangePropertyOverridesCommand.h>
#include <age/scene/ScenePropertyTypes.h>
#include <age/settings/settings.h>

using namespace Ag;

namespace
{
	// Goes through AddSceneObjectsCommand so creating a light is undoable.
	void AddLightObject(const bool aSpot)
	{
		auto light = std::make_shared<SceneObject>();
		light->SetName(aSpot ? "Spot Light" : "Point Light");
		light->SetType(aSpot ? SceneObjectType::SpotLight : SceneObjectType::PointLight);
		light->GetPosition() = { 0.f, 150.f, 0.f };
		if (aSpot)
		{
			light->GetLightRange() = 15.f;   // metres
			light->GetEuler() = { 0.f, 0.f, 0.f };
		}
		std::vector<std::shared_ptr<SceneObject>> objects{ light };
		auto command = std::make_shared<AddSceneObjectsCommand>();
		command->AddObjects(objects);
		CommandManager::DoCommand(command);
		SceneSelection::GetActiveSceneSelection()->ClearSelection();
		SceneSelection::GetActiveSceneSelection()->AddToSelection(command->GetObjects()[0].first);
		SetSelectedSceneLight(SceneLightSelection::None);
	}

	const char* IconFor(const SceneObject& aObject)
	{
		switch (aObject.GetType())
		{
		case SceneObjectType::PointLight: return ICON_LC_LIGHTBULB " ";
		case SceneObjectType::SpotLight: return ICON_LC_FLASHLIGHT " ";
		default: return ICON_LC_BOX " ";
		}
	}
}

void SceneObjectList::Draw()
{
	std::vector<bool> isFolderOpenStack;
	const auto& allObjects = GetActiveScene()->GetSceneObjects();

	const bool hasFilter = !myRequiredPropertyTypeIds.empty();
	const bool hasSearch = mySearchBuffer[0] != '\0';
	const bool searchDirty = myLastSearch != mySearchBuffer;
	const bool needsRebuild = searchDirty || mySceneDirty;

	SearchAndFilterBar(allObjects);

	// The sun and ambient light belong to the scene, not to a SceneObject, so they are
	// selected through SceneLightSelection and listed first like Unreal's environment actors.
	if (!hasSearch && !hasFilter)
	{
		const SceneLightSelection current = GetSelectedSceneLight();
		const ImGuiTreeNodeFlags rowFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth;
		ImGui::TreeNodeEx(ICON_LC_SUN " Sun", rowFlags | (current == SceneLightSelection::Sun ? ImGuiTreeNodeFlags_Selected : 0));
		if (ImGui::IsItemClicked())
		{
			SetSelectedSceneLight(SceneLightSelection::Sun);
			SceneSelection::GetActiveSceneSelection()->ClearSelection();
		}
		ImGui::TreeNodeEx(ICON_LC_SUN_MEDIUM " Ambient", rowFlags | (current == SceneLightSelection::Ambient ? ImGuiTreeNodeFlags_Selected : 0));
		if (ImGui::IsItemClicked())
		{
			SetSelectedSceneLight(SceneLightSelection::Ambient);
			SceneSelection::GetActiveSceneSelection()->ClearSelection();
		}
	}

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
	std::vector<uint32_t> objectsToDelete;
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
		&& !ImGui::GetIO().WantTextInput
		&& ImGui::IsKeyPressed(ImGuiKey_Delete))
	{
		const std::span<const uint32_t> selection = SceneSelection::GetActiveSceneSelection()->GetSelection();
		for (const uint32_t id : selection)
			if (allObjects.contains(id)) objectsToDelete.push_back(id);
	}

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

				Ag::StringId ungroup = Ag::StringRegistry::RegisterOrGetString("");

				for (size_t i=0; i<size; ++i)
				{
					const auto objectIt = allObjects.find(dropped[i]);
					if (objectIt == allObjects.end() || !objectIt->second) continue;
					auto& droppedObject = *objectIt->second;

					if (ungroup != droppedObject.GetPath())
					{
						std::shared_ptr<ChangeSceneObjectFolderCommand> command = std::make_shared<ChangeSceneObjectFolderCommand>(
							dropped[i], ungroup, droppedObject.GetPath());
						CommandManager::DoCommand(command);
					}
				}
			}
			ImGui::EndDragDropTarget();
		}
		ImGui::PopStyleColor(2);
	}

	bool pointerOverRow = false;
	for (const uint32_t objectId : mySortedObjects)
	{
		const auto currentObject = allObjects.find(objectId);
		if (currentObject == allObjects.end() || !currentObject->second)
			continue; // The cache predates a scene mutation; rebuild next frame.
		SceneObject* object = currentObject->second.get();
		const std::vector<StringId>* path = &myFolderPaths[object->GetPath()];

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
								const auto objectIt = allObjects.find(dropped[i]);
								if (objectIt == allObjects.end() || !objectIt->second) continue;
								auto& droppedObject = *objectIt->second;

								// Was sprintf_s(folder, "%s/%s", folder, ...) in a loop -- folder
								// as both destination and a %s source in the same call is
								// undefined behaviour (overlapping read/write), not just an
								// unlikely-but-safe pattern. A plain std::string accumulator
								// sidesteps that and the fixed 128-byte overflow risk both.
								std::string folder = (*path)[0].GetString();
								for (size_t j = 1; j < isFolderOpenStack.size(); ++j)
								{
									folder += '/';
									folder += (*path)[j].GetString();
								}
								Ag::StringId pathbufferid = Ag::StringRegistry::RegisterOrGetString(folder);

								if (pathbufferid != droppedObject.GetPath())
								{
									std::shared_ptr<ChangeSceneObjectFolderCommand> command = std::make_shared<ChangeSceneObjectFolderCommand>(
										dropped[i], pathbufferid, droppedObject.GetPath());
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
			if (SceneSelection::GetActiveSceneSelection()->Contains(objectId))
				flags |= ImGuiTreeNodeFlags_Selected;

			ImGui::PushID(objectId);
			if (myRenameObject == objectId)
			{
				ImGui::SetNextItemWidth(-1);
				if (myFocusRename) { ImGui::SetKeyboardFocusHere(); myFocusRename = false; }
				const bool submitted = ImGui::InputText("##Rename", myRenameBuffer, IM_ARRAYSIZE(myRenameBuffer), ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue);
				if (submitted || ImGui::IsItemDeactivatedAfterEdit())
				{
					if (myRenameBuffer[0] != '\0' && strcmp(myRenameBuffer, object->GetName()) != 0)
						CommandManager::DoCommand(std::make_shared<ChangeSceneObjectNameCommand>(objectId, myRenameBuffer, object->GetName()));
					myRenameObject = 0;
				}
				if (ImGui::IsKeyPressed(ImGuiKey_Escape)) myRenameObject = 0;
			}
			else
			{
				sprintf_s(buffer, "%s%s", IconFor(*object), object->GetName());
				ImGui::TreeNodeEx(buffer, flags);
			}
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(".tgmat"))
				{
					const fs::path materialPath = static_cast<const char*>(payload->Data);
						const StringId modelName = "Model"_tgaid;
						SceneProperty oldProperty{};
						for (const SceneProperty& property : object->GetPropertyOverrides())
							if (property.name == modelName) { oldProperty = property; break; }

						// TGO definitions usually own the model path; a TGS instance
						// commonly stores only per-instance overrides. Preserve the
						// resolved value before replacing texture assignments.
						SceneModel resolvedModel{};
						std::vector<ScenePropertyDefinition> properties;
						object->CalculateCombinedPropertySet(Editor::GetEditor()->GetSceneObjectDefinitionManager(), properties);
						for (const ScenePropertyDefinition& property : properties)
							if (property.name == modelName && property.type == GetPropertyType<CopyOnWriteWrapper<SceneModel>>())
							{
								resolvedModel = property.value.Get<CopyOnWriteWrapper<SceneModel>>()->Get();
								break;
							}
						SceneProperty newProperty{};
						newProperty.name = modelName;
						newProperty.type = GetPropertyType<CopyOnWriteWrapper<SceneModel>>();
						auto model = CopyOnWriteWrapper<SceneModel>::Create();
						SceneModel& sceneModel = model.Edit();
						sceneModel = resolvedModel;
						for (int mesh = 0; mesh < MAX_MESHES_PER_MODEL; ++mesh)
							sceneModel.materials[mesh] = StringRegistry::RegisterOrGetString(materialPath.generic_string());
						newProperty.value = Property::Create<CopyOnWriteWrapper<SceneModel>>(model);
						CommandManager::DoCommand(std::make_shared<ChangePropertyOverridesCommand>(objectId, newProperty, oldProperty));
						mySceneDirty = true;
				}
				ImGui::EndDragDropTarget();
			}

			if (myRenameObject != objectId && ImGui::IsItemHovered() && ImGui::IsKeyPressed(ImGuiKey_F2))
			{
				strncpy_s(myRenameBuffer, object->GetName(), sizeof(myRenameBuffer));
				myRenameObject = objectId;
				myFocusRename = true;
			}
			if(ImGui::BeginDragDropSource()) 
			{
				if (!SceneSelection::GetActiveSceneSelection()->Contains(objectId))
				{
					SceneSelection::GetActiveSceneSelection()->ClearSelection();
					SceneSelection::GetActiveSceneSelection()->AddToSelection(objectId);
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
				SceneSelection::GetActiveSceneSelection()->ToggleSelect(objectId);
				SetSelectedSceneLight(SceneLightSelection::None);
			}
			pointerOverRow |= ImGui::IsItemHovered();
			if (myRenameObject != objectId && ImGui::IsItemClicked(ImGuiMouseButton_Right)
				&& !SceneSelection::GetActiveSceneSelection()->Contains(objectId))
			{
				// Right-clicking an unselected row acts on that row, like every other editor.
				SceneSelection::GetActiveSceneSelection()->ClearSelection();
				SceneSelection::GetActiveSceneSelection()->AddToSelection(objectId);
				SetSelectedSceneLight(SceneLightSelection::None);
			}
			if (myRenameObject != objectId && ImGui::BeginPopupContextItem("SceneObjectContext"))
			{
				const size_t selectedCount = SceneSelection::GetActiveSceneSelection()->GetSelection().size();
				if (ImGui::MenuItem(ICON_LC_PENCIL_LINE "  Rename", "F2", false, selectedCount <= 1))
				{
					strncpy_s(myRenameBuffer, object->GetName(), sizeof(myRenameBuffer));
					myRenameObject = objectId;
					myFocusRename = true;
				}
				if (ImGui::MenuItem(ICON_LC_COPY "  Duplicate", "Ctrl+D"))
				{
					auto duplicate = std::make_shared<SceneObject>(*object);
					const std::string duplicateName = std::string(object->GetName()) + " Copy";
					duplicate->SetName(duplicateName.c_str());
					std::vector<std::shared_ptr<SceneObject>> objects{ duplicate };
					auto command = std::make_shared<AddSceneObjectsCommand>();
					command->AddObjects(objects);
					CommandManager::DoCommand(command);
				}
				ImGui::Separator();
				if (ImGui::MenuItem(ICON_LC_TRASH_2 "  Delete", "Del"))
				{
					for (const uint32_t selectedId : SceneSelection::GetActiveSceneSelection()->GetSelection())
						if (allObjects.contains(selectedId)) objectsToDelete.push_back(selectedId);
				}
				ImGui::EndPopup();
			}
			ImGui::PopID();

			if (selectEveryThingBeyondLevel != INT_MAX)
			{
				SceneSelection::GetActiveSceneSelection()->AddToSelection(objectId);
			}
		}
		previousPath = path;
	}

	// Right-click on empty space: what the Add button offers.
	if (!pointerOverRow && ImGui::IsWindowHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
		ImGui::OpenPopup("OutlinerEmptyContext");
	if (ImGui::BeginPopup("OutlinerEmptyContext"))
	{
		if (ImGui::MenuItem(ICON_LC_LIGHTBULB "  Add Point Light"))
		{
			AddLightObject(false);
			mySceneDirty = true;
		}
		if (ImGui::MenuItem(ICON_LC_FLASHLIGHT "  Add Spot Light"))
		{
			AddLightObject(true);
			mySceneDirty = true;
		}
		ImGui::EndPopup();
	}

	if (!objectsToDelete.empty())
	{
		auto command = std::make_shared<RemoveSceneObjectsCommand>();
		command->AddObjects(objectsToDelete);
		CommandManager::DoCommand(command);
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

	ImGui::Separator();
	ImGui::TextDisabled("%d actors", (int)allObjects.size());

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
	if (ImGui::Button(ICON_LC_PLUS " Add"))
	{
		ImGui::OpenPopup("OutlinerAddPopup");
	}
	if (ImGui::BeginPopup("OutlinerAddPopup"))
	{
		if (ImGui::MenuItem(ICON_LC_LIGHTBULB " Point Light"))
		{
			AddLightObject(false);
			mySceneDirty = true;
		}
		if (ImGui::MenuItem(ICON_LC_FLASHLIGHT " Spot Light"))
		{
			AddLightObject(true);
			mySceneDirty = true;
		}
		ImGui::EndPopup();
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-ImGui::CalcTextSize(ICON_LC_LIST_FILTER).x - ImGui::GetStyle().FramePadding.x * 2.f - ImGui::GetStyle().ItemSpacing.x);
	ImGui::InputTextWithHint("##Search", ICON_LC_SEARCH " Search...", mySearchBuffer, IM_ARRAYSIZE(mySearchBuffer));
	ImGui::SameLine();

	// Only every object's *type* changes what's in the dropdown; typing in
	// the search box (which does not set mySceneDirty) does not, so this
	// stays cached across those frames instead of recomputing unconditionally.
	if (mySceneDirty)
	{
		std::unordered_set<uint32_t> seenPropertyTypeIds;
		myAvailablePropertyTypes.clear();

		for (const auto& object : aAllObjects | std::views::values)
		{
			std::vector<ScenePropertyDefinition> props;
			object->CalculateCombinedPropertySet(Editor::GetEditor()->GetSceneObjectDefinitionManager(), props);
			for (const auto& prop : props)
			{
				if (seenPropertyTypeIds.insert(prop.type->GetTypeId().id).second)
				{
					myAvailablePropertyTypes.push_back(prop.type);
				}
			}
		}
	}
	std::vector<const PropertyTypeBase*>& availablePropertyTypes = myAvailablePropertyTypes;

	const bool filterActive = mySelectedPropertyTypeIndex >= 0;
	if (filterActive) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
	const bool filterClicked = ImGui::Button(ICON_LC_LIST_FILTER);
	if (filterActive) ImGui::PopStyleColor();
	if (filterActive && ImGui::IsItemHovered())
		ImGui::SetTooltip("Showing objects with: %s", availablePropertyTypes[mySelectedPropertyTypeIndex]->GetName().GetString());
	if (filterClicked)
	{
		ImGui::OpenPopup("PropertyTypeFilterPopup");
	}
	
	if (ImGui::BeginPopup("PropertyTypeFilterPopup"))
	{
		ImGui::TextDisabled("Show only objects with");
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
		

		mySortedObjects.push_back(object.first);

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

	std::ranges::sort(mySortedObjects, [&](const uint32_t a, const uint32_t b)
	{
		const SceneObject* objectA = aAllObjects.at(a).get();
		const SceneObject* objectB = aAllObjects.at(b).get();
		const StringId folderA = objectA->GetPath();
		const StringId folderB = objectB->GetPath();

		if (folderA == folderB)
		{
			return std::string_view(objectA->GetName()) < std::string_view(objectB->GetName());
		}
		return folderA < folderB;
	});

	myLastSearch = mySearchBuffer;
}

