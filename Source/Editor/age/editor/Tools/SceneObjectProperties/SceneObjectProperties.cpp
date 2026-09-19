#include <age/editor/Tools/SceneObjectProperties/SceneObjectProperties.h>

constexpr float pi = 3.14159265359f;
float deg_to_rad(float degree) { return (degree * (pi / 180.0f)); }

#include <fstream>

#include <age/editor/Editor.h>

#include <imgui.h>

#include <age/editor/CommandManager/CommandManager.h>
#include <age/script/Script.h>
#include <age/script/JsonData.h>
#include <age/settings/settings.h>

#include <age/stringRegistry/StringRegistry.h>
#include <age/imgui/ImguiPropertyEditor.h>
#include <age/scene/Scene.h>

#include <age/editor/Scene/SceneSelection.h>
#include <age/editor/Scene/ActiveScene.h>
#include <age/editor/Scene/SceneLightSelection.h>
#include <age/editor/Tools/SceneObjectProperties/ChangePropertyOverridesCommand.h>
#include <age/editor/Tools/SceneObjectProperties/ChangeSceneObjectNameCommand.h>
#include <age/editor/Tools/SceneObjectProperties/ChangeSceneObjectFolderCommand.h>
#include <age/editor/Tools/SceneObjectProperties/ChangeSceneObjectLightFieldCommand.h>
#include <age/editor/Tools/SceneObjectProperties/ChangeSceneLightingFieldCommand.h>
#include <age/editor/Tools/SceneObjectProperties/ChangeSceneEnvironmentTextureCommand.h>

#include <IconFontHeaders\IconsLucide.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>

#include <filesystem>

using namespace Ag;

namespace
{
	// Cached recursive scan of the project for environment-capable textures
	// (.hdr panoramas + .dds, which may be an already-authored cubemap).
	// Rebuilt on demand (Refresh button) rather than every frame: a big
	// project's asset tree is not something to re-walk per ImGui frame just
	// to populate a dropdown.
	std::vector<std::string>& EnvironmentTextureCache()
	{
		static std::vector<std::string> cache;
		return cache;
	}

	void RescanEnvironmentTextures()
	{
		std::vector<std::string>& cache = EnvironmentTextureCache();
		cache.clear();
		std::error_code ec;
		const std::filesystem::path root(Settings::GameAssetRoot());
		for (auto it = std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, ec);
			it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
		{
			if (ec) break;
			if (!it->is_regular_file(ec)) continue;
			const std::filesystem::path& p = it->path();
			bool inTrash = false;
			for (const auto& part : std::filesystem::relative(p, root, ec)) if (part == ".trash") inTrash = true;
			if (inTrash) continue;
			std::string ext = p.extension().string();
			for (char& c : ext) c = (char)tolower((unsigned char)c);
			if (ext != ".hdr" && ext != ".dds") continue;
			std::error_code relEc;
			std::filesystem::path rel = std::filesystem::relative(p, root, relEc);
			if (relEc) continue;
			cache.push_back(rel.generic_string());
		}
		std::sort(cache.begin(), cache.end());
	}

	// Combo box scanning the project for .hdr/.dds files, mirroring the
	// BeginCombo/Selectable pattern SceneObjectProperties already uses for
	// the folder-path picker further down in this file.
	void DrawEnvironmentTexturePicker(Scene& scene)
	{
		std::vector<std::string>& options = EnvironmentTextureCache();
		if (options.empty()) RescanEnvironmentTextures();   // first use of this panel

		const std::string current = scene.GetEnvironmentTexturePath();
		auto choose = [&](const std::string& aNewPath)
		{
			if (aNewPath == current) return;
			CommandManager::DoCommand(std::make_shared<ChangeSceneEnvironmentTextureCommand>(aNewPath, current));
		};
		if (ImGui::BeginCombo("##EnvironmentTexture", current.empty() ? "None (uniform ambient)" : current.c_str()))
		{
			if (ImGui::Selectable("None (uniform ambient)", current.empty()))
				choose(std::string());
			for (size_t i = 0; i < options.size(); ++i)
			{
				ImGui::PushID((int)i);
				const bool isSelected = current == options[i];
				if (ImGui::Selectable(options[i].c_str(), isSelected))
					choose(options[i]);
				if (isSelected) ImGui::SetItemDefaultFocus();
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Refresh")) RescanEnvironmentTextures();
	}

	// Call right after an ImGui widget bound directly to a live light field
	// (same direct-binding style the Transform block below uses for
	// position/rotation/scale), so dragging previews in the viewport in real
	// time. Captures the value from the instant the drag starts and pushes
	// one undo command covering the whole drag when it ends, rather than one
	// command per intermediate frame. Only one widget can be mid-drag at a
	// time -- ImGui itself enforces that -- so a single static capture
	// buffer is safe to share across every field/object this is called for.
	void DragLightField(uint32_t aObjectId, LightField aFieldTag, float* aLive, int aCount)
	{
		static uint32_t sActiveObjectId = 0;
		static LightField sActiveField = LightField::Color;
		static float sOldValue[3] = {};

		if (ImGui::IsItemActivated())
		{
			sActiveObjectId = aObjectId;
			sActiveField = aFieldTag;
			std::memcpy(sOldValue, aLive, sizeof(float) * aCount);
		}
		if (ImGui::IsItemDeactivatedAfterEdit() && sActiveObjectId == aObjectId && sActiveField == aFieldTag)
		{
			std::shared_ptr<ChangeSceneObjectLightFieldCommand> command =
				std::make_shared<ChangeSceneObjectLightFieldCommand>(aObjectId, aFieldTag, aLive, sOldValue, aCount);
			CommandManager::DoCommand(command);
		}
	}

	// Same idea as DragLightField, for the Scene-level sun/ambient fields
	// (no object id to key on -- only one Scene is ever being edited).
	void DragSceneLightingField(SceneLightingField aFieldTag, float* aLive, int aCount)
	{
		static SceneLightingField sActiveField = SceneLightingField::SunYaw;
		static float sOldValue[3] = {};

		if (ImGui::IsItemActivated())
		{
			sActiveField = aFieldTag;
			std::memcpy(sOldValue, aLive, sizeof(float) * aCount);
		}
		if (ImGui::IsItemDeactivatedAfterEdit() && sActiveField == aFieldTag)
		{
			std::shared_ptr<ChangeSceneLightingFieldCommand> command =
				std::make_shared<ChangeSceneLightingFieldCommand>(aFieldTag, aLive, sOldValue, aCount);
			CommandManager::DoCommand(command);
		}
	}
}

namespace
{
	// A collapsible category with its own property table; pair with EndPropertyTable().
	bool SectionBegin(const char* aLabel, const bool aForceOpen)
	{
		if (aForceOpen) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
		else ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		return ImGui::CollapsingHeader(aLabel) && PropertyEditor::BeginPropertyTable();
	}

	bool ContainsNoCase(const char* aText, const char* aFilter)
	{
		std::string text = aText;
		std::string filter = aFilter;
		std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)tolower(c); });
		std::transform(filter.begin(), filter.end(), filter.begin(), [](unsigned char c) { return (char)tolower(c); });
		return text.find(filter) != std::string::npos;
	}
}

void SceneObjectProperties::Draw()
{
	static char locFilter[128] = "";
	static char locCreateNewFolderBuffer[512];
	static std::vector<StringId> allFolderNames;

	// The hierarchy panel's Sun/Ambient rows (SceneObjectList.cpp) only ever
	// updated SceneLightSelection's own selection state -- nothing anywhere
	// read it back to actually draw a properties panel, so clicking either
	// row looked like it did nothing. Scene owns these as plain scalar
	// fields (not SceneObjects; see Scene.h's comment on mySunYaw etc.), so
	// this branches before the normal per-SceneObject id loop below instead
	// of going through SceneSelection/ChangePropertyOverridesCommand.
	const SceneLightSelection selectedLight = GetSelectedSceneLight();
	if (selectedLight != SceneLightSelection::None)
	{
		Scene* scene = GetActiveScene();
		if (scene && PropertyEditor::BeginPropertyTable())
		{
			if (selectedLight == SceneLightSelection::Sun)
			{
				// Direct-bind + apply-on-change already gave live preview;
				// the Drag*Field call after each widget is what's new -- it
				// batches the whole drag into one undo entry (ChangeSceneLightingFieldCommand)
				// instead of leaving these completely outside the undo stack.
				float yaw = scene->GetSunYaw();
				PropertyEditor::PropertyLabel();
				ImGui::Text("Yaw");
				PropertyEditor::HelpMarker("Sun rotation around the vertical axis, in degrees");
				PropertyEditor::PropertyValue();
				if (ImGui::DragFloat("##SunYaw", &yaw, 0.5f)) scene->SetSunYaw(yaw);
				DragSceneLightingField(SceneLightingField::SunYaw, &yaw, 1);

				float pitch = scene->GetSunPitch();
				PropertyEditor::PropertyLabel();
				ImGui::Text("Pitch");
				PropertyEditor::HelpMarker("Sun elevation angle, in degrees. Negative points the sun down toward the scene");
				PropertyEditor::PropertyValue();
				if (ImGui::DragFloat("##SunPitch", &pitch, 0.5f, -90.f, 90.f)) scene->SetSunPitch(pitch);
				DragSceneLightingField(SceneLightingField::SunPitch, &pitch, 1);

				PropertyEditor::PropertyLabel();
				ImGui::Text("Color");
				PropertyEditor::HelpMarker("Sun light color");
				PropertyEditor::PropertyValue();
				{
					float* sunColor = scene->GetSunColor();
					ImGui::ColorEdit3("##SunColor", sunColor);
					DragSceneLightingField(SceneLightingField::SunColor, sunColor, 3);
				}

				float intensity = scene->GetSunIntensity();
				PropertyEditor::PropertyLabel();
				ImGui::Text("Intensity");
				PropertyEditor::HelpMarker("Sun light intensity multiplier");
				PropertyEditor::PropertyValue();
				if (ImGui::DragFloat("##SunIntensity", &intensity, 0.01f, 0.f, 100.f)) scene->SetSunIntensity(intensity);
				DragSceneLightingField(SceneLightingField::SunIntensity, &intensity, 1);
			}
			else // Ambient
			{
				PropertyEditor::PropertyLabel();
				ImGui::Text("Color");
				PropertyEditor::HelpMarker("Uniform ambient fill light color, added everywhere regardless of surface normal. Ignored while an Environment texture below is set");
				PropertyEditor::PropertyValue();
				{
					float* ambientColor = scene->GetAmbientColor();
					ImGui::ColorEdit3("##AmbientColor", ambientColor);
					DragSceneLightingField(SceneLightingField::AmbientColor, ambientColor, 3);
				}

				PropertyEditor::PropertyLabel();
				ImGui::Text("Environment");
				PropertyEditor::HelpMarker("A .hdr panorama or .dds cubemap used as the ambient/IBL environment instead of the flat color above");
				PropertyEditor::PropertyValue();
				DrawEnvironmentTexturePicker(*scene);
			}

			PropertyEditor::EndPropertyTable();
		}
		return;
	}

	std::span<const uint32_t> currentSelection = SceneSelection::GetActiveSceneSelection()->GetSelection();

	bool hasSameSelection = true;
	if (myPreviousSelection.size() != currentSelection.size())
	{
		hasSameSelection = false;
	}
	else
	{
		for (int i = 0; i < currentSelection.size(); i++)
		{
			if (myPreviousSelection[i] != currentSelection[i])
			{
				hasSameSelection = false;
				break;
			}
		}
	}

	if (!hasSameSelection)
	{
		myTransformCommand.End();
		myTransformCommand = {};

		myPreviousSelection.clear();
		for (int i = 0; i < currentSelection.size(); i++)
		{
			myPreviousSelection.push_back(currentSelection[i]);
		}
	}

	{
		for (uint32_t id : currentSelection)
		{
			ImGui::PushID(id);

			// The selection can briefly reference an id the scene no longer has
			// (e.g. deleted by another panel or an undo this same frame, before
			// the selection list itself is resynced) -- GetSceneObject() returns
			// null for that rather than a stale pointer, so guard against it
			// instead of dereferencing unconditionally.
			SceneObject* objectPtr = GetActiveScene()->GetSceneObject(id);
			if (!objectPtr) { ImGui::PopID(); continue; }

			{
				SceneObject& object = *objectPtr;

				{
					char buffer[512];
					strncpy_s(buffer, object.GetName(), sizeof(buffer));
					buffer[sizeof(buffer) - 1] = '\0';

					ImGui::SetNextItemWidth(-1);
					ImGui::InputText("##Name", buffer, IM_ARRAYSIZE(buffer));
					if (ImGui::IsItemDeactivatedAfterEdit())
					{
						std::shared_ptr<ChangeSceneObjectNameCommand> command = std::make_shared<ChangeSceneObjectNameCommand>(id, buffer, object.GetName());
						CommandManager::DoCommand(command);
					}
				}
				if (object.IsLight())
					ImGui::TextDisabled("%s", object.GetType() == SceneObjectType::SpotLight ? "Spot Light" : "Point Light");
				else
					ImGui::TextDisabled("Instance of %s", object.GetSceneObjectDefinitionName().GetString());

				ImGui::SetNextItemWidth(-1);
				ImGui::InputTextWithHint("##DetailsFilter", ICON_LC_SEARCH " Search details...", locFilter, IM_ARRAYSIZE(locFilter));
				const bool filtering = locFilter[0] != '\0';

				if (!filtering && SectionBegin(ICON_LC_TAG " General", false))
				{
				PropertyEditor::PropertyLabel();

				ImGui::Text("Path");
				PropertyEditor::HelpMarker("Used to organize objects in the Instances window. Use \"/\" to create folder hierarchies");

				PropertyEditor::PropertyValue();

				allFolderNames.clear();
				GetActiveScene()->GetAllFolderNames(allFolderNames);

				StringId createNewName = "Create New..."_tgaid;
				allFolderNames.push_back(createNewName);

				StringId currentPath = object.GetPath();
				StringId newPath = currentPath;
				if (ImGui::BeginCombo("##Path", currentPath.GetString(), 0))
				{
					for (int i = 0; i < allFolderNames.size(); i++)
					{
						ImGui::PushID(i);

						StringId rowName = allFolderNames[i];
						bool isSelected = currentPath == allFolderNames[i];
						if (ImGui::Selectable(allFolderNames[i].GetString(), isSelected))
						{
							newPath = rowName;
						}

						if (isSelected)
							ImGui::SetItemDefaultFocus();

						ImGui::PopID();
					}
					ImGui::EndCombo();
				}

				if (newPath == createNewName)
				{
					ImGui::OpenPopup("Add New Folder");
				}
				else if (newPath != currentPath)
				{
					std::shared_ptr<ChangeSceneObjectFolderCommand> command = std::make_shared<ChangeSceneObjectFolderCommand>(id, newPath, object.GetPath());
					CommandManager::DoCommand(command);
				}

				if (ImGui::BeginPopupModal("Add New Folder", NULL, ImGuiWindowFlags_AlwaysAutoResize))
				{

					ImGui::InputText("##Folder Name", locCreateNewFolderBuffer, IM_ARRAYSIZE(locCreateNewFolderBuffer), ImGuiInputTextFlags_AutoSelectAll);

					ImGui::Separator();

					if (ImGui::Button("Create", ImVec2(120, 0)))
					{				
						std::shared_ptr<ChangeSceneObjectFolderCommand> command = std::make_shared<ChangeSceneObjectFolderCommand>(id, StringRegistry::RegisterOrGetString(locCreateNewFolderBuffer), object.GetPath());
						CommandManager::DoCommand(command);

						ImGui::CloseCurrentPopup();
					}

					ImGui::SetItemDefaultFocus();
					ImGui::SameLine();
					if (ImGui::Button("Cancel", ImVec2(120, 0)))
					{
						ImGui::CloseCurrentPopup();
					}

					ImGui::EndPopup();
				}


				PropertyEditor::EndPropertyTable();
				}

				if (!filtering && SectionBegin(ICON_LC_MOVE_3D " Transform", false))
				{
					bool anyActive = false;
					bool anyDeactivatedAfterEdit = false;

					{
						PropertyEditor::PropertyLabel(true);

						ImGui::Indent();
						ImGui::Text("Position");
						ImGui::Unindent();

						PropertyEditor::PropertyValue(true);

						ImGui::DragFloat3("##Position", object.GetPosition().myValues);
						if (ImGui::IsItemActive())
							anyActive = true;
						if (ImGui::IsItemDeactivatedAfterEdit())
							anyDeactivatedAfterEdit = true;

						PropertyEditor::PropertyLabel(true);

						ImGui::Indent();
						ImGui::Text("Rotation");
						ImGui::Unindent();

						PropertyEditor::PropertyValue(true);

						ImGui::DragFloat3("##Rotation", object.GetEuler().myValues);
						if (ImGui::IsItemActive())
							anyActive = true;
						if (ImGui::IsItemDeactivatedAfterEdit())
							anyDeactivatedAfterEdit = true;

						PropertyEditor::PropertyLabel(true);

						ImGui::Indent();
						ImGui::Text("Scale");
						ImGui::Unindent();

						PropertyEditor::PropertyValue(true);

						ImGui::DragFloat3("##Scale", object.GetScale().myValues, 0.01f);
						if (ImGui::IsItemActive())
							anyActive = true;
						if (ImGui::IsItemDeactivatedAfterEdit())
							anyDeactivatedAfterEdit = true;
					}

					if (anyActive)
					{
						if (myTransformCommand.IsEmpty())
							myTransformCommand.Begin(SceneSelection::GetActiveSceneSelection()->GetSelection());
					}
					else if (anyDeactivatedAfterEdit)
					{
						myTransformCommand.End();
						myTransformCommand = {};
					}
					PropertyEditor::EndPropertyTable();
				}

				// Lights previously had creation defaults only -- nothing here
				// let you edit a light once placed. Point and spot lights share
				// SceneObject's fields (SceneObject.h); inner/outer cone only
				// mean anything for spot lights, so they're hidden for point
				// lights rather than shown disabled.
				if (!filtering && object.IsLight() && SectionBegin(ICON_LC_LIGHTBULB " Light", false))
				{
					const bool isSpot = object.GetType() == SceneObjectType::SpotLight;

					PropertyEditor::PropertyLabel();
					ImGui::Text("Light");
					PropertyEditor::PropertyValue();
					ImGui::TextDisabled(isSpot ? "Spot Light" : "Point Light");

					PropertyEditor::PropertyLabel();
					ImGui::Text("Color");
					PropertyEditor::HelpMarker("HDR: drag a channel above 1.0 for a brighter-than-white light. "
						"There is no separate intensity field -- this color's own magnitude is the light's brightness.");
					PropertyEditor::PropertyValue();
					{
						float* color = object.GetLightColor();
						ImGui::ColorEdit3("##LightColor", color, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
						DragLightField(id, LightField::Color, color, 3);
					}

					PropertyEditor::PropertyLabel();
					ImGui::Text("Range");
					PropertyEditor::HelpMarker("Distance in scene units the light's influence extends to.");
					PropertyEditor::PropertyValue();
					{
						float& range = object.GetLightRange();
						ImGui::DragFloat("##LightRange", &range, 1.f, 0.f, 100000.f, "%.0f");
						DragLightField(id, LightField::Range, &range, 1);
					}

					PropertyEditor::PropertyLabel();
					ImGui::Text("Source radius");
					PropertyEditor::HelpMarker("Soft-shadow source size; 0 is a hard point/spot light.");
					PropertyEditor::PropertyValue();
					{
						float& radius = object.GetLightRadius();
						ImGui::DragFloat("##LightRadius", &radius, 0.1f, 0.f, 1000.f, "%.1f");
						DragLightField(id, LightField::Radius, &radius, 1);
					}

					if (isSpot)
					{
						PropertyEditor::PropertyLabel();
						ImGui::Text("Inner cone");
						PropertyEditor::HelpMarker("Degrees. Fully bright inside this cone; fades out to the outer cone.");
						PropertyEditor::PropertyValue();
						{
							float& inner = object.GetLightInnerAngle();
							ImGui::DragFloat("##LightInnerAngle", &inner, 0.5f, 0.f, object.GetLightOuterAngle(), "%.1f deg");
							DragLightField(id, LightField::InnerAngle, &inner, 1);
						}

						PropertyEditor::PropertyLabel();
						ImGui::Text("Outer cone");
						PropertyEditor::HelpMarker("Degrees. The spot's total cone angle; no light beyond this.");
						PropertyEditor::PropertyValue();
						{
							float& outer = object.GetLightOuterAngle();
							ImGui::DragFloat("##LightOuterAngle", &outer, 0.5f, object.GetLightInnerAngle(), 89.f, "%.1f deg");
							DragLightField(id, LightField::OuterAngle, &outer, 1);
						}
					}
					PropertyEditor::EndPropertyTable();
				}

				std::vector<SceneObject::PropertySourceAndOveride> allProperties;
				object.CalculateEditablePropertySet(Editor::GetEditor()->GetSceneObjectDefinitionManager(), allProperties);

				if (!allProperties.empty() && SectionBegin(ICON_LC_SLIDERS_HORIZONTAL " Properties", filtering))
				{
				for (const SceneObject::PropertySourceAndOveride& propertySourceAndOverride : allProperties)
				{
					if (filtering && !ContainsNoCase(propertySourceAndOverride.source.name.GetString(), locFilter))
						continue;
					ImGui::PushID(propertySourceAndOverride.source.name.GetString());

					// todo: probably show groups here also
					const bool isOverridden = propertySourceAndOverride.override.name == propertySourceAndOverride.source.name;
					SceneProperty newProperty = propertySourceAndOverride.source;
					if (isOverridden)
					{
						newProperty.value = propertySourceAndOverride.override.value;
					}

					// An overridden property's row (ShowImGuiEditor draws its own
					// label internally, hence tinting via ImGuiCol_Text rather
					// than a separate label call) reads in the accent colour
					// instead of the default text colour -- previously there was
					// no way to tell an inherited value from an overridden one
					// just by looking at the panel, despite the data to do so
					// (PropertySourceAndOveride itself) already existing.
					if (isOverridden) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
					const bool changed = newProperty.value.ShowImGuiEditor(newProperty.name.GetString(), propertySourceAndOverride.source.description.GetString());
					if (isOverridden) ImGui::PopStyleColor();

					if (isOverridden)
					{
						ImGui::SameLine();
						if (ImGui::SmallButton("Revert"))
						{
							// ChangePropertyOverridesCommand::Execute() already
							// special-cases an empty-named "new" value as "remove
							// the override matching the old value's name" -- built
							// for exactly this, just never called with one before.
							std::shared_ptr<ChangePropertyOverridesCommand> command =
								std::make_shared<ChangePropertyOverridesCommand>(id, SceneProperty{}, propertySourceAndOverride.override);
							CommandManager::DoCommand(command);
						}
						if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
							ImGui::SetTooltip("Revert to the prefab/definition value");
					}

					if (changed)
					{
						std::shared_ptr<ChangePropertyOverridesCommand> command = std::make_shared<ChangePropertyOverridesCommand>(id, newProperty, propertySourceAndOverride.override);
						CommandManager::DoCommand(command);
					}

					ImGui::PopID();
				}
				PropertyEditor::EndPropertyTable();
				}
			}

			ImGui::PopID();

		}
	}
}

