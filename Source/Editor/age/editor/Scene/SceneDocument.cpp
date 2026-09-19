#include "stdafx.h"

#include <commdlg.h>
#include <cstdlib>
#include <regex>
#include <fstream>

#include <nlohmann/json.hpp>

#include <age/editor/Scene/SceneDocument.h>

#include <imguizmo/ImGuizmo.h>
#include <age/input/InputManager.h>

#include <age/editor/CommandManager/CommandManager.h>

#include <age/graphics/DX11.h>
#include <age/imgui/ImGuiInterface.h>
#include <age/scene/SceneSerialize.h>
#include <age/scene/ScenePropertyTypes.h>

#include <age/math/BoxSphereBounds.h>

#include <age/editor/Scene/SceneSelection.h>
#include <age/editor/Scene/ActiveScene.h>

#include <age/editor/Commands/AddSceneObjectsCommand.h>
#include <age/editor/Commands/RemoveSceneObjectsCommand.h>
#include <age/scene/ScenePropertyTypes.h>

#include <age/editor/Editor.h>

#include <age/editor/Tools/Viewport/Viewport.h>
#include <age/editor/Tools/Viewport/CollisionOverlay.h>
#include <age/editor/Tools/ProjectRunControls/ProjectRunControls.h>
#include <age/editor/FileDialog/FileDialog.h>
#include <age/editor/imgui_widgets/imgui_widgets.h>
#include "imgui_internal.h" // for DockBuilder Api

#include <IconFontHeaders/IconsLucide.h>

#include "age/Application.h"


using namespace Ag;



//SceneDocument::~SceneDocument()
void SceneDocument::Close()
{

}

void SceneDocument::Init(std::string_view path)
{
	Document::Init(path);
	//myNavmeshCreationTool.Init();

	myViewport.Init();
	myViewport.GetGrid().SetGridLineExtreme(2000.0f);

	myScene = Editor::GetEditor()->GetEditorSceneManager().Get(path);
	// EditorSceneManager::Get() returns null when the .tgs doesn't exist on
	// disk (deleted/moved externally after the Content Browser listed it, or a
	// stale path from elsewhere) -- myScene->GetName() a few lines down would
	// otherwise be a null deref. Match ObjectDefinitionDocument::Init()'s own
	// convention (throw, let the caller's try/catch -- see ContentBrowser's
	// .tgs double-click handler -- turn it into a status message).
	if (!myScene)
		throw std::runtime_error("Could not load scene: " + std::string(path));
	myGraphics = Editor::GetEditor()->GetEditorGraphics().CreateSceneGraphicsInterface();

	char buffer[512];
	char asterix[2] = {0, 0};

	sprintf_s(buffer, "%s%s###Document:%s", myScene->GetName(), asterix, myScene->GetPath());
	myImGuiName = StringRegistry::RegisterOrGetString(buffer);

	sprintf_s(buffer, "Viewport##Document:%s", path.data());
	myPanelWindowNames[(size_t)Panels::Viewport] = buffer;
	sprintf_s(buffer, "Details##Document:%s", path.data());
	myPanelWindowNames[(size_t)Panels::Properties] = buffer;
	sprintf_s(buffer, "Outliner##Document:%s", path.data());
	myPanelWindowNames[(size_t)Panels::Instances] = buffer;
	sprintf_s(buffer, "Tool Settings##Document:%s", path.data());
	//sprintf_s(buffer, "Navmesh Creation##Document:%s", path.data());
	//myPanelWindowNames[(size_t)Panels::NavmeshCreationTool] = buffer;

	Camera& camera = myViewport.GetCamera();
	Vector2i resolution = myViewport.GetViewportSize();
	camera.SetPerspectiveProjection(
		60,
		{
			(float)resolution.x,
			(float)resolution.y
		},
		0.1f,
		50000.0f
	);

	Vector3f cameraRotation = { 45, 45, 0 };
	
	camera.GetTransform().SetRotation(cameraRotation);
	myViewport.SetCameraRotation(cameraRotation);
	camera.GetTransform().SetPosition((camera.GetTransform().GetForward() * -myViewport.GetCameraFocusDistance()));
}

void SceneDocument::Save()
{
	SaveScene(*myScene);
	mySaveUndoStackSize = myUndoStackSize;
}

