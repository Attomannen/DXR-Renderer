#include <stdafx.h>
#include "ScriptArrays.h"

#include <age/script/JsonData.h>
#include <imgui/imgui.h>

#include <nlohmann/json.hpp>

namespace Ag
{
	namespace
	{
		// One element type: how it is saved and how it is edited in a list.
		template <typename T> struct Element;

		template <> struct Element<int>
		{
			static int FromJson(const nlohmann::json& j) { return j.is_number() ? j.get<int>() : 0; }
			static nlohmann::json ToJson(int v) { return v; }
			static bool Edit(const char* id, int& v) { return ImGui::DragInt(id, &v, 0.2f); }
			static const char* Name() { return "Int"; }
		};

		template <> struct Element<float>
		{
			static float FromJson(const nlohmann::json& j) { return j.is_number() ? j.get<float>() : 0.f; }
			static nlohmann::json ToJson(float v) { return v; }
			static bool Edit(const char* id, float& v) { return ImGui::DragFloat(id, &v, 0.05f); }
			static const char* Name() { return "Float"; }
		};

		template <> struct Element<bool>
		{
			static unsigned char FromJson(const nlohmann::json& j) { return j.is_boolean() && j.get<bool>() ? 1 : 0; }
			static nlohmann::json ToJson(unsigned char v) { return v != 0; }
			static bool Edit(const char* id, unsigned char& v)
			{
				bool value = v != 0;
				const bool changed = ImGui::Checkbox(id, &value);
				v = value ? 1 : 0;
				return changed;
			}
			static const char* Name() { return "Bool"; }
		};

		template <> struct Element<StringId>
		{
			static StringId FromJson(const nlohmann::json& j) { return StringRegistry::RegisterOrGetString(j.is_string() ? j.get<std::string>() : std::string()); }
			static nlohmann::json ToJson(const StringId& v) { return std::string(v.GetString()); }
			static bool Edit(const char* id, StringId& v)
			{
				char buffer[128];
				strncpy_s(buffer, v.GetString(), _TRUNCATE);
				if (!ImGui::InputText(id, buffer, IM_ARRAYSIZE(buffer)))
					return false;
				v = StringRegistry::RegisterOrGetString(buffer);
				return true;
			}
			static const char* Name() { return "String"; }
		};

