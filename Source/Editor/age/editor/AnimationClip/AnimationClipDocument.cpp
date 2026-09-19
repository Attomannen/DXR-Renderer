#include <age/editor/AnimationClip/AnimationClipDocument.h>

#include <age/editor/imgui_widgets/imgui_widgets.h>
#include "imgui_internal.h" // for DockBuilder Api

#include <age/Application.h>
#include <age/imgui/ImGuiPropertyEditor.h>
#include <age/settings/settings.h>
#include <age/editor/AnimationClip/Commands/ChangeAnimationClipCommand.h>

#include <IconFontHeaders/IconsLucide.h>

#include <age/editor/Editor.h>

#include <age/Animation/Animation.h>
#include <age/Animation/Skeleton.h>
#include <age/graphics/DX11.h>

using namespace Ag;

void AnimationClipDocument::Init(std::string_view aPath)
{
	Document::Init(aPath);

	myGraphics = Editor::GetEditor()->GetEditorGraphics().CreateAnimationClipGraphicsInterface();

	myViewport.Init();
	myViewport.GetGrid().SetGridLineExtreme(400.0f);

	myPath = StringRegistry::RegisterOrGetString(aPath);
	myAnimationClip = GetOrCreateAnimationClip(myPath);

	std::filesystem::path path = aPath;
	myName = StringRegistry::RegisterOrGetString(path.stem().replace_extension("").string());

	char buffer[512];
	char asterix[2] = { 0, 0 };

	sprintf_s(buffer, "%s%s###Document:%s", myName.GetString(), asterix, myPath.GetString());
	myImGuiName = StringRegistry::RegisterOrGetString(buffer);

	sprintf_s(buffer, "Properties##Document:%s", myPath.GetString());
	myPanelWindowNames[(size_t)Panels::Properties] = buffer;
	sprintf_s(buffer, "PlayControls##Document:%s", myPath.GetString());
	myPanelWindowNames[(size_t)Panels::PlayControls] = buffer;
	sprintf_s(buffer, "Skeleton##Document:%s", myPath.GetString());
	myPanelWindowNames[(size_t)Panels::Skeleton] = buffer;
	sprintf_s(buffer, "Viewport##Document:%s", myPath.GetString());
	myPanelWindowNames[(size_t)Panels::Viewport] = buffer;

	Camera& camera = myViewport.GetCamera();
	Vector2i resolution = myViewport.GetViewportSize();
	camera.SetPerspectiveProjection(
		60,
		{
			(float)resolution.x,
			(float)resolution.y
		},
		0.01f,
		5000.0f
	);

	Vector3f cameraRotation = { 45, 45, 0 };

	camera.GetTransform().SetRotation(cameraRotation);
	myViewport.SetCameraRotation(cameraRotation);
	camera.GetTransform().SetPosition((camera.GetTransform().GetForward() * -myViewport.GetCameraFocusDistance()));
}

void AnimationClipDocument::Save()
{
	SaveAnimationClip(myPath);

	mySaveUndoStackSize = myUndoStackSize;
}

