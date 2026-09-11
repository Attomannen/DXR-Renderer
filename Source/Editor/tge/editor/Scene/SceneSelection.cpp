#include <tge/editor/Scene/SceneSelection.h>

#include <algorithm>

#include <tge/graphics/Camera.h>
#include <tge/scene/Scene.h>
#include <tge/editor/CommandManager/CommandManager.h>
#include <tge/math/BoxSphereBounds.h>

#include <imgui/imgui.h>
using namespace Tga;

static RectSelection locInRectSelection;
RectSelection* RectSelection::GetCurrentRectSelection()
{
	return &locInRectSelection;
}

std::vector<uint32_t> RectSelection::GetSelection() const
{
	return mySelection;
}

void RectSelection::AddToSelection(uint32_t anInstance)
{
	if (anInstance > 0 && std::find(mySelection.begin(), mySelection.end(), anInstance) == mySelection.end())
	{
		mySelection.push_back(anInstance);
	}
}

bool RectSelection::IsActive()
{
	return mySelection.size() > 0;
}

void RectSelection::RemoveFromSelection(uint32_t anInstance)
{
	mySelection.erase(std::remove(mySelection.begin(), mySelection.end(), anInstance), mySelection.end());
}

void RectSelection::ClearSelection()
{
	mySelection.clear();
}

bool RectSelection::Contains(const uint32_t anInstance) const
{
	return std::find(mySelection.begin(), mySelection.end(), anInstance) != mySelection.end();
}

static void RectSelectFrustum(Tga::SelectionRect& aRect, const Tga::Camera &aCamera, const Vector2f &aViewportSize)
{
	const Tga::Matrix4x4f &projection = aCamera.GetProjection();
	Tga::Vector2f resolution{aViewportSize.x, aViewportSize.y};

	////////////////////////////////////////////////////////
	// produce DIRECTION where the top left and bottom right 
	// corners of the selectionbox are in viewspace. Therefor normalized
	// To find frustum Near/Far-rectangle we would multiply with the distance to near/far
	Tga::Vector3f tl = Tga::Vector3f { 
		((2.0f * std::min(aRect.start.x, aRect.end.x) / resolution.x) - 1.0f) / projection(1,1),
		(1.0f - (2.0f * std::min(aRect.start.y, aRect.end.y)) / resolution.y) / projection(2,2),
		1.f
	}.GetNormalized();

	Tga::Vector3f br = Tga::Vector3f { 
		((2.0f * std::max(aRect.start.x, aRect.end.x) / resolution.x) - 1.0f) / projection(1,1),
		(1.0f - (2.0f * std::max(aRect.start.y, aRect.end.y)) / resolution.y) / projection(2,2),
		1.f
	}.GetNormalized();

	Tga::Vector3f tr = Tga::Vector3f { 
		((2.0f * std::max(aRect.start.x, aRect.end.x) / resolution.x) - 1.0f) / projection(1,1),
		(1.0f - (2.0f * std::min(aRect.start.y, aRect.end.y)) / resolution.y) / projection(2,2),
		1.f
	}.GetNormalized();

	Tga::Vector3f bl = Tga::Vector3f { 
		((2.0f * std::min(aRect.start.x, aRect.end.x) / resolution.x) - 1.0f) / projection(1,1),
		(1.0f - (2.0f * std::max(aRect.start.y, aRect.end.y)) / resolution.y) / projection(2,2),
		1.f
	}.GetNormalized();
	//Tga::Vector3f bl = Tga::Vector3f{ tl.x, br.y, 1.f }.GetNormalized();
	//Tga::Vector3f tr = Tga::Vector3f{ br.x, tl.y, 1.f }.GetNormalized();

	/////////////////////////////////////////////////////
	// Define frustum planes these normals are pointing inward 
	float close; float distant;
	aCamera.GetProjectionPlanes(close, distant);
	{
		aRect.frustum.left.pos = { tl };
		aRect.frustum.left.normal = (tl * close - tl * distant).Cross(tl * close - bl * close).GetNormalized();
		aRect.frustum.right.pos = { tr };
		aRect.frustum.right.normal = (br * close - tr * close).Cross(tr * distant - tr * close).GetNormalized();
		aRect.frustum.top.pos = { tl };
		aRect.frustum.top.normal = (tr * close - tl * close).Cross(tl * distant - tl * close).GetNormalized();
		aRect.frustum.bottom.pos = { bl };
		aRect.frustum.bottom.normal = (br * distant - br * close).Cross(br * close - bl * close).GetNormalized();
		aRect.frustum.nearplane.pos = { bl * close };
		aRect.frustum.nearplane.normal = (br * close - bl * close).Cross(tr * close - br * close).GetNormalized();
		aRect.frustum.farplane.pos = { bl * distant };
		aRect.frustum.farplane.normal = (tr * distant - br * distant).Cross(br * distant - bl * distant).GetNormalized();
	}

	aRect.frustum.nearrect.tl = tl*100.f;
	aRect.frustum.nearrect.tr = tr*100.f;
	aRect.frustum.nearrect.bl = bl*100.f;
	aRect.frustum.nearrect.br = br*100.f;

	aRect.frustum.farrect.tl = tl*distant;
	aRect.frustum.farrect.tr = tr*distant;
	aRect.frustum.farrect.bl = bl*distant;
	aRect.frustum.farrect.br = br*distant;
}