void SceneDocument::Update(float aTimeDelta, InputManager& inputManager)
{
	aTimeDelta; inputManager;

	assert(GetActiveScene() == nullptr);
	SetActiveScene(myScene);
	assert(SceneSelection::GetActiveSceneSelection() == nullptr);
	SceneSelection::SetActiveSceneSelection(&mySceneSelection);

	SceneDrawParameters params =
	{
		.viewport = &myViewport,
		.scene = myScene,
		.sceneSelection = &mySceneSelection,
			};
	myGraphics->Draw(params);

	char buffer[512];
	char asterix[2] = {0, 0};

	// Todo: all of this base imgui stuff should move to the Document base class
	// Todo: it seems like ImGui figures out the name when calling ImGui::SetWindowFocus (in Editor when trying to create an already open document). Perhaps myImGuiName should actually be updated?
	if (mySaveUndoStackSize != myUndoStackSize)
		asterix[0] = '*';

	sprintf_s(buffer, "%s%s###Document:%s", myScene->GetName(), asterix, myScene->GetPath());

	// A document's very first frame can see a zero-size document dockspace --
	// GetContentRegionAvail() (which feeds GetDocumentDockSpaceSize()) reports
	// {0,0} before ImGui has laid out the host window on its first pass, same
	// as the editor viewport's own {0,0}-on-first-frame case elsewhere. Building
	// the one-time layout against that hits DockBuilderSetNodeSize's assert;
	// wait for a real size instead of asserting on the doc's opening frame.
	const ImVec2 outerDockSize = Editor::GetEditor()->GetDocumentDockSpaceSize();
	if (!myIsDockingInitialized && outerDockSize.x > 0.0f && outerDockSize.y > 0.0f)
	{
		ImGui::DockBuilderSetNodeSize(Editor::GetEditor()->GetDocumentDockSpaceId(), outerDockSize);
		ImGui::DockBuilderDockWindow(buffer, Editor::GetEditor()->GetDocumentDockSpaceId());

		ImGui::DockBuilderFinish(Editor::GetEditor()->GetDocumentDockSpaceId());
	}

	ImGui::SetNextWindowClass(Editor::GetEditor()->GetDocumentWindowClass());
	ImGui::SetNextWindowDockID(Editor::GetEditor()->GetDocumentDockSpaceId(), ImGuiCond_Once);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1);


	// The level is always open: no close button. Opening another level replaces it.
	ImGui::Begin(buffer);
	{
		// Todo: move this out so it can be reused between documents

		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(10, 0));

		ImGui::PushFont(ImGuiInterface::GetIconFontLarge());

		// not quite sure why exactly these numbers are needed, but fixes padding
		ImGui::SetCursorPosX(6);
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);

		// Add half of CellPadding to make positions of first icon more consistent
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 5);

		const ImVec2 toolbarItemSize = ImVec2(26, 28);
		auto toolbarSeparator = []()
		{
			ImGui::SameLine(0.f, 10.f);
			ImGui::TextDisabled("|");
			ImGui::SameLine(0.f, 10.f);
		};

		if (ImGui::Selectable(ICON_LC_SAVE_ALL, false, 0, toolbarItemSize))
		{
			Editor::GetEditor()->Save();
		}
		ImGui::SameLine();
		if (ImGui::Selectable(ICON_LC_PLAY, false, 0, toolbarItemSize) || ImGui::IsKeyPressed(ImGuiKey_F5))
		{
			ProjectRunControls::ExecuteRun(*this);
		}

		toolbarSeparator();
		{
			Gizmos& gizmos = myViewport.GetGizmos();
			const uint16_t operations[] = { ImGuizmo::TRANSLATE, ImGuizmo::ROTATE, ImGuizmo::SCALE };
			const char* icons[] = { ICON_LC_MOVE_3D, ICON_LC_ROTATE_3D, ICON_LC_SCALE_3D };
			for (int i = 0; i < 3; ++i)
			{
				if (i > 0) ImGui::SameLine();
				const bool active = gizmos.GetCurrentOperation() == operations[i];
				if (ImGui::Selectable(icons[i], active, 0, toolbarItemSize))
					gizmos.SetCurrentOperation(active ? uint16_t(0) : operations[i]);
			}

			ImGui::SameLine();
			if (ImGui::Selectable(ICON_LC_SETTINGS, false, 0, toolbarItemSize))
				ImGui::OpenPopup("TransformSettings");
			if (ImGui::BeginPopup("TransformSettings"))
			{
				ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
				gizmos.Draw();
				ImGui::PopFont();
				ImGui::EndPopup();
			}
		}

		toolbarSeparator();
		ImGui::PopFont();
		ImGui::PopStyleVar(2);

		ImGui::SameLine();
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.f);
		DrawAddMenu();
	}

	ImGui::PopStyleVar(2);

	ImVec2 docSpaceSize = ImGui::GetContentRegionAvail();

	ImGuiID dockSpaceId = ImGui::GetID("Document Dockspace");
	// todo: ImGui::GetContentRegionAvail() returns wrong result first time it seems. What to do instead?
	ImGui::DockSpace(dockSpaceId, docSpaceSize, ImGuiDockNodeFlags_AutoHideTabBar, &myDocumentWindowClass);

	if (!myIsDockingInitialized && docSpaceSize.x > 0.0f && docSpaceSize.y > 0.0f)
	{
		ImGuiID center = 0, right = 0, rightBottom = 0;

		ImGui::DockBuilderRemoveNode(dockSpaceId); // clear any previous layout
		ImGui::DockBuilderAddNode(dockSpaceId, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockSpaceId, docSpaceSize);

		center = dockSpaceId;

		ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f, &right, &center);
		ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.55f, &rightBottom, &right);

		ImGui::DockBuilderDockWindow(myPanelWindowNames[(size_t)Panels::Viewport].c_str(), center);
		ImGui::DockBuilderDockWindow(myPanelWindowNames[(size_t)Panels::Instances].c_str(), right);
		ImGui::DockBuilderDockWindow(myPanelWindowNames[(size_t)Panels::Properties].c_str(), rightBottom);

		ImGui::DockBuilderFinish(dockSpaceId);

		myIsDockingInitialized = true;
	}

	ImGui::End();

	const Ag::Color color = Ag::Application::GetInstance()->GetClearColor();

	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(color.myR, color.myG, color.myB, color.myA));
	ImGui::SetNextWindowClass(&myDocumentWindowClass);

	bool isViewportOrInstancesFocused = false;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::Begin(myPanelWindowNames[(size_t)Panels::Viewport].c_str());
	ImGui::PopStyleVar(1);

	isViewportOrInstancesFocused = isViewportOrInstancesFocused || ImGui::IsWindowFocused();
	myViewport.DrawAndUpdateViewportWindow(aTimeDelta, *this);

	ImGui::End();
	ImGui::PopStyleColor();

	ImGui::SetNextWindowClass(&myDocumentWindowClass);
	ImGui::Begin(myPanelWindowNames[(size_t)Panels::Properties].c_str());
	myProperties.Draw();
	ImGui::End();

	ImGui::SetNextWindowClass(&myDocumentWindowClass);
	ImGui::Begin(myPanelWindowNames[(size_t)Panels::Instances].c_str());
	mySceneObjectList.Draw();
	ImGui::End();

	//ImGui::SetNextWindowClass(&myDocumentWindowClass);
	//ImGui::Begin(myPanelWindowNames[(size_t)Panels::NavmeshCreationTool].c_str());
	//myNavmeshCreationTool.DrawUI();
	//ImGui::End();

	ImGuiIO& io = ImGui::GetIO();
	if (isViewportOrInstancesFocused)
	{
		if (ImGui::IsAnyItemActive() == false && ImGui::IsKeyPressed(ImGuiKey_Delete))
		{
			if (SceneSelection::GetActiveSceneSelection()->GetSelection().size() > 0)
			{
				std::shared_ptr<RemoveSceneObjectsCommand> command = std::make_shared<RemoveSceneObjectsCommand>();
				command->AddObjects(SceneSelection::GetActiveSceneSelection()->GetSelection());

				CommandManager::DoCommand(command);

				SceneSelection::GetActiveSceneSelection()->ClearSelection();
			}
		}

		if (ImGui::IsKeyPressed(ImGuiKey_F) && !SceneSelection::GetActiveSceneSelection()->GetSelection().empty())
		{
			Vector3f pos{};
			size_t selectionSize = SceneSelection::GetActiveSceneSelection()->GetSelection().size();
			for (uint32_t id : SceneSelection::GetActiveSceneSelection()->GetSelection())
			{
				SceneObject* obj = myScene->GetSceneObject(id);
				pos += obj->GetPosition();
			}

			Ag::Camera& activeCamera = myViewport.GetCamera();
			pos.x /= selectionSize;
			pos.y /= selectionSize;
			pos.z /= selectionSize;
			activeCamera.GetTransform().SetPosition(pos);
			activeCamera.GetTransform().Translate(-myViewport.GetCameraFocusDistance() * activeCamera.GetTransform().GetForward());

		}

		if (io.KeyCtrl)
		{
			if (ImGui::IsKeyReleased(ImGuiKey_D))
			{
				std::span<const uint32_t> selection = SceneSelection::GetActiveSceneSelection()->GetSelection();

				if (selection.size() > 0)
				{
					std::shared_ptr<AddSceneObjectsCommand> command = std::make_shared<AddSceneObjectsCommand>();

					constexpr int nameBufferSize = 512;
					char nameBuffer[nameBufferSize];

					for (uint32_t id : selection)
					{
						std::shared_ptr<SceneObject> object = std::make_shared<SceneObject>(*GetActiveScene()->GetSceneObject(id));

						const char* initialName = object->GetName();
						size_t initialLength = strlen(initialName);

						std::regex re("(.*)\\((\\d+)\\)$"); // Regex to match name and number in parentheses
						std::cmatch match;
						int number = 1;
						size_t baseLength = 0;

						// Use regex to parse the base name and number if parentheses with numbers are present
						if (std::regex_match(initialName, initialName + initialLength, match, re))
						{
							std::string base = match[1].str();
							baseLength = base.length();
							sprintf_s(nameBuffer, nameBufferSize, "%s", base.c_str());
							if (match[2].matched) 
							{
								number = std::stoi(match[2].str()) + 1;
							}
						}
						else 
						{
							sprintf_s(nameBuffer, nameBufferSize, "%s", initialName);
							baseLength = initialLength;
						}

						// Check if the base name already exists
						while (true)
						{
							bool exists = myScene->GetFirstSceneObject(nameBuffer) != nullptr;
							if (!exists)
							{
								for (auto pair : command->GetObjects())
								{
									if (pair.second->GetName() == std::string_view(nameBuffer))
									{
										exists = true;
										break;
									}
								}
							}

							if (!exists)
								break;

							sprintf_s(nameBuffer + baseLength, nameBufferSize - baseLength, "(%d)", number++);
						}

						object->SetName(nameBuffer);

						/*
						int i = 1;

						// todo: this is a O(n^2) algorithm
						while (true)
						{
							sprintf_s(buffer, "%s(%i)", object->GetName(), i);
							if (myScene->GetFirstSceneObject(buffer) == nullptr)
							{
								object->SetName(buffer);
								break;
							}
							i++;
						}
						*/

						command->AddObjects(std::span<std::shared_ptr<SceneObject>>(&object, 1));
					}

					CommandManager::DoCommand(command);

					SceneSelection::GetActiveSceneSelection()->ClearSelection();

					std::span<const std::pair<uint32_t, std::shared_ptr<SceneObject>>>  createdObjects = command->GetObjects();

					for (const std::pair<uint32_t, std::shared_ptr<SceneObject>>& p : createdObjects)
					{
						SceneSelection::GetActiveSceneSelection()->AddToSelection(p.first);
					}
				}
			}
		}
	}
	assert(GetActiveScene() == myScene);
	SetActiveScene(nullptr);
	assert(SceneSelection::GetActiveSceneSelection() == &mySceneSelection);
	SceneSelection::SetActiveSceneSelection(nullptr);
}

Scene* locPrevScene;

void SceneDocument::OnAction(CommandManager::Action action)
{
	static std::vector<uint32_t> objects;

	// keep track of which objects have been modified and if the scene has been modified
	auto updateModificationCounts = [&](const AbstractCommand* command, int change)
		{
			const SceneCommandBase* commandBase = dynamic_cast<const SceneCommandBase*>(command);

			if (commandBase == nullptr)
			{
				mySceneModificationsCount += change;
			}
			else
			{
				objects.clear();
				bool hasSceneChanged;
				commandBase->GetModifiedObjects(objects, hasSceneChanged);

				if (hasSceneChanged)
				{
					mySceneModificationsCount += change;
				}

				for (uint32_t object : objects)
				{
					int& count = myObjectModificationsCounts[object];
					count += change;
				}
			}
		};

	if (action == CommandManager::Action::Do)
	{
		// If doing something when the undo stack is lower than when we saved, it means we can't get back to the saved state
		if (myUndoStackSize < mySaveUndoStackSize)
			mySaveUndoStackSize = -1;

		myUndoStackSize++;

		updateModificationCounts(CommandManager::GetTopOfUndoStack(), 1);
	}
	if (action == CommandManager::Action::PostRedo)
	{
		myUndoStackSize++;

		updateModificationCounts(CommandManager::GetTopOfUndoStack(), 1);
	}
	if (action == CommandManager::Action::PreUndo)
	{
		updateModificationCounts(CommandManager::GetTopOfUndoStack(), -1);
	}

	if (action == CommandManager::Action::PostUndo)
	{
		myUndoStackSize--;
	}
	if (action == CommandManager::Action::Clear)
	{
		myUndoStackSize = 0;
	}

	if (action == CommandManager::Action::PreRedo || action == CommandManager::Action::PreUndo)
	{
		assert(locPrevScene == nullptr);

		locPrevScene = GetActiveScene();
		SetActiveScene(myScene);
	}

	mySceneSelection.OnAction(action);

	if (action == CommandManager::Action::PostRedo || action == CommandManager::Action::PostUndo)
	{
		assert(GetActiveScene() == myScene);

		SetActiveScene(locPrevScene);
		locPrevScene = nullptr;
	}

	mySceneObjectList.SetSceneDirty();
}