void AnimationClipDocument::Update(float aTimeDelta, InputManager& inputManager)
{
	inputManager;

	Ag::Application& application = *Ag::Application::GetInstance();

	const Camera& renderCamera = myViewport.GetCamera();
	Frustum frustum = CalculateFrustum(renderCamera);

	{
		if (myAnimationClip->endTime <= myAnimationClip->startTime)
		{
			// start and end time are set up wrong, can't play properly
			myCurrentTime = myAnimationClip->startTime;
		}
		else
		{
			// first adjust time to correct range, to handle cases start and end time are adjusted while playing
			if (myCurrentTime < myAnimationClip->startTime)
			{
				myCurrentTime = myAnimationClip->startTime;
			}

			if (myCurrentTime > myAnimationClip->endTime)
			{
				myCurrentTime = myAnimationClip->endTime;
			}

			if (myPlayState == PlayState::Playing)
			{
				myCurrentTime += myAnimationClip->playbackRate * aTimeDelta;

				if (myAnimationClip->isLooping)
				{
					if (myAnimationClip->playbackRate < 0.f)
					{
						while (myCurrentTime < myAnimationClip->startTime)
						{
							myCurrentTime += myAnimationClip->endTime - myAnimationClip->startTime;
						}
					}
					else
					{
						while (myCurrentTime > myAnimationClip->endTime)
						{
							myCurrentTime -= myAnimationClip->endTime - myAnimationClip->startTime;
						}
					}
				}
				else
				{
					if (myCurrentTime < myAnimationClip->startTime)
					{
						myCurrentTime = myAnimationClip->startTime;
						myPlayState = PlayState::Stopped;
					}

					if (myCurrentTime > myAnimationClip->endTime)
					{
						myCurrentTime = myAnimationClip->endTime;
						myPlayState = PlayState::Stopped;
					}
				}
			}
		}

		AnimationClipDrawParameters parameters =
		{
			.viewport = &myViewport,
			.clip = myAnimationClip,
			.currentTime = myCurrentTime,
			.selectedSkeletonNodeIndex = mySelectedSkeletonNodeIndex
								};
		myGraphics->Draw(parameters);
		}

	char buffer[512];
	char asterix[2] = { 0, 0 };

	// Todo: all of this base imgui stuff should move to the Document base class
	if (mySaveUndoStackSize != myUndoStackSize)
		asterix[0] = '*';

	sprintf_s(buffer, "%s%s###Document:%s", myName.GetString(), asterix, myPath.GetString());

	// See SceneDocument.cpp's identical guard: GetDocumentDockSpaceSize() can
	// still be {0,0} on this document's opening frame, which asserts inside
	// DockBuilderSetNodeSize -- wait for a real size instead.
	const ImVec2 outerDockSize = Editor::GetEditor()->GetDocumentDockSpaceSize();
	if (!myIsDockingInitialized && outerDockSize.x > 0.0f && outerDockSize.y > 0.0f)
	{
		ImGui::DockBuilderSetNodeSize(Editor::GetEditor()->GetDocumentDockSpaceId(), outerDockSize);
		ImGui::DockBuilderDockWindow(buffer, Editor::GetEditor()->GetDocumentDockSpaceId());

		ImGui::DockBuilderFinish(Editor::GetEditor()->GetDocumentDockSpaceId());
	}

	ImGui::SetNextWindowClass(Editor::GetEditor()->GetDocumentWindowClass());
	ImGui::SetNextWindowDockID(Editor::GetEditor()->GetDocumentDockSpaceId(), ImGuiCond_Once);

	{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1);

		bool open = true;
		ImGui::Begin(buffer, &open);
		if (myState == Document::State::Open && !open)
		{
			myState = Document::State::CloseRequested;
		}
		ImGui::PopStyleVar(2);

		ImVec2 docSpaceSize = ImGui::GetContentRegionAvail();
		ImGuiID dockSpaceId = ImGui::GetID("Document Dockspace");
		// todo: ImGui::GetContentRegionAvail() returns wrong result first time it seems. What to do instead?
		ImGui::DockSpace(dockSpaceId, docSpaceSize, ImGuiDockNodeFlags_AutoHideTabBar, &myDocumentWindowClass);

		if (!myIsDockingInitialized && docSpaceSize.x > 0.0f && docSpaceSize.y > 0.0f)
		{
			ImGuiID center = 0;
			ImGuiID right = 0;
			ImGuiID left = 0;

			ImGuiID centerUpper = 0;
			ImGuiID centerLower = 0;

			ImGui::DockBuilderRemoveNode(dockSpaceId); // clear any previous layout
			ImGui::DockBuilderAddNode(dockSpaceId, ImGuiDockNodeFlags_DockSpace);
			ImGui::DockBuilderSetNodeSize(dockSpaceId, docSpaceSize);

			center = dockSpaceId;

			// Skeleton on the left, details on the right, a one-row timeline under the viewport.
			ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.22f, &right, &center);
			ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.22f, &left, &center);

			ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.07f, &centerLower, &centerUpper);

			ImGui::DockBuilderDockWindow(myPanelWindowNames[(size_t)Panels::Properties].c_str(), right);
			ImGui::DockBuilderDockWindow(myPanelWindowNames[(size_t)Panels::Skeleton].c_str(), left);
			ImGui::DockBuilderDockWindow(myPanelWindowNames[(size_t)Panels::PlayControls].c_str(), centerLower);

			ImGui::DockBuilderDockWindow(myPanelWindowNames[(size_t)Panels::Viewport].c_str(), centerUpper);

			// Single-panel nodes do not need a tab header.
			for (const ImGuiID node : { centerUpper, centerLower })
				if (ImGuiDockNode* dockNode = ImGui::DockBuilderGetNode(node))
					dockNode->SetLocalFlags(dockNode->LocalFlags | ImGuiDockNodeFlags_AutoHideTabBar);

			ImGui::DockBuilderFinish(dockSpaceId);

			myIsDockingInitialized = true;
		}

		ImGui::End();
	}

	const Ag::Color color = application.GetClearColor();

	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(color.myR, color.myG, color.myB, color.myA));
	ImGui::SetNextWindowClass(&myDocumentWindowClass);

	bool isViewportOrPropertiesFocused = false;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::Begin(myPanelWindowNames[(size_t)Panels::Viewport].c_str());
	ImGui::PopStyleVar(1);

	isViewportOrPropertiesFocused = isViewportOrPropertiesFocused || ImGui::IsWindowFocused();
	myViewport.DrawAndUpdateViewportWindow(aTimeDelta, *this);

	ImGui::End();
	ImGui::PopStyleColor();

	ImGui::SetNextWindowClass(&myDocumentWindowClass);

	isViewportOrPropertiesFocused = isViewportOrPropertiesFocused || ImGui::IsWindowFocused();
	ImGui::Begin(myPanelWindowNames[(size_t)Panels::Properties].c_str());

	DrawPropertyPanel();

	ImGui::End();

	ImGui::SetNextWindowClass(&myDocumentWindowClass);
	isViewportOrPropertiesFocused = isViewportOrPropertiesFocused || ImGui::IsWindowFocused();
	ImGui::Begin(myPanelWindowNames[(size_t)Panels::Skeleton].c_str());

	DrawSkeletonPanel();

	ImGui::End();

	ImGui::SetNextWindowClass(&myDocumentWindowClass);

	isViewportOrPropertiesFocused = isViewportOrPropertiesFocused || ImGui::IsWindowFocused();
	ImGui::Begin(myPanelWindowNames[(size_t)Panels::PlayControls].c_str());

	DrawPlayControls();

	ImGui::End();
}