		template <> struct Element<Vector3f>
		{
			static Vector3f FromJson(const nlohmann::json& j)
			{
				if (j.is_array() && j.size() >= 3) return Vector3f(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
				return Vector3f(0.f, 0.f, 0.f);
			}
			static nlohmann::json ToJson(const Vector3f& v) { return nlohmann::json::array({ v.x, v.y, v.z }); }
			static bool Edit(const char* id, Vector3f& v) { return ImGui::DragFloat3(id, &v.x, 0.05f); }
			static const char* Name() { return "Float3"; }
		};

		template <typename T>
		void LoadArray(ArrayValue<T>& value, const JsonData& data)
		{
			value = ArrayValue<T>::Create();
			auto& items = value.Edit().items;
			if (data.json.is_array())
				for (const nlohmann::json& element : data.json)
					items.push_back(Element<T>::FromJson(element));
		}

		template <typename T>
		void WriteArray(const ArrayValue<T>& value, JsonData& data)
		{
			data.json = nlohmann::json::array();
			for (const auto& item : value.Get().items)
				data.json.push_back(Element<T>::ToJson(item));
		}

		template <typename T>
		bool ShowArray(ArrayValue<T>& value, const char* name)
		{
			std::vector<typename ArrayStorage<T>::Type> items = value.Get().items;
			if (name == nullptr)
			{
				ImGui::Text("%zu %s items", items.size(), Element<T>::Name());
				return false;
			}

			bool changed = false;
			char header[96];
			snprintf(header, sizeof(header), "%s (%zu)###array", name, items.size());
			if (ImGui::TreeNode(header))
			{
				int removeIndex = -1;
				for (int i = 0; i < (int)items.size(); ++i)
				{
					ImGui::PushID(i);
					ImGui::TextDisabled("%d", i);
					ImGui::SameLine();
					ImGui::SetNextItemWidth(-30.f);
					changed |= Element<T>::Edit("##item", items[i]);
					ImGui::SameLine();
					if (ImGui::SmallButton("x"))
						removeIndex = i;
					ImGui::PopID();
				}
				if (removeIndex >= 0)
				{
					items.erase(items.begin() + removeIndex);
					changed = true;
				}
				if (ImGui::SmallButton("+ Add"))
				{
					items.push_back(typename ArrayStorage<T>::Type{});
					changed = true;
				}
				ImGui::TreePop();
			}
			if (changed)
				value.Edit().items = std::move(items);
			return changed;
		}

		template <typename T>
		void LoadMap(MapValue<T>& value, const JsonData& data)
		{
			value = MapValue<T>::Create();
			auto& entries = value.Edit().entries;
			if (data.json.is_object())
				for (auto it = data.json.begin(); it != data.json.end(); ++it)
					entries.emplace_back(StringRegistry::RegisterOrGetString(it.key()), Element<T>::FromJson(it.value()));
		}

		template <typename T>
		void WriteMap(const MapValue<T>& value, JsonData& data)
		{
			data.json = nlohmann::json::object();
			for (const auto& entry : value.Get().entries)
				data.json[entry.first.GetString()] = Element<T>::ToJson(entry.second);
		}

		template <typename T>
		bool ShowMap(MapValue<T>& value, const char* name)
		{
			auto entries = value.Get().entries;
			if (name == nullptr)
			{
				ImGui::Text("%zu %s entries", entries.size(), Element<T>::Name());
				return false;
			}

			bool changed = false;
			char header[96];
			snprintf(header, sizeof(header), "%s (%zu)###map", name, entries.size());
			if (ImGui::TreeNode(header))
			{
				int removeIndex = -1;
				for (int i = 0; i < (int)entries.size(); ++i)
				{
					ImGui::PushID(i);
					ImGui::SetNextItemWidth(90.f);
					changed |= Element<StringId>::Edit("##key", entries[i].first);
					ImGui::SameLine();
					ImGui::SetNextItemWidth(-30.f);
					changed |= Element<T>::Edit("##value", entries[i].second);
					ImGui::SameLine();
					if (ImGui::SmallButton("x"))
						removeIndex = i;
					ImGui::PopID();
				}
				if (removeIndex >= 0)
				{
					entries.erase(entries.begin() + removeIndex);
					changed = true;
				}
				if (ImGui::SmallButton("+ Add"))
				{
					entries.emplace_back(StringId{}, typename ArrayStorage<T>::Type{});
					changed = true;
				}
				ImGui::TreePop();
			}
			if (changed)
				value.Edit().entries = std::move(entries);
			return changed;
		}
	}

#define DEFINE_SCRIPT_COLLECTION_TYPES(T, ARRAY_NAME, MAP_NAME) \
	template<> void LoadFromJson<ArrayValue<T>>(ArrayValue<T>& value, const JsonData& data) { LoadArray<T>(value, data); } \
	template<> void WriteToJson<ArrayValue<T>>(const ArrayValue<T>& value, JsonData& data) { WriteArray<T>(value, data); } \
	template<> bool ShowImGuiEditor<ArrayValue<T>>(ArrayValue<T>& value, const char* name, const char*) { return ShowArray<T>(value, name); } \
	IMPLEMENT_PROPERTY_TYPE(ArrayValue<T>, ARRAY_NAME) \
	template<> void LoadFromJson<MapValue<T>>(MapValue<T>& value, const JsonData& data) { LoadMap<T>(value, data); } \
	template<> void WriteToJson<MapValue<T>>(const MapValue<T>& value, JsonData& data) { WriteMap<T>(value, data); } \
	template<> bool ShowImGuiEditor<MapValue<T>>(MapValue<T>& value, const char* name, const char*) { return ShowMap<T>(value, name); } \
	IMPLEMENT_PROPERTY_TYPE(MapValue<T>, MAP_NAME)

	DEFINE_SCRIPT_COLLECTION_TYPES(int, "Int Array", "Int Map")
	DEFINE_SCRIPT_COLLECTION_TYPES(float, "Float Array", "Float Map")
	DEFINE_SCRIPT_COLLECTION_TYPES(bool, "Bool Array", "Bool Map")
	DEFINE_SCRIPT_COLLECTION_TYPES(StringId, "String Array", "String Map")
	DEFINE_SCRIPT_COLLECTION_TYPES(Vector3f, "Float3 Array", "Float3 Map")
}