void SceneDocument::PlaceObject(const std::string& definitionPath, const std::string& displayName)
{
	auto object = std::make_shared<SceneObject>();
	StringId objectDefinitionName = StringRegistry::RegisterOrGetString(fs::path(definitionPath).stem().string());
	object->SetSceneObjectDefinitionName(objectDefinitionName);

	if (myScene->GetFirstSceneObject(objectDefinitionName.GetString()) == nullptr)
	{
		object->SetName(displayName.c_str());
	}
	else
	{
		char buffer[512];

		int i = 1;

		// todo: this is a O(n^2) algorithm
		while (true)
		{
			sprintf_s(buffer, "%s(%i)", displayName.c_str(), i);
			if (myScene->GetFirstSceneObject(buffer) == nullptr)
			{
				object->SetName(buffer);
				break;
			}
			i++;
		}
	}

	{
		Camera& cam = myViewport.GetCamera();
		Vector3f pos = cam.GetTransform().GetPosition() + cam.GetTransform().GetForward() * myViewport.GetCameraFocusDistance();

		if (myViewport.GetGizmos().GetSnappingInfo().snapPos)
		{
			pos = pos / myViewport.GetGizmos().GetSnappingInfo().pos;
			pos.x = round(pos.x);
			pos.y = round(pos.y);
			pos.z = round(pos.z);

			pos = myViewport.GetGizmos().GetSnappingInfo().pos * pos;
		}

		object->GetTRS().translation = pos;
	}

	std::shared_ptr<AddSceneObjectsCommand> command = std::make_shared<AddSceneObjectsCommand>();
	command->AddObjects(std::span<std::shared_ptr<SceneObject>>(&object, 1));
	CommandManager::DoCommand(command);

	SceneSelection::GetActiveSceneSelection()->ClearSelection();

	std::span<const std::pair<uint32_t, std::shared_ptr<SceneObject>>>  createdObjects = command->GetObjects();
	for (const std::pair<uint32_t, std::shared_ptr<SceneObject>>& p : createdObjects)
	{
		SceneSelection::GetActiveSceneSelection()->AddToSelection(p.first);
	}

	mySceneObjectList.SetSceneDirty();
}

void SceneDocument::DrawAddMenu()
{
	static char filter[64] = "";

	if (ImGui::Button(ICON_LC_PLUS " Add"))
	{
		filter[0] = '\0';
		ImGui::OpenPopup("PlaceObjectMenu");
	}

	if (ImGui::BeginPopup("PlaceObjectMenu"))
	{
		if (ImGui::IsWindowAppearing())
			ImGui::SetKeyboardFocusHere();
		ImGui::SetNextItemWidth(300.f);
		ImGui::InputTextWithHint("##placefilter", ICON_LC_SEARCH " Search", filter, sizeof(filter));

		std::string lowerFilter = filter;
		for (char& c : lowerFilter) c = (char)std::tolower((unsigned char)c);

		if (ImGui::BeginChild("##placelist", ImVec2(300.f, 260.f)))
		{
			int shown = 0;
			for (SceneObjectDefinition* definition : Editor::GetEditor()->GetSceneObjectDefinitionManager().GetAll())
			{
				std::string name = definition->GetName().GetString();
				std::string lowerName = name;
				for (char& c : lowerName) c = (char)std::tolower((unsigned char)c);
				if (!lowerFilter.empty() && lowerName.find(lowerFilter) == std::string::npos)
					continue;
				++shown;
				ImGui::PushID(shown);
				if (ImGui::Selectable(name.c_str()))
				{
					PlaceObject(definition->GetPath(), name);
					ImGui::CloseCurrentPopup();
				}
				ImGui::PopID();
			}
			if (shown == 0)
				ImGui::TextDisabled("No TGO matches");
		}
		ImGui::EndChild();
		ImGui::EndPopup();
	}
}