void AnimationClipDocument::OnAction(CommandManager::Action action)
{
	if (action == CommandManager::Action::Do)
	{
		// If doing something when the undo stack is lower than when we saved, it means we can't get back to the saved state
		if (myUndoStackSize < mySaveUndoStackSize)
			mySaveUndoStackSize = -1;

		myUndoStackSize++;
	}
	if (action == CommandManager::Action::PostRedo)
	{
		myUndoStackSize++;
	}
	if (action == CommandManager::Action::PostUndo)
	{
		myUndoStackSize--;
	}
	if (action == CommandManager::Action::Clear)
	{
		myUndoStackSize = 0;
	}
}

void AnimationClipDocument::DrawSkeletonPanel()
{
	FilePathStream dummyPath;
	if (myAnimationClip->previewModelPath.IsEmpty() || !Settings::ResolveAssetPath(myAnimationClip->previewModelPath, dummyPath))
		return;
	
	const std::shared_ptr<const Skeleton> skeleton = GetSkeleton(myAnimationClip->previewModelPath.GetString());
	
	if (!skeleton)
		return;
	
	const size_t jointCount = skeleton->joints.size();
	if (jointCount == 0)
		return;

	struct StackEntry
	{
		unsigned joint;
		int childCursor;
		bool isOpen;
	};

	StackEntry stack[MAX_ANIMATION_BONES];
	int sp = 0;

	stack[sp++] = { 0, -1, false };

	while (sp > 0)
	{
		StackEntry& e = stack[sp - 1];
		const auto& joint = skeleton->joints[e.joint];

		if (e.childCursor == -1)
		{
			ImGuiTreeNodeFlags flags =
				ImGuiTreeNodeFlags_OpenOnArrow |
				ImGuiTreeNodeFlags_OpenOnDoubleClick |
				ImGuiTreeNodeFlags_SpanAvailWidth;

			if (sp < 5)
				flags |= ImGuiTreeNodeFlags_DefaultOpen;

			if (mySelectedSkeletonNodeIndex == (int)e.joint)
				flags |= ImGuiTreeNodeFlags_Selected;

			if (joint.children.empty())
				flags |= ImGuiTreeNodeFlags_Leaf;

			e.isOpen = ImGui::TreeNodeEx(
				(void*)(intptr_t)e.joint,
				flags,
				"%s",
				joint.name.c_str()
			);

			if (ImGui::IsItemClicked())
			{
				int newIndex = (int)e.joint;

				if (mySelectedSkeletonNodeIndex == newIndex)
					mySelectedSkeletonNodeIndex = -1;
				else
					mySelectedSkeletonNodeIndex = newIndex;
			}

			e.childCursor = 0;

			if (!e.isOpen || joint.children.empty())
			{
				if (e.isOpen)
					ImGui::TreePop();

				sp--;
				continue;
			}
		}

		if (e.childCursor < (int)joint.children.size())
		{
			unsigned child = joint.children[e.childCursor++];
			stack[sp++] = { child, -1, false };
		}
		else
		{
			if (e.isOpen)
				ImGui::TreePop();

			sp--;
		}
	}

	if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
		ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
		!ImGui::IsAnyItemHovered())
	{
		mySelectedSkeletonNodeIndex = -1;
	}
	
}

void AnimationClipDocument::DrawPropertyPanel()
{
	bool hasModifications = false;
	static AnimationClip modifiedClip;
	static bool hasOperationInProgress = false;

	if (!hasOperationInProgress)
		modifiedClip = *myAnimationClip;

	hasOperationInProgress = false;

	if (PropertyEditor::PropertyHeader("Animation Clip"))
	{
		if (PropertyEditor::BeginPropertyTable())
		{
			PropertyEditor::PropertyLabel();
			ImGui::Text("Animation Source");
			PropertyEditor::PropertyValue();
			{
				StringId assetValue = modifiedClip.animationSourcePath;
				if (PropertyEditor::AssetField("##animationSourcePath", assetValue, { ".fbx" }))
				{
					modifiedClip.animationSourcePath = assetValue;
					hasModifications = true;
				}
			}

			PropertyEditor::PropertyLabel();
			ImGui::Text("Preview Model");
			PropertyEditor::PropertyValue();
			{
				StringId assetValue = modifiedClip.previewModelPath;
				if (PropertyEditor::AssetField("##previewModelPath", assetValue, { ".fbx" }))
				{
					modifiedClip.previewModelPath = assetValue;
					hasModifications = true;
				}
			}

			std::shared_ptr<const Skeleton> skeleton;
			FilePathStream dummyPath;
			if (!modifiedClip.previewModelPath.IsEmpty() && Settings::ResolveAssetPath(modifiedClip.previewModelPath, dummyPath))
			{
				skeleton = GetSkeleton(modifiedClip.previewModelPath.GetString());
			}

			std::shared_ptr<const Animation> animation;
			if (skeleton && !modifiedClip.animationSourcePath.IsEmpty() && Settings::ResolveAssetPath(modifiedClip.animationSourcePath, dummyPath))
			{
				animation = GetAnimation(modifiedClip.animationSourcePath.GetString(), skeleton);
			}

			if (animation && hasModifications && modifiedClip.startTime == 0.f && modifiedClip.endTime == 0.f)
			{
				modifiedClip.endTime = animation->duration;
			}


			PropertyEditor::PropertyLabel();
			ImGui::Text("Start Time");
			PropertyEditor::PropertyValue();

			ImGui::DragFloat("##Start Frame", &modifiedClip.startTime);
			if (ImGui::IsItemActive())
				hasOperationInProgress = true;
			if (ImGui::IsItemDeactivatedAfterEdit() && modifiedClip.startTime != myAnimationClip->startTime)
				hasModifications = true;
			
			ImGui::PushItemFlag(ImGuiItemFlags_Disabled, modifiedClip.startTime == 0.f);
			if (ImGui::Button("Reset##Start"))
			{
				modifiedClip.startTime = 0.f;
				hasModifications = true;
			}
			ImGui::PopItemFlag();

			PropertyEditor::PropertyLabel();
			ImGui::Text("End Time");
			PropertyEditor::PropertyValue();
			ImGui::DragFloat("##End Frame", &modifiedClip.endTime);
			if (ImGui::IsItemActive())
				hasOperationInProgress = true;
			if (ImGui::IsItemDeactivatedAfterEdit() && modifiedClip.endTime != myAnimationClip->endTime)
				hasModifications = true;

			ImGui::PushItemFlag(ImGuiItemFlags_Disabled, !animation || modifiedClip.endTime == animation->duration);
			if (ImGui::Button("Reset##sEnd"))
			{
				modifiedClip.endTime = animation->duration;
				hasModifications = true;
			}
			ImGui::PopItemFlag();

			PropertyEditor::PropertyLabel();
			ImGui::Text("Playback Rate");
			PropertyEditor::PropertyValue();
			ImGui::DragFloat("##Playback Rate", &modifiedClip.playbackRate);
			if (ImGui::IsItemActive())
				hasOperationInProgress = true;
			if (ImGui::IsItemDeactivatedAfterEdit() && modifiedClip.playbackRate != myAnimationClip->playbackRate)
				hasModifications = true;

			PropertyEditor::PropertyLabel();
			ImGui::Text("Is Looping");
			PropertyEditor::PropertyValue();
			if (ImGui::Checkbox("##Is Looping", &modifiedClip.isLooping) && modifiedClip.isLooping != myAnimationClip->isLooping)
				hasModifications = true;

			PropertyEditor::PropertyLabel();
			ImGui::Text("Is Syncronized");
			PropertyEditor::PropertyValue();
			if (ImGui::Checkbox("##Is Syncronized", &modifiedClip.isSyncronized) && modifiedClip.isSyncronized != myAnimationClip->isSyncronized)
				hasModifications = true;

			PropertyEditor::PropertyLabel();
			ImGui::Text("Syncronized Cycle Offset");
			PropertyEditor::PropertyValue();
			ImGui::DragFloat("##Syncronized Cycle Offset", &modifiedClip.cycleOffsetPercentage, 1.f, 0.f, 1.f);
			if (ImGui::IsItemActive())
				hasOperationInProgress = true;
			if (ImGui::IsItemDeactivatedAfterEdit() && modifiedClip.cycleOffsetPercentage != myAnimationClip->cycleOffsetPercentage)
				hasModifications = true;		

			PropertyEditor::PropertyLabel();
			ImGui::Text("Syncronized Cycle Count");
			PropertyEditor::PropertyValue();
			ImGui::DragFloat("##Syncronized Cycle Count", &modifiedClip.cycleCount);
			if (ImGui::IsItemActive())
				hasOperationInProgress = true;
			if (ImGui::IsItemDeactivatedAfterEdit() && modifiedClip.cycleCount != myAnimationClip->cycleCount)
				hasModifications = true;

			PropertyEditor::EndPropertyTable();
		}
	}

	if (hasModifications)
	{
		std::shared_ptr<ChangeAnimationClipCommand> command = std::make_shared<ChangeAnimationClipCommand>(*myAnimationClip, modifiedClip);
		CommandManager::DoCommand(command);
	}
}

