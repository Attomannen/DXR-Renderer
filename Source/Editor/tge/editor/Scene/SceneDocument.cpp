#include "stdafx.h"

#include <commdlg.h>
#include <cstdlib>
#include <regex>

#include <tge/editor/Scene/SceneDocument.h>

#include <imguizmo/ImGuizmo.h>
#include <tge/input/InputManager.h>

#include <tge/editor/CommandManager/CommandManager.h>

#include <tge/graphics/DX11.h>
#include <tge/imgui/ImGuiInterface.h>
#include <tge/scene/SceneSerialize.h>
#include <tge/scene/ScenePropertyTypes.h>

#include <tge/math/BoxSphereBounds.h>

#include <tge/editor/Scene/SceneSelection.h>
#include <tge/editor/Scene/ActiveScene.h>

#include <tge/editor/Commands/AddSceneObjectsCommand.h>
#include <tge/editor/Commands/RemoveSceneObjectsCommand.h>

#include <tge/editor/Editor.h>

#include <tge/editor/Tools/Viewport/Viewport.h>
#include <tge/editor/Tools/ProjectRunControls/ProjectRunControls.h>
#include <tge/editor/FileDialog/FileDialog.h>
#include <tge/editor/imgui_widgets/imgui_widgets.h>
#include "imgui_internal.h" // for DockBuilder Api

#include <IconFontHeaders/IconsLucide.h>

#include <tge/editor/p4/p4.h>

#include "tge/Application.h"


using namespace Tga;

void Tga::SceneP4Handler(SceneFileChangeType aChangeType, const char* aPath)
{
	switch (aChangeType)
	{
	case SceneFileChangeType::Add:
		P4::MarkFileForAdd(aPath);
		break;
	case SceneFileChangeType::Modify:
		P4::CheckoutFile(aPath);
		break;
	case SceneFileChangeType::Delete:
		P4::MarkFileForDelete(aPath);
		break;
	}
}




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
	myGraphics = Editor::GetEditor()->GetEditorGraphics().CreateSceneGraphicsInterface();

	char buffer[512];
	char asterix[2] = {0, 0};

	sprintf_s(buffer, "%s%s###Document:%s", myScene->GetName(), asterix, myScene->GetPath());
	myImGuiName = StringRegistry::RegisterOrGetString(buffer);

	sprintf_s(buffer, "Viewport##Document:%s", path.data());
	myPanelWindowNames[(size_t)Panels::Viewport] = buffer;
	sprintf_s(buffer, "Properties##Document:%s", path.data());
	myPanelWindowNames[(size_t)Panels::Properties] = buffer;
	sprintf_s(buffer, "Instances##Document:%s", path.data());
	myPanelWindowNames[(size_t)Panels::Instances] = buffer;
	sprintf_s(buffer, "Tool Settings##Document:%s", path.data());
	myPanelWindowNames[(size_t)Panels::ToolSettings] = buffer;
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
	SaveScene(*myScene, SceneP4Handler);
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

	if (!myIsDockingInitialized)
	{
		ImGui::DockBuilderSetNodeSize(Editor::GetEditor()->GetDocumentDockSpaceId(), Editor::GetEditor()->GetDocumentDockSpaceSize());
		ImGui::DockBuilderDockWindow(buffer, Editor::GetEditor()->GetDocumentDockSpaceId());

		ImGui::DockBuilderFinish(Editor::GetEditor()->GetDocumentDockSpaceId());
	}

	ImGui::SetNextWindowClass(Editor::GetEditor()->GetDocumentWindowClass());
	ImGui::SetNextWindowDockID(Editor::GetEditor()->GetDocumentDockSpaceId(), ImGuiCond_Once);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1);


	bool open = true;
	ImGui::Begin(buffer, &open);
	if (myState == Document::State::Open && !open)
	{
		myState = Document::State::CloseRequested;
	}
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

		if (ImGui::BeginTable("Toolbar", 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit))
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);

			ImVec2 toolbarItemSize = ImVec2(26, 28);

			if (ImGui::Selectable(ICON_LC_SAVE_ALL, false, 0, toolbarItemSize))
			{
				Editor::GetEditor()->Save();
			}

			ImGui::TableSetColumnIndex(1);

			if (ImGui::Selectable(ICON_LC_PLAY, false, 0, toolbarItemSize) || ImGui::IsKeyPressed(ImGuiKey_F5))
			{
				ProjectRunControls::ExecuteRun(*this);
			}

			ImGui::TableSetColumnIndex(2);

			{
				Gizmos& gizmos = myViewport.GetGizmos();
				bool isMoveToolActive = gizmos.GetCurrentOperation() == ImGuizmo::TRANSLATE;
				if (ImGui::Selectable(ICON_LC_MOVE_3D, isMoveToolActive, 0, toolbarItemSize))
				{
					gizmos.SetCurrentOperation(isMoveToolActive ? 0 : ImGuizmo::TRANSLATE);
				}
				ImGui::SameLine();

				bool isRotateToolActive = gizmos.GetCurrentOperation() == ImGuizmo::ROTATE;
				if (ImGui::Selectable(ICON_LC_ROTATE_3D, isRotateToolActive, 0, toolbarItemSize))
					gizmos.SetCurrentOperation(isRotateToolActive ? 0 : ImGuizmo::ROTATE);

				ImGui::SameLine();

				bool isScaleToolActive = gizmos.GetCurrentOperation() == ImGuizmo::SCALE;
				if (ImGui::Selectable(ICON_LC_SCALE_3D, isScaleToolActive, 0, toolbarItemSize))
					gizmos.SetCurrentOperation(isScaleToolActive ? 0 : ImGuizmo::SCALE);
			}

			ImGui::EndTable();
		}

		ImGui::PopFont();
		ImGui::PopStyleVar(2);
	}

	ImGui::PopStyleVar(2);

	ImVec2 docSpaceSize = ImGui::GetContentRegionAvail();

	ImGuiID dockSpaceId = ImGui::GetID("Document Dockspace");
	// todo: ImGui::GetContentRegionAvail() returns wrong result first time it seems. What to do instead?
	ImGui::DockSpace(dockSpaceId, docSpaceSize, ImGuiDockNodeFlags_None, &myDocumentWindowClass);

	if (!myIsDockingInitialized)
	{
		ImGuiID center = 0, left = 0, rightBottom = 0, rightTop = 0;

		ImGui::DockBuilderRemoveNode(dockSpaceId); // clear any previous layout
		ImGui::DockBuilderAddNode(dockSpaceId, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockSpaceId, docSpaceSize);

		center = dockSpaceId;

		ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.2f, &left, &center);
		ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f, &rightBottom, &center);
		ImGui::DockBuilderSplitNode(rightBottom, ImGuiDir_Up, 0.2f, &rightTop, &rightBottom);

		ImGui::DockBuilderDockWindow(myPanelWindowNames[(size_t)Panels::Viewport].c_str(), center);
		ImGui::DockBuilderDockWindow(myPanelWindowNames[(size_t)Panels::ToolSettings].c_str(), rightTop);
		ImGui::DockBuilderDockWindow(myPanelWindowNames[(size_t)Panels::Properties].c_str(), rightBottom);
		ImGui::DockBuilderDockWindow(myPanelWindowNames[(size_t)Panels::Instances].c_str(), left);
		//ImGui::DockBuilderDockWindow(myPanelWindowNames[(size_t)Panels::NavmeshCreationTool].c_str(), left);

		ImGui::DockBuilderFinish(dockSpaceId);

		myIsDockingInitialized = true;
	}

	ImGui::End();

	const Tga::Color color = Tga::Application::GetInstance()->GetClearColor();

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

	ImGui::Begin(myPanelWindowNames[(size_t)Panels::ToolSettings].c_str());
	Gizmos& gizmos = myViewport.GetGizmos();
	gizmos.Draw();
	ImGui::End();

	ImGui::SetNextWindowClass(&myDocumentWindowClass);
	ImGui::Begin(myPanelWindowNames[(size_t)Panels::Properties].c_str());
	myProperties.Draw();
	ImGui::End();

	ImGui::SetNextWindowClass(&myDocumentWindowClass);
	ImGui::Begin(myPanelWindowNames[(size_t)Panels::Instances].c_str());
	isViewportOrInstancesFocused = isViewportOrInstancesFocused || ImGui::IsWindowFocused();
	mySceneObjectList.Draw();
	ImGui::End();

#ifdef NOT_USED_FOR_HANDELING_P4_LOGIN
	if (authRequest) {
		char password[128]{};
		bool shouldSubmit = false;
		static bool focus_set = false;

		ImGui::OpenPopup("##p4password");

		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.5f, 0.0f, 0.5f, 1.0f)); // Purple color
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f); // Thicker border
		if (ImGui::BeginPopupModal("##p4password", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
		{
			ImGui::Text("Enter perforce password for% s", P4::MyUser(), NULL, ImGuiWindowFlags_AlwaysAutoResize);
			if (!focus_set)
			{
				ImGui::SetKeyboardFocusHere();
				focus_set = false;
			}
			ImGui::InputText("##password_input", password, sizeof(password), ImGuiInputTextFlags_Password | ImGuiInputTextFlags_AutoSelectAll);

			// Add spacing for better layout
			ImGui::Spacing();

			if (ImGui::IsKeyPressed(ImGuiKey_Enter))
			{
				shouldSubmit = true;
			}
			if (ImGui::Button("Submit"))
			{
				shouldSubmit = true;
			}

			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
			{
				focus_set = false;
				ImGui::CloseCurrentPopup();
			}

			if (shouldSubmit)
			{
				focus_set = false;
				ImGui::CloseCurrentPopup();

				/*
				todo: this keeps blocking when login fails
				authRequest = false;
				if (P4::Login(password))
				{
					std::filesystem::path p = myScene->GetName();
					P4::StartPollingLevelInfo(p.replace_extension(".leveldata").string().c_str(), 2, []() {
						authRequest = true;
					});
				}
				else
				{
					// show error!
				}*/
			}
			ImGui::EndPopup();
		}
		ImGui::PopStyleColor();
		ImGui::PopStyleVar();
	}
#endif
	//ImGui::SetNextWindowClass(&myDocumentWindowClass);
	//ImGui::Begin(myPanelWindowNames[(size_t)Panels::NavmeshCreationTool].c_str());
	//myNavmeshCreationTool.DrawUI();
	//ImGui::End();

	ImGuiIO& io = ImGui::GetIO();
	if (isViewportOrInstancesFocused)
	{
		if (ImGui::IsAnyItemActive() == false && ImGui::IsKeyDown(ImGuiKey_Delete))
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

			Tga::Camera& activeCamera = myViewport.GetCamera();
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
	// first time an object is modified, check it out in p4
	auto updateCountsAndP4 = [&](const AbstractCommand* command, int change)
		{
			const SceneCommandBase* commandBase = dynamic_cast<const SceneCommandBase*>(command);

			if (commandBase == nullptr)
			{
				if (mySceneModificationsCount == 0)
				{
					P4::CheckoutFile(myPath);
				}

				mySceneModificationsCount += change;
			}
			else
			{
				objects.clear();
				bool hasSceneChanged;
				commandBase->GetModifiedObjects(objects, hasSceneChanged);

				if (hasSceneChanged)
				{
					if (mySceneModificationsCount == 0)
					{
						P4::CheckoutFile(myPath);
					}

					mySceneModificationsCount += change;
				}

				for (uint32_t object : objects)
				{
					int& count = myObjectModificationsCounts[object];

					if (count == 0)
					{
						P4::CheckoutFile(myScene->GetObjectFilePath(object).GetString());
					}

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

		updateCountsAndP4(CommandManager::GetTopOfUndoStack(), 1);
	}
	if (action == CommandManager::Action::PostRedo)
	{
		myUndoStackSize++;

		updateCountsAndP4(CommandManager::GetTopOfUndoStack(), 1);
	}
	if (action == CommandManager::Action::PreUndo)
	{
		updateCountsAndP4(CommandManager::GetTopOfUndoStack(), -1);
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

void SceneDocument::HandleDrop() 
{
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(".tgo"))
		{
			std::string data = (const char*)payload->Data;

			auto object = std::make_shared<SceneObject>();
			StringId objectDefinitionName = StringRegistry::RegisterOrGetString(fs::path(data).stem().string());
			object->SetSceneObjectDefinitionName(objectDefinitionName);

			if (myScene->GetFirstSceneObject(objectDefinitionName.GetString()) == nullptr)
			{
				object->SetName(objectDefinitionName.GetString());
			}
			else
			{
				char buffer[512];

				int i = 1;

				// todo: this is a O(n^2) algorithm
				while (true)
				{
					sprintf_s(buffer, "%s(%i)", objectDefinitionName.GetString(), i);
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
					Tga::Vector3f viewCenter = bounds.center * p.second->GetTransform() * worldToCamera;

					// todo: Bounds here aren't correct since they aren't rotated.
					// Have to send in the transform as well into CheckFrustum for this to work
					// Easiest way to make this work then is to transform the frustrum planes with the inverse of the transform, 
					// so that the bounds still form a box/sphere
					const Tga::BoxSphereBounds viewBounds = {
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
			Tga::Vector3f viewCenter = Vector3f(0.f) * p.second->GetTransform() * worldToCamera;
			const Tga::BoxSphereBounds viewBounds = {
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
		P4::CheckoutFile(myScene->GetObjectFilePath(objectid).GetString());

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

	return GetActiveScene()->GetSceneObject(selection.back())->GetPosition();
}

Matrix4x4f SceneDocument::CalculateSelectionOrientation()
{
	const std::span<const uint32_t>& selection = SceneSelection::GetActiveSceneSelection()->GetSelection();

	return GetActiveScene()->GetSceneObject(selection.back())->GetTransform();
}

bool SceneDocument::HasTransformableSelection()
{
	return !SceneSelection::GetActiveSceneSelection()->GetSelection().empty();
}