void SceneDocument::HandleDrop()
{
	if (ImGui::BeginDragDropTarget())
	{
		auto placePrefab = [this](const std::string& definitionPath, const std::string& displayName)
		{
			PlaceObject(definitionPath, displayName);
		};

		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(".tgo"))
		{
			const std::string definitionPath = static_cast<const char*>(payload->Data);
			placePrefab(definitionPath, fs::path(definitionPath).stem().string());
		}

		// Unity-style model placement: artists drag an FBX straight from Project
		// into the Scene.  The editor creates its minimal prefab definition beside
		// the model on first use, adds the Model property automatically, then
		// places an instance.  TGO remains the serialised prefab asset but no
		// longer has to be authored before the model can be used.
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(".fbx"))
		{
			try
			{
			const std::string modelPath = static_cast<const char*>(payload->Data);
			fs::path prefabPath = fs::path(modelPath).replace_extension(".tgo");

			auto& definitions = Editor::GetEditor()->GetSceneObjectDefinitionManager();
			SceneObjectDefinition* definition = definitions.CreateOrGet(prefabPath);
			if (definition && definition->GetProperties().empty())
			{
				ScenePropertyDefinition modelProperty{};
				modelProperty.name = StringRegistry::RegisterOrGetString("Model");
				modelProperty.groupName = StringRegistry::RegisterOrGetString("Rendering");
				modelProperty.description = StringRegistry::RegisterOrGetString("Model placed by dragging the FBX into the scene.");
				modelProperty.flags = ScenePropertyFlags::IsPerInstance;
				modelProperty.type = GetPropertyType<CopyOnWriteWrapper<SceneModel>>();
				auto model = CopyOnWriteWrapper<SceneModel>::Create();
				model.Edit().path = StringRegistry::RegisterOrGetString(modelPath);
				modelProperty.value = Property::Create<CopyOnWriteWrapper<SceneModel>>(model);
				definition->EditProperties().push_back(std::move(modelProperty));
				definition->Save();
			}
			// The definition above is only the bare Model property. If the prefab
			// has no material assets yet (a first drop, or an older bare prefab
			// like Bistro.tgo), run the real conversion so its .tgmat files and
			// material list get generated rather than left for the user to
			// author by hand. It runs in the background; the instance is placed
			// right away and the prefab is reloaded when the cook finishes.
			if (definition)
			{
				bool hasMaterials = false;
				for (const ScenePropertyDefinition& property : definition->GetProperties())
				{
					if (property.type != GetPropertyType<CopyOnWriteWrapper<SceneModel>>())
						continue;
					const SceneModel& sceneModel = property.value.Get<CopyOnWriteWrapper<SceneModel>>()->Get();
					for (const StringId& material : sceneModel.materials)
						if (!material.IsEmpty()) { hasMaterials = true; break; }
				}
				ContentBrowser& contentBrowser = Editor::GetEditor()->GetContentBrowser();
				if (!hasMaterials && !contentBrowser.IsConverting())
					contentBrowser.ConvertFbxToTgo(fs::absolute(fs::path(Settings::GameAssetRoot()) / modelPath));
			}
			placePrefab(prefabPath.string(), fs::path(modelPath).stem().string());
			}
			catch (const std::exception& e)
			{
				ERROR_PRINT("FBX placement failed for '%s': %s", static_cast<const char*>(payload->Data), e.what());
			}
			catch (...)
			{
				ERROR_PRINT("FBX placement failed: unknown error");
			}
		}

		ImGui::EndDragDropTarget();
	}
}
void SceneDocument::BeginDragSelection(Vector2f mousePos)
{
	Vector2i vpos = myViewport.GetViewportPos();
	Vector2i vsize = myViewport.GetViewportSize();

	RectSelection::GetCurrentRectSelection()->Update(
		{ mousePos.x - vpos.x, mousePos.y - vpos.y },
		{ (float)vpos.x, (float)vpos.y },
		{ (float)vsize.x, (float)vsize.y },
		myViewport.GetCamera()
	);

	SceneObjectDefinitionManager& manager = Editor::GetEditor()->GetSceneObjectDefinitionManager();
	std::vector<ScenePropertyDefinition> sceneObjectProperties;

	if (ImGui::GetIO().KeyShift == false)
	{
		RectSelection::GetCurrentRectSelection()->ClearSelection();
	}

	Matrix4x4f worldToCamera = Matrix4x4f::GetFastInverse(myViewport.GetCamera().GetTransform());
	for (auto& p : myScene->GetSceneObjects())
	{
		sceneObjectProperties.clear();
		p.second->CalculateCombinedPropertySet(manager, sceneObjectProperties);

		bool hasModel = false;

		for (ScenePropertyDefinition& property : sceneObjectProperties)
		{
			if (property.type == GetPropertyType<CopyOnWriteWrapper<SceneModel>>())
			{
				const SceneModel& value = property.value.Get<CopyOnWriteWrapper<SceneModel>>()->Get();

				StringId path = value.path;
				FilePathStream dummyPath;
				if (path.IsEmpty() || !Settings::ResolveAssetPath(path, dummyPath))
					continue;

				SceneModelMeshInfo meshInfo;
				if (GetModelMeshInfo(path, meshInfo))
				{
					hasModel = true;

					const auto& bounds = meshInfo.bounds;
					Ag::Vector3f viewCenter = bounds.center * p.second->GetTransform() * worldToCamera;

					// todo: Bounds here aren't correct since they aren't rotated.
					// Have to send in the transform as well into CheckFrustum for this to work
					// Easiest way to make this work then is to transform the frustrum planes with the inverse of the transform, 
					// so that the bounds still form a box/sphere
					const Ag::BoxSphereBounds viewBounds = {
						.radius = bounds.radius,
						.boxExtents = bounds.boxExtents,
						.center = viewCenter
					};

					if (RectSelection::GetCurrentRectSelection()->CheckFrustum(viewBounds))
					{
						RectSelection::GetCurrentRectSelection()->AddToSelection(p.first);
					}
				}
			}
		}

		// If the object lacks a model, select based on its position only
		if (!hasModel)
		{
			Ag::Vector3f viewCenter = Vector3f(0.f) * p.second->GetTransform() * worldToCamera;
			const Ag::BoxSphereBounds viewBounds = {
				.radius = 0,
				.boxExtents = 0,
				.center = viewCenter
			};

			if (RectSelection::GetCurrentRectSelection()->CheckFrustum(viewBounds))
			{
				RectSelection::GetCurrentRectSelection()->AddToSelection(p.first);
			}
		}
	}
}
void SceneDocument::DrawCollisionOverlay(CollisionOverlay& overlay)
{
	SceneObjectDefinitionManager& manager = Editor::GetEditor()->GetSceneObjectDefinitionManager();
	std::vector<ScenePropertyDefinition> properties;
	for (auto& p : myScene->GetSceneObjects())
	{
		properties.clear();
		p.second->CalculateCombinedPropertySet(manager, properties);
		overlay.DrawObject(properties, p.second->GetTransform());
	}
}

void SceneDocument::EndDragSelection(Vector2f mousePos, bool isShiftDown)
{
	Vector2i vpos = myViewport.GetViewportPos();
	Vector2i vsize = myViewport.GetViewportSize();

	RectSelection::GetCurrentRectSelection()->Update(
		{ mousePos.x, mousePos.y },
		{ (float)vpos.x, (float)vpos.y },
		{ (float)vsize.x, (float)vsize.y },
		myViewport.GetCamera()
	);

	SceneObjectDefinitionManager& manager = Editor::GetEditor()->GetSceneObjectDefinitionManager();
	std::vector<ScenePropertyDefinition> sceneObjectProperties;

	Matrix4x4f worldToCamera = Matrix4x4f::GetFastInverse(myViewport.GetCamera().GetTransform());
	if (RectSelection::GetCurrentRectSelection()->IsActive() && isShiftDown == false)
	{
		SceneSelection::GetActiveSceneSelection()->ClearSelection();
	}

	for (auto& p : myScene->GetSceneObjects())
	{
		sceneObjectProperties.clear();
		p.second->CalculateCombinedPropertySet(manager, sceneObjectProperties);

		if (RectSelection::GetCurrentRectSelection()->Contains(p.first))
		{
			SceneSelection::GetActiveSceneSelection()->AddToSelection(p.first);
		}

	}
	RectSelection::GetCurrentRectSelection()->ClearSelection();
}

void SceneDocument::ClickSelection(Vector2f mousePos, uint32_t selectedId, bool isShiftDown)
{
	mousePos;

	if (isShiftDown == false)
	{
		SceneSelection::GetActiveSceneSelection()->ClearSelection();
	}

	if (selectedId > 0)
	{
		for (auto& p : myScene->GetSceneObjects())
		{
			if (selectedId == p.first) 
			{
				if (SceneSelection::GetActiveSceneSelection()->Contains(p.first))
				{
					SceneSelection::GetActiveSceneSelection()->RemoveFromSelection(p.first);
				}
				else
				{
					SceneSelection::GetActiveSceneSelection()->AddToSelection(p.first);
				}
			}
		}
	}
}

void SceneDocument::BeginTransformation() 
{
	myTransformationInitialTransforms.clear();

	const std::span<const uint32_t>& selection = SceneSelection::GetActiveSceneSelection()->GetSelection();

	for (const uint32_t& objectid : selection)
	{
		SceneObject& object = *myScene->GetSceneObject(objectid);

		myTransformationInitialTransforms.push_back(object.GetTransform());
	}
	myPendingTransformCommand.Begin(selection);
}

void SceneDocument::UpdateTransformation(const Vector3f& referencePosition, const Matrix4x4f& transform)
{
	const std::span<const uint32_t>& selection = SceneSelection::GetActiveSceneSelection()->GetSelection();

	for (int i = 0; i < selection.size(); i++)
	{
		uint32_t objectid = selection[i];

		SceneObject& object = *myScene->GetSceneObject(objectid);

		Matrix4x4f oldTransform = myTransformationInitialTransforms[i];
		oldTransform.SetPosition(oldTransform.GetPosition() - referencePosition);
		Matrix4x4f t = oldTransform * transform;
		t.SetPosition(t.GetPosition() + referencePosition);

		object.SetTransform(t);
	}
}

void SceneDocument::EndTransformation() 
{
	myTransformationInitialTransforms.clear();

	myPendingTransformCommand.End();
	myPendingTransformCommand = {};
}

Vector3f SceneDocument::CalculateSelectionPosition()
{
	const std::span<const uint32_t>& selection = SceneSelection::GetActiveSceneSelection()->GetSelection();
	// selection.back() on an empty span is undefined behaviour. Callers are
	// expected to check HasTransformableSelection() first (ViewportInterface's
	// own contract), but a defensive check here is cheap insurance against
	// whichever call site doesn't.
	if (selection.empty()) return {};

	return GetActiveScene()->GetSceneObject(selection.back())->GetPosition();
}

Matrix4x4f SceneDocument::CalculateSelectionOrientation()
{
	const std::span<const uint32_t>& selection = SceneSelection::GetActiveSceneSelection()->GetSelection();
	if (selection.empty()) return Matrix4x4f::CreateIdentityMatrix();

	return GetActiveScene()->GetSceneObject(selection.back())->GetTransform();
}

bool SceneDocument::HasTransformableSelection()
{
	return !SceneSelection::GetActiveSceneSelection()->GetSelection().empty();
}