void AnimationClipDocument::DrawPlayControls()
{
	const bool playingForward = myAnimationClip->playbackRate > 0.f;
	const float startTime = playingForward ? myAnimationClip->startTime : myAnimationClip->endTime;
	const float endTime = playingForward ? myAnimationClip->endTime : myAnimationClip->startTime;
	const bool playing = myPlayState == PlayState::Playing;

	ImGui::BeginDisabled(playing);
	if (ImGui::Button(ICON_LC_PLAY))
	{
		myPlayState = PlayState::Playing;

		if (myCurrentTime == endTime)
			myCurrentTime = startTime;
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::BeginDisabled(!playing);
	if (ImGui::Button(ICON_LC_PAUSE))
		myPlayState = PlayState::Stopped;
	ImGui::EndDisabled();

	ImGui::SameLine();
	if (ImGui::Button(ICON_LC_SQUARE))
	{
		myPlayState = PlayState::Stopped;
		myCurrentTime = startTime;
	}

	ImGui::SameLine();
	ImGui::SetNextItemWidth(-110.f);
	ImGui::SliderFloat("##Time", &myCurrentTime, myAnimationClip->startTime, myAnimationClip->endTime, "");
	ImGui::SameLine();
	ImGui::TextDisabled("%.2f / %.2f s", myCurrentTime, myAnimationClip->endTime);
}

void AnimationClipDocument::HandleDrop()
{

}

void AnimationClipDocument::BeginDragSelection(Vector2f mousePos)
{
	mousePos;
}

void AnimationClipDocument::EndDragSelection(Vector2f mousePos, bool isShiftDown)
{
	mousePos;
	isShiftDown;
}

void AnimationClipDocument::ClickSelection(Vector2f mousePos, uint32_t selectedId, bool isShiftDown)
{
	mousePos;
	selectedId;
	isShiftDown;
}

void AnimationClipDocument::BeginTransformation()
{

}

void AnimationClipDocument::UpdateTransformation(const Vector3f& referencePosition, const Matrix4x4f& transform)
{
	referencePosition;
	transform;
}

void AnimationClipDocument::EndTransformation()
{
}

Vector3f AnimationClipDocument::CalculateSelectionPosition()
{
	return {};
}

Matrix4x4f AnimationClipDocument::CalculateSelectionOrientation()
{
	return {};
}

bool AnimationClipDocument::HasTransformableSelection()
{
	return false;
}