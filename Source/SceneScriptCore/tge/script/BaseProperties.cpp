#include <stdafx.h>

#include "BaseProperties.h"
#include <tge/script/JsonData.h>
#include <tge/imgui/ImGuiPropertyEditor.h>
#include <imgui/imgui.h>

namespace Tga
{
	// Workaround to ensure this object file is included
	// called from Engine.cpp
	void EnsureBasePropertiesAreLoaded() {}

	template<>
	void LoadFromJson<bool>(bool& value, const JsonData& json)
	{
		value = json.json.get<bool>();
	}

	template<>
	void WriteToJson<bool>(const bool& value, JsonData& json)
	{
		json.json = value;
	}

	template<>
	bool ShowImGuiEditor<bool>(bool& value, const char* name, const char* description)
	{
		bool initialValue = value;
		
		if (name)
		{
			PropertyEditor::PropertyLabel();
			ImGui::Text(name);
			if (description && *description != 0)
			{
				PropertyEditor::HelpMarker(description);
			}
			PropertyEditor::PropertyValue();
			ImGui::PushID(name);
		}

		bool returnValue = ImGui::Checkbox("##", &value) && value != initialValue;

		if (name)
		{
			ImGui::PopID();
		}

		return returnValue;
	}

	IMPLEMENT_PROPERTY_TYPE(bool, "Bool")

	template<>
	void LoadFromJson<int>(int& value, const JsonData& json)
	{
		value = json.json.get<int>();
	}

	template<>
	void WriteToJson<int>(const int& value, JsonData& json)
	{
		json.json = value;
	}

	template<>
	bool ShowImGuiEditor<int>(int& value, const char* name, const char* description)
	{
		int initialValue = value;

		if (name)
		{
			PropertyEditor::PropertyLabel();
			ImGui::Text(name);
			if (description && *description != 0)
			{
				PropertyEditor::HelpMarker(description);
			}
			PropertyEditor::PropertyValue();
			ImGui::PushID(name);
		}

		bool returnValue = ImGui::DragInt("##", &value) && value != initialValue;

		if (name)
		{
			ImGui::PopID();
		}

		return returnValue;
	}

	IMPLEMENT_PROPERTY_TYPE(int, "Int")

	template<>
	void LoadFromJson<float>(float& value, const JsonData& json)
	{
		value = json.json.get<float>();
	}

	template<>
	void WriteToJson<float>(const float& value, JsonData& json)
	{
		json.json = value;
	}

	template<>
	bool ShowImGuiEditor<float>(float& value, const char* name, const char* description)
	{
		float initialValue = value;

		if (name)
		{
			PropertyEditor::PropertyLabel();
			ImGui::Text(name);
			if (description && *description != 0)
			{
				PropertyEditor::HelpMarker(description);
			}
			PropertyEditor::PropertyValue();
			ImGui::PushID(name);
		}

		bool returnValue = ImGui::DragFloat("##", &value) && value != initialValue;

		if (name)
		{
			ImGui::PopID();
		}

		return returnValue;
	}

	IMPLEMENT_PROPERTY_TYPE(float, "Float")

	template<>
	void LoadFromJson<Vector2f>(Vector2f& value, const JsonData& json)
	{
		value.x = json.json[0].get<float>();
		value.y = json.json[1].get<float>();
	}

	template<>
	void WriteToJson<Vector2f>(const Vector2f& value, JsonData& json)
	{
		json.json = nlohmann::json{
			value.x,
			value.y,
		};
	}

	template<>
	bool ShowImGuiEditor<Vector2f>(Vector2f& value, const char* name, const char* description)
	{
		Vector2f initialValue = value;

		if (name)
		{
			PropertyEditor::PropertyLabel();
			ImGui::Text(name);
			if (description && *description != 0)
			{
				PropertyEditor::HelpMarker(description);
			}
			PropertyEditor::PropertyValue();
			ImGui::PushID(name);
		}

		bool returnValue = (ImGui::DragFloat2("##", &value.x) && value != initialValue);

		if (name)
		{
			ImGui::PopID();
		}

		return returnValue;
	}

	IMPLEMENT_PROPERTY_TYPE(Vector2f, "Float2")

	template<>
	void LoadFromJson<Vector3f>(Vector3f& value, const JsonData& json)
	{
		value.x = json.json[0].get<float>();
		value.y = json.json[1].get<float>();
		value.z = json.json[2].get<float>();
	}

	template<>
	void WriteToJson<Vector3f>(const Vector3f& value, JsonData& json)
	{
		json.json = nlohmann::json{
			value.x,
			value.y,
			value.z,
		};
	}

	template<>
	bool ShowImGuiEditor<Vector3f>(Vector3f& value, const char* name, const char* description)
	{
		Vector3f initialValue = value;

		if (name)
		{
			PropertyEditor::PropertyLabel();
			ImGui::Text(name);
			if (description && *description != 0)
			{
				PropertyEditor::HelpMarker(description);
			}
			PropertyEditor::PropertyValue();
			ImGui::PushID(name);
		}

		bool returnValue = (ImGui::DragFloat3("##", &value.x) && value != initialValue);

		if (name)
		{
			ImGui::PopID();
		}

		return returnValue;
	}

	IMPLEMENT_PROPERTY_TYPE(Vector3f, "Float3")

	template<>
	void LoadFromJson<Vector4f>(Vector4f& value, const JsonData& json)
	{
		value.x = json.json[0].get<float>();
		value.y = json.json[1].get<float>();
		value.z = json.json[2].get<float>();
		value.w = json.json[3].get<float>();
	}

	template<>
	void WriteToJson<Vector4f>(const Vector4f& value, JsonData& json)
	{
		json.json = nlohmann::json{
			value.x,
			value.y,
			value.z,
			value.w,
		};
	}

	template<>
	bool ShowImGuiEditor<Vector4f>(Vector4f& value, const char* name, const char* description)
	{
		Vector4f initialValue = value;

		if (name)
		{
			PropertyEditor::PropertyLabel();
			ImGui::Text(name);
			if (description && *description != 0)
			{
				PropertyEditor::HelpMarker(description);
			}
			PropertyEditor::PropertyValue();
			ImGui::PushID(name);
		}

		bool returnValue = (ImGui::DragFloat4("##", &value.x) && value != initialValue);

		if (name)
		{
			ImGui::PopID();
		}

		return returnValue;
	}

	IMPLEMENT_PROPERTY_TYPE(Vector4f, "Float4")

	template<>
	void LoadFromJson<Color>(Color& value, const JsonData& json)
	{
		value.r = json.json[0].get<float>();
		value.g = json.json[1].get<float>();
		value.b = json.json[2].get<float>();
		value.a = json.json[3].get<float>();
	}

	template<>
	void WriteToJson<Color>(const Color& value, JsonData& json)
	{
		json.json = nlohmann::json{
			value.r,
			value.g,
			value.b,
			value.a,
		};
	}

	template<>
	bool ShowImGuiEditor<Color>(Color& value, const char* name, const char* description)
	{
		Color initialValue = value;

		if (name)
		{
			PropertyEditor::PropertyLabel();
			ImGui::Text(name);
			if (description && *description != 0)
			{
				PropertyEditor::HelpMarker(description);
			}
			PropertyEditor::PropertyValue();
			ImGui::PushID(name);
		}

		bool returnValue = (ImGui::ColorEdit4("##", &value.r, ImGuiColorEditFlags_Float) && value != initialValue);

		if (name)
		{
			ImGui::PopID();
		}

		return returnValue;
	}

	IMPLEMENT_PROPERTY_TYPE(Color, "Color")

	template<>
	void LoadFromJson<StringId>(StringId& value, const JsonData& json)
	{
		std::string_view string = json.json.get<std::string_view>();
		value = StringRegistry::RegisterOrGetString(string);
	}

	template<>
	void WriteToJson<StringId>(const StringId& value, JsonData& json)
	{
		json.json = value.GetString();
	}

	template<>
	bool ShowImGuiEditor<StringId>(StringId& value, const char* name, const char* description)
	{
		if (name)
		{
			PropertyEditor::PropertyLabel();
			ImGui::Text(name);
			if (description && *description != 0)
			{
				PropertyEditor::HelpMarker(description);
			}
			PropertyEditor::PropertyValue();
			ImGui::PushID(name);
		}

		static char stringBuffer[256];

		strncpy_s(stringBuffer, value.GetString(), sizeof(stringBuffer));
		stringBuffer[sizeof(stringBuffer) - 1] = '\0';

		bool returnValue = false;

		bool didInputChange = ImGui::InputText("##", stringBuffer, IM_ARRAYSIZE(stringBuffer));
		if(didInputChange)
		{
			StringId newValue = StringRegistry::RegisterOrGetString(stringBuffer);

			if (newValue != value)
			{
				value = newValue;
				returnValue = true;
			}
		}

		if (name)
		{
			ImGui::PopID();
		}

		return returnValue;
	}

	IMPLEMENT_PROPERTY_TYPE(StringId, "StringId")
}