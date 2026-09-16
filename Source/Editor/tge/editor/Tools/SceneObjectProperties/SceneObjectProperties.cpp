#include <tge/editor/Tools/SceneObjectProperties/SceneObjectProperties.h>

constexpr float pi = 3.14159265359f;
float deg_to_rad(float degree) { return (degree * (pi / 180.0f)); }

#include <fstream>

#include <tge/editor/Editor.h>

#include <imgui.h>

#include <tge/editor/CommandManager/CommandManager.h>
#include <tge/script/Script.h>
#include <tge/script/ScriptManager.h>
#include <tge/script/JsonData.h>
#include <tge/settings/settings.h>

#include <tge/stringRegistry/StringRegistry.h>
#include <tge/imgui/ImguiPropertyEditor.h>
#include <tge/scene/Scene.h>

#include <tge/editor/Scene/SceneSelection.h>
#include <tge/editor/Scene/ActiveScene.h>
#include <tge/editor/Scene/SceneLightSelection.h>
#include <tge/editor/Tools/SceneObjectProperties/ChangePropertyOverridesCommand.h>
#include <tge/editor/Tools/SceneObjectProperties/ChangeSceneObjectNameCommand.h>
#include <tge/editor/Tools/SceneObjectProperties/ChangeSceneObjectFolderCommand.h>
#include <tge/editor/p4/p4.h>

#include <vector>
#include <algorithm>

#include <filesystem>

using namespace Tga;

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
		if (ImGui::BeginCombo("##EnvironmentTexture", current.empty() ? "None (uniform ambient)" : current.c_str()))
		{
			if (ImGui::Selectable("None (uniform ambient)", current.empty()))
				scene.SetEnvironmentTexturePath(std::string());
			for (size_t i = 0; i < options.size(); ++i)
			{
				ImGui::PushID((int)i);
				const bool isSelected = current == options[i];
				if (ImGui::Selectable(options[i].c_str(), isSelected))
					scene.SetEnvironmentTexturePath(options[i]);
				if (isSelected) ImGui::SetItemDefaultFocus();
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Refresh")) RescanEnvironmentTextures();
	}
}

void SceneObjectProperties::Draw()
{
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
				float yaw = scene->GetSunYaw();
				PropertyEditor::PropertyLabel();
				ImGui::Text("Yaw");
				PropertyEditor::HelpMarker("Sun rotation around the vertical axis, in degrees");
				PropertyEditor::PropertyValue();
				if (ImGui::DragFloat("##SunYaw", &yaw, 0.5f)) scene->SetSunYaw(yaw);

				float pitch = scene->GetSunPitch();
				PropertyEditor::PropertyLabel();
				ImGui::Text("Pitch");
				PropertyEditor::HelpMarker("Sun elevation angle, in degrees. Negative points the sun down toward the scene");
				PropertyEditor::PropertyValue();
				if (ImGui::DragFloat("##SunPitch", &pitch, 0.5f, -90.f, 90.f)) scene->SetSunPitch(pitch);

				PropertyEditor::PropertyLabel();
				ImGui::Text("Color");
				PropertyEditor::HelpMarker("Sun light color");
				PropertyEditor::PropertyValue();
				ImGui::ColorEdit3("##SunColor", scene->GetSunColor());

				float intensity = scene->GetSunIntensity();
				PropertyEditor::PropertyLabel();
				ImGui::Text("Intensity");
				PropertyEditor::HelpMarker("Sun light intensity multiplier");
				PropertyEditor::PropertyValue();
				if (ImGui::DragFloat("##SunIntensity", &intensity, 0.01f, 0.f, 100.f)) scene->SetSunIntensity(intensity);
			}
			else // Ambient
			{
				PropertyEditor::PropertyLabel();
				ImGui::Text("Color");
				PropertyEditor::HelpMarker("Uniform ambient fill light color, added everywhere regardless of surface normal. Ignored while an Environment texture below is set");
				PropertyEditor::PropertyValue();
				ImGui::ColorEdit3("##AmbientColor", scene->GetAmbientColor());

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

			if (PropertyEditor::BeginPropertyTable())
			{
				SceneObject& object = *GetActiveScene()->GetSceneObject(id);

				PropertyEditor::PropertyLabel();

				ImGui::Text("Object Definition");
				PropertyEditor::HelpMarker("Name of the Object Definition this object is using");

				PropertyEditor::PropertyValue();

				ImGui::Text(object.GetSceneObjectDefinitionName().GetString());

				PropertyEditor::PropertyLabel();

				ImGui::Text("Name");
				PropertyEditor::HelpMarker("Name of this object instance. Can be used to identify the object in the editor and game code");

				PropertyEditor::PropertyValue();

				{
					char buffer[512];

					strncpy_s(buffer, object.GetName(), sizeof(buffer));
					buffer[sizeof(buffer) - 1] = '\0';

					ImGui::InputText("##Name", buffer, IM_ARRAYSIZE(buffer));

					if (ImGui::IsItemDeactivatedAfterEdit())
					{
						std::shared_ptr<ChangeSceneObjectNameCommand> command = std::make_shared<ChangeSceneObjectNameCommand>(id, buffer, object.GetName());
						CommandManager::DoCommand(command);
					}
				}

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


				PropertyEditor::PropertyLabel();

				ImGui::Text("Transform");

				PropertyEditor::PropertyValue();

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
				}

				std::vector<SceneObject::PropertySourceAndOveride> allProperties;
				object.CalculateEditablePropertySet(Editor::GetEditor()->GetSceneObjectDefinitionManager(), allProperties);

				for (const SceneObject::PropertySourceAndOveride& propertySourceAndOverride : allProperties)
				{
					ImGui::PushID(propertySourceAndOverride.source.name.GetString());

					// todo: probably show groups here also
					SceneProperty newProperty = propertySourceAndOverride.source;
					if (propertySourceAndOverride.override.name == propertySourceAndOverride.source.name)
					{
						newProperty.value = propertySourceAndOverride.override.value;
					}

					// todo: add a way to clear the override
					// and display if the value is overriden or not somehow

					if (newProperty.value.ShowImGuiEditor(newProperty.name.GetString(), propertySourceAndOverride.source.description.GetString()))
					{
						std::shared_ptr<ChangePropertyOverridesCommand> command = std::make_shared<ChangePropertyOverridesCommand>(id, newProperty, propertySourceAndOverride.override);
						CommandManager::DoCommand(command);
					}

					ImGui::PopID();
				}

				PropertyEditor::EndPropertyTable();
				
				// Perforce file status
				{
					if (P4::QueryHasFileInfo(object.GetPath().GetString()))
					{
						P4::FileInfo fileinfo = P4::GetFileInfo(object.GetPath().GetString());
						ImVec4 color = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
						ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

						ImGui::Separator();
						if (ImGui::TreeNodeEx("P4 File Info", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth))
						{
							if (fileinfo.action == P4::FileAction::Add)
							{
								ImGui::Text("revision ");							ImGui::SameLine();
								ImGui::TextColored(color, "#%d", fileinfo.revision);
							}

							ImGui::Text("marked for ");								ImGui::SameLine();
							ImGui::TextColored(color, "%s", fileinfo.action);		ImGui::SameLine();
							ImGui::Text(" by ");									ImGui::SameLine();
							ImGui::TextColored(color, "%s", strcmp(P4::MyUser(), fileinfo.user) == 0 ? "you" : fileinfo.user);

							ImGui::Text("changelist ");								ImGui::SameLine();
							ImGui::TextColored(color, "%s", fileinfo.changelist);

							ImGui::Text("workspace ");								ImGui::SameLine();
							ImGui::TextColored(color, "%s", fileinfo.client);

							ImGui::TreePop();
						}
						else if (ImGui::IsItemHovered())
						{
							ImGui::BeginTooltip();
							{
								ImGui::PushTextWrapPos(ImGui::GetFontSize() * 20);
								ImGui::TextWrapped(
									"marked for %s by %s in changelist %s workspace %s",
									fileinfo.action, fileinfo.user, fileinfo.changelist, fileinfo.client
								);
								ImGui::PopTextWrapPos();
							}
							ImGui::EndTooltip();
						}
						ImGui::PopStyleVar();

					}
				}
			}

			ImGui::PopID();

		}
	}
}