bool RectSelection::CheckFrustum(const Tga::BoxSphereBounds& someBounds)
{
	Tga::Vector3f FrustumToPoint{};

	FrustumToPoint = someBounds.center - mySelectRect.frustum.nearrect.tl;
	if (mySelectRect.frustum.top.normal.Dot(FrustumToPoint) <= 0.f) { return false; }
	FrustumToPoint = someBounds.center - mySelectRect.frustum.nearrect.tr;
	if (mySelectRect.frustum.right.normal.Dot(FrustumToPoint) <= 0.f) { return false; }
	FrustumToPoint = someBounds.center - mySelectRect.frustum.nearrect.br;
	if (mySelectRect.frustum.bottom.normal.Dot(FrustumToPoint) <= 0.f) { return false; }
	FrustumToPoint = someBounds.center - mySelectRect.frustum.nearrect.bl;
	if (mySelectRect.frustum.left.normal.Dot(FrustumToPoint) <= 0.f) { return false; }
	FrustumToPoint = someBounds.center - mySelectRect.frustum.nearrect.bl;
	if (mySelectRect.frustum.nearplane.normal.Dot(FrustumToPoint) <= 0.f) { return false; }
	FrustumToPoint = someBounds.center - mySelectRect.frustum.farrect.bl;
	if (mySelectRect.frustum.farplane.normal.Dot(FrustumToPoint) <= 0.f) { return false; }

	return true;
}

void RectSelection::Update(const Vector2f& aMousePos, const Vector2f& aViewportPos, const Vector2f aViewportSize, const Tga::Camera& aCamera)
{
	ImGuiMouseButton button = ImGuiMouseButton_Left;
	if (ImGui::IsMouseClicked(button))
	{ 
		mySelectRect.start = aMousePos;
	}
	if (ImGui::IsMouseDown(button))
	{
		mySelectRect.end = aMousePos;

		if (mySelectRect.start != mySelectRect.end)
		{
			ImDrawList* draw_list = ImGui::GetForegroundDrawList(); //ImGui::GetWindowDrawList();
			ImVec2 start = { mySelectRect.start.x + aViewportPos.x, mySelectRect.start.y + aViewportPos.y };
			ImVec2 end = { mySelectRect.end.x + aViewportPos.x, mySelectRect.end.y + aViewportPos.y };

			draw_list->AddRect(start, end, ImGui::GetColorU32(IM_COL32(0, 130, 216, 255)));   // Border
			draw_list->AddRectFilled(start, end, ImGui::GetColorU32(IM_COL32(0, 130, 216, 50)));    // Background
		}
	}

	///////////////////////////////
	// Create frustum for selection
	RectSelectFrustum(mySelectRect, aCamera, aViewportSize);
}

//////////////////////////////////////////

std::vector<uint32_t>& SceneSelection::GetOrCreateSelection()
{
	if (myUndoStack.empty())
		myUndoStack.emplace_back();

	return myUndoStack.back().selection;
}

const std::vector<uint32_t>* SceneSelection::GetSelectionInternal() const
{
	if (myUndoStack.empty())
		return nullptr;

	return &myUndoStack.back().selection;
}

SceneSelection* locActiveSelection;
SceneSelection* locPrevActiveSelection;

SceneSelection* SceneSelection::GetActiveSceneSelection()
{
	return locActiveSelection;
}

void SceneSelection::SetActiveSceneSelection(SceneSelection* selection)
{
	locActiveSelection = selection;
}


std::span<const uint32_t> SceneSelection::GetSelection() const
{
	auto selection = GetSelectionInternal();

	if (selection == nullptr)
		return {};

	return *selection;
}

void SceneSelection::SetSelection(const std::span<uint32_t>& aNewSelection)
{
	std::vector<uint32_t>& selection = GetOrCreateSelection();

	selection.resize(aNewSelection.size());
	memcpy(selection.data(), aNewSelection.data(), sizeof(uint32_t) * aNewSelection.size());
}

void SceneSelection::AddToSelection(uint32_t anInstance)
{
	std::vector<uint32_t>& selection = GetOrCreateSelection();

	if (anInstance > 0 && std::find(selection.begin(), selection.end(), anInstance) == selection.end())
	{
		selection.push_back(anInstance);
	}
}

void SceneSelection::RemoveFromSelection(uint32_t anInstance)
{
	std::vector<uint32_t>& selection = GetOrCreateSelection();

	selection.erase(std::remove(selection.begin(), selection.end(), anInstance), selection.end());
}

void SceneSelection::ClearSelection()
{
	if (GetSelection().size() != 0)
		GetOrCreateSelection().clear();
}

bool SceneSelection::Contains(const uint32_t anInstance) const
{
	const std::span<const uint32_t>& selection = GetSelection();

	return std::find(selection.begin(), selection.end(), anInstance) != selection.end();
}

void SceneSelection::ToggleSelect(uint32_t anInstance)
{
	const std::span<const uint32_t>& selection = GetSelection();

	if (std::find(selection.begin(), selection.end(), anInstance) != selection.end()) 
	{
		RemoveFromSelection(anInstance);
	}
	else 
	{
		AddToSelection(anInstance);
	}
}

void SceneSelection::OnAction(CommandManager::Action action)
{
	if (action == CommandManager::Action::Do)
	{
		myRedoStack.clear();
		myUndoStack.push_back(myUndoStack.empty() ? SelectionUndoState{} : myUndoStack.back()); // duplicate selection state to have a value for each command
	}
	else if (action == CommandManager::Action::PreUndo)
	{
		assert(locPrevActiveSelection == nullptr);
		locPrevActiveSelection = locActiveSelection;
		locActiveSelection = this;
	}
	else if (action == CommandManager::Action::PostUndo)
	{
		assert(locActiveSelection == this);
		locActiveSelection = locPrevActiveSelection;
		locPrevActiveSelection = nullptr;

		myRedoStack.push_back(myUndoStack.back());
		myUndoStack.pop_back();
	}
	else if (action == CommandManager::Action::PreRedo)
	{
		assert(locPrevActiveSelection == nullptr);
		locPrevActiveSelection = locActiveSelection;
		locActiveSelection = this;
	}
	else if (action == CommandManager::Action::PostRedo)
	{
		assert(locActiveSelection == this);

		locActiveSelection = locPrevActiveSelection;
		locPrevActiveSelection = nullptr;

		myUndoStack.push_back(myRedoStack.back());
		myRedoStack.pop_back();
	}
	else if (action == CommandManager::Action::Clear)
	{
		myUndoStack.clear();
		myRedoStack.clear();
	}
}
