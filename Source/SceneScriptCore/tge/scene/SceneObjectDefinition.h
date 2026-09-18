#pragma once

#include <tge/stringRegistry/StringRegistry.h>
#include <tge/script/Property.h>

#include <memory>

namespace Tga
{
	class Script;

	enum class ScenePropertyFlags : unsigned char
	{
		None = 0,
		IsDynamic = 1, // Dynamic properties can be edited from a script. Dynamic properties also have to be per instance
		IsPerInstance = 2, // Per instance properties can be set per SceneObject, not only in object definitions
		All = IsDynamic | IsPerInstance,
	};

	inline ScenePropertyFlags operator|(ScenePropertyFlags lhs, ScenePropertyFlags rhs)
	{
		return static_cast<ScenePropertyFlags>(static_cast<unsigned char>(lhs) | static_cast<unsigned char>(rhs));
	}

	inline ScenePropertyFlags& operator|=(ScenePropertyFlags& lhs, ScenePropertyFlags rhs)
	{
		lhs = static_cast<ScenePropertyFlags>(static_cast<unsigned char>(lhs) | static_cast<unsigned char>(rhs));
		return lhs;
	}

	inline ScenePropertyFlags operator&(ScenePropertyFlags lhs, ScenePropertyFlags rhs)
	{
		return static_cast<ScenePropertyFlags>(static_cast<unsigned char>(lhs) & static_cast<unsigned char>(rhs));
	}

	inline ScenePropertyFlags operator~(ScenePropertyFlags value)
	{
		return static_cast<ScenePropertyFlags>(~static_cast<unsigned char>(value));
	}

	struct SceneProperty
	{
		StringId name;
		const PropertyTypeBase* type;
		Property value;
	};

	struct ScenePropertyDefinition : SceneProperty
	{
		ScenePropertyFlags flags;

		// todo: remove these in retail
		StringId groupName; // Used to organize properties in the scene inspector
		StringId description; // Used to show what a property does in the editor
	};

	class SceneObjectDefinition
	{
	public:
		void SetName(StringId name) { myName = name; }
		const StringId GetName() const { return myName; }

		void SetPath(const char* path) { myPath = path; }
		const char* GetPath() const { return myPath.c_str(); }

		void Save();
		void Load(const char* aPath);

		std::span<const ScenePropertyDefinition> GetProperties() const { return myProperties; };
		std::vector<ScenePropertyDefinition>& EditProperties() { return myProperties; };

		// The object's script. It lives inside the .tgo (saved as "event-graph"), the way a
		// Blueprint keeps its event graph, and cannot exist apart from it.
		Script& EditEventGraph();                       // creates an empty graph on first use
		const Script* GetEventGraph() const { return myEventGraph.get(); }
		bool HasEventGraph() const;                     // has at least one node
		// A frozen copy to run while the graph keeps being edited. Rebuilt when the graph changes.
		std::shared_ptr<const Script> GetEventGraphSnapshot();

	private:
		std::string myPath;
		StringId myName;

		StringId myParent;
		std::vector<ScenePropertyDefinition> myProperties;

		std::shared_ptr<Script> myEventGraph;
		std::shared_ptr<const Script> myEventGraphSnapshot;
		int myEventGraphSnapshotSequence = -1;
	};
}