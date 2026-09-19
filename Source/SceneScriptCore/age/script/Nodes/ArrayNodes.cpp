#include <stdafx.h>
#include "ArrayNodes.h"

#include "NodeHelpers.h"
#include <age/script/ScriptArrays.h>
#include <age/log/Log.h>

#include <algorithm>
#include <cstring>
#include <random>
#include <string>
#include <type_traits>

using namespace Ag;
using namespace Ag::NodeHelpers;

namespace
{
	template <typename T> using Stored = typename ArrayStorage<T>::Type;

	std::mt19937& Rng()
	{
		static std::mt19937 generator{ std::random_device{}() };
		return generator;
	}

	template <typename T>
	ArrayValue<T> ReadArray(ScriptExecutionContext& context, ScriptPinId pin)
	{
		const Property property = context.ReadInputPin(pin);
		const ArrayValue<T>* array = property.Get<ArrayValue<T>>();
		return array ? *array : ArrayValue<T>();
	}

	template <typename T>
	MapValue<T> ReadMap(ScriptExecutionContext& context, ScriptPinId pin)
	{
		const Property property = context.ReadInputPin(pin);
		const MapValue<T>* map = property.Get<MapValue<T>>();
		return map ? *map : MapValue<T>();
	}

	// The array or map variable with this name (created empty on first use), or null when the name holds another type.
	template <typename P>
	P* GetVariable(ScriptExecutionContext& context, StringId name)
	{
		auto* properties = context.GetUpdateContext().dynamicProperties;
		if (!properties)
			return nullptr;
		Property& property = (*properties)[name];
		if (!property.HasValue())
			property = Property::Create<P>();
		P* value = property.Get<P>();
		if (!value)
			ERROR_PRINT("Variable '%s' is not a %s", name.GetString(), GetPropertyType<P>()->GetName().GetString());
		return value;
	}

	template <typename T>
	int FindIndex(const std::vector<Stored<T>>& items, const T& item)
	{
		for (int i = 0; i < (int)items.size(); ++i)
			if (ElementEqual<T>(FromStored<T>(items[i]), item))
				return i;
		return -1;
	}

	// Sorts the numbers and strings; Vector3f and bool have no natural order, so those arrays are left as they are.
	template <typename T>
	void SortItems(std::vector<Stored<T>>& items)
	{
		if constexpr (std::is_arithmetic_v<Stored<T>> || std::is_same_v<T, StringId>)
			std::sort(items.begin(), items.end());
	}

	// ---------------------------------------------------------------- pure array nodes

	template <typename T>
	class MakeArrayNode : public ScriptNodeBase
	{
		static constexpr int kItems = 4;
		ScriptPinId myItems[kItems];
	public:
		void Init(const ScriptCreationContext& context) override
		{
			static const char* names[kItems] = { "Item 0", "Item 1", "Item 2", "Item 3" };
			for (int i = 0; i < kItems; ++i) myItems[i] = In<T>(context, names[i]);
			Out<ArrayValue<T>>(context, "Array");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			ArrayValue<T> array = ArrayValue<T>::Create();
			for (const ScriptPinId item : myItems) array.Edit().items.push_back(ToStored<T>(Read<T>(context, item)));
			return Make<ArrayValue<T>>(array);
		}
	};

	template <typename T>
	class EmptyArrayNode : public ScriptNodeBase
	{
	public:
		void Init(const ScriptCreationContext& context) override { Out<ArrayValue<T>>(context, "Array"); }
		Property ReadPin(ScriptExecutionContext&, ScriptPinId) const override { return Make<ArrayValue<T>>(ArrayValue<T>::Create()); }
	};

	// Array -> int / bool
	template <typename T, typename R, R (*Fn)(const std::vector<Stored<T>>&)>
	class ArrayQueryNode : public ScriptNodeBase
	{
		ScriptPinId myArray;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myArray = In<ArrayValue<T>>(context, "Array");
			Out<R>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			return Make<R>(Fn(ReadArray<T>(context, myArray).Get().items));
		}
	};

	template <typename T> int LengthOf(const std::vector<Stored<T>>& items) { return (int)items.size(); }
	template <typename T> bool IsEmptyOf(const std::vector<Stored<T>>& items) { return items.empty(); }

	template <typename T>
	class GetNode : public ScriptNodeBase
	{
		ScriptPinId myArray, myIndex;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myArray = In<ArrayValue<T>>(context, "Array");
			myIndex = In<int>(context, "Index", 0);
			Out<T>(context, "Item");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const ArrayValue<T> itemsArray = ReadArray<T>(context, myArray);
			const auto& items = itemsArray.Get().items;
			const int index = Read<int>(context, myIndex);
			return Make<T>(index >= 0 && index < (int)items.size() ? FromStored<T>(items[index]) : T{});
		}
	};

	template <typename T>
	class GetLastNode : public ScriptNodeBase
	{
		ScriptPinId myArray;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myArray = In<ArrayValue<T>>(context, "Array");
			Out<T>(context, "Item");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const ArrayValue<T> itemsArray = ReadArray<T>(context, myArray);
			const auto& items = itemsArray.Get().items;
			return Make<T>(items.empty() ? T{} : FromStored<T>(items.back()));
		}
	};

	template <typename T>
	class RandomItemNode : public ScriptNodeBase
	{
		ScriptPinId myArray;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myArray = In<ArrayValue<T>>(context, "Array");
			Out<T>(context, "Item");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const ArrayValue<T> itemsArray = ReadArray<T>(context, myArray);
			const auto& items = itemsArray.Get().items;
			if (items.empty()) return Make<T>(T{});
			return Make<T>(FromStored<T>(items[std::uniform_int_distribution<size_t>(0, items.size() - 1)(Rng())]));
		}
	};

	template <typename T>
	class ContainsNode : public ScriptNodeBase
	{
		ScriptPinId myArray, myItem;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myArray = In<ArrayValue<T>>(context, "Array");
			myItem = In<T>(context, "Item");
			Out<bool>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			return Make<bool>(FindIndex<T>(ReadArray<T>(context, myArray).Get().items, Read<T>(context, myItem)) >= 0);
		}
	};

	template <typename T>
	class FindNode : public ScriptNodeBase
	{
		ScriptPinId myArray, myItem;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myArray = In<ArrayValue<T>>(context, "Array");
			myItem = In<T>(context, "Item");
			Out<int>(context, "Index");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			return Make<int>(FindIndex<T>(ReadArray<T>(context, myArray).Get().items, Read<T>(context, myItem)));
		}
	};

	template <typename T>
	class IsValidIndexNode : public ScriptNodeBase
	{
		ScriptPinId myArray, myIndex;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myArray = In<ArrayValue<T>>(context, "Array");
			myIndex = In<int>(context, "Index", 0);
			Out<bool>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const int index = Read<int>(context, myIndex);
			return Make<bool>(index >= 0 && index < (int)ReadArray<T>(context, myArray).Get().items.size());
		}
	};

	// Copies of an array with one change.
	enum class ArrayEdit { Add, Set, RemoveIndex, Insert };

	template <typename T, ArrayEdit Kind>
	class ArrayEditNode : public ScriptNodeBase
	{
		ScriptPinId myArray, myIndex, myItem;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myArray = In<ArrayValue<T>>(context, "Array");
			if constexpr (Kind != ArrayEdit::Add) myIndex = In<int>(context, "Index", 0);
			if constexpr (Kind != ArrayEdit::RemoveIndex) myItem = In<T>(context, "Item");
			Out<ArrayValue<T>>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			ArrayValue<T> array = ReadArray<T>(context, myArray);
			const int count = (int)array.Get().items.size();
			if constexpr (Kind == ArrayEdit::Add)
			{
				array.Edit().items.push_back(ToStored<T>(Read<T>(context, myItem)));
			}
			else
			{
				const int index = Read<int>(context, myIndex);
				if constexpr (Kind == ArrayEdit::Set)
				{
					if (index >= 0 && index < count)
						array.Edit().items[index] = ToStored<T>(Read<T>(context, myItem));
				}
				else if constexpr (Kind == ArrayEdit::RemoveIndex)
				{
					if (index >= 0 && index < count)
						array.Edit().items.erase(array.Edit().items.begin() + index);
				}
				else
				{
					if (index >= 0 && index <= count)
						array.Edit().items.insert(array.Edit().items.begin() + index, ToStored<T>(Read<T>(context, myItem)));
				}
			}
			return Make<ArrayValue<T>>(array);
		}
	};

	template <typename T>
	class AppendArrayNode : public ScriptNodeBase
	{
		ScriptPinId myA, myB;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myA = In<ArrayValue<T>>(context, "A");
			myB = In<ArrayValue<T>>(context, "B");
			Out<ArrayValue<T>>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			ArrayValue<T> result = ReadArray<T>(context, myA);
			const ArrayValue<T> moreArray = ReadArray<T>(context, myB);
			const auto& more = moreArray.Get().items;
			auto& items = result.Edit().items;
			items.insert(items.end(), more.begin(), more.end());
			return Make<ArrayValue<T>>(result);
		}
	};

	template <typename T>
	class ReverseArrayNode : public ScriptNodeBase
	{
		ScriptPinId myArray;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myArray = In<ArrayValue<T>>(context, "Array");
			Out<ArrayValue<T>>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			ArrayValue<T> result = ReadArray<T>(context, myArray);
			auto& items = result.Edit().items;
			std::reverse(items.begin(), items.end());
			return Make<ArrayValue<T>>(result);
		}
	};

	// Sum, Min, Max and Average of a number array.
	enum class Reduction { Sum, Min, Max, Average };

	template <typename T, Reduction Kind>
	class ReduceNode : public ScriptNodeBase
	{
		ScriptPinId myArray;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myArray = In<ArrayValue<T>>(context, "Array");
			Out<T>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const ArrayValue<T> itemsArray = ReadArray<T>(context, myArray);
			const auto& items = itemsArray.Get().items;
			if (items.empty()) return Make<T>(T{});
			T result = items[0];
			for (size_t i = 1; i < items.size(); ++i)
			{
				if constexpr (Kind == Reduction::Min) result = std::min<T>(result, items[i]);
				else if constexpr (Kind == Reduction::Max) result = std::max<T>(result, items[i]);
				else result += items[i];
			}
			if constexpr (Kind == Reduction::Average) result = (T)(result / (T)items.size());
			return Make<T>(result);
		}
	};

	// ---------------------------------------------------------------- loops

	template <typename T>
	struct ForEachData
	{
		int index = 0;
		bool broken = false;
		T element = T{};
	};

	template <typename T>
	class ForEachNode : public ScriptNodeWithRuntimeData<ForEachData<T>>
	{
		ScriptPinId myBreak, myArray, myBody, myCompleted, myElement, myIndex;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			FlowIn(context, "In");
			myBreak = FlowIn(context, "Break");
			myArray = In<ArrayValue<T>>(context, "Array");
			myBody = FlowOut(context, "Loop Body");
			myElement = Out<T>(context, "Array Element");
			myIndex = Out<int>(context, "Array Index");
			myCompleted = FlowOut(context, "Completed");
		}

		Property ReadPin(ScriptExecutionContext& context, ScriptPinId pin) const override
		{
			const ForEachData<T>& data = ScriptNodeWithRuntimeData<ForEachData<T>>::GetRuntimeData(context);
			if (pin == myIndex) return Make<int>(data.index);
			return Make<T>(data.element);
		}

		ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId pin) const override
		{
			ForEachData<T>& data = ScriptNodeWithRuntimeData<ForEachData<T>>::GetRuntimeData(context);
			if (pin == myBreak)
			{
				data.broken = true;
				return ScriptNodeResult::Finished;
			}

			// Iterate a copy: the body may change the variable this array came from.
			const ArrayValue<T> array = ReadArray<T>(context, myArray);
			const int count = (int)array.Get().items.size();
			data.broken = false;
			for (int i = 0; i < count && !data.broken; ++i)
			{
				data.index = i;
				data.element = FromStored<T>(array.Get().items[i]);
				context.RunOutputPin(myBody);
			}
			context.TriggerOutputPin(myCompleted);
			return ScriptNodeResult::Finished;
		}
	};

	// ---------------------------------------------------------------- in-place nodes, on a variable by name

	enum class VarOp { Add, AddUnique, Insert, RemoveAt, RemoveItem, Clear, SetAt, Shuffle, Sort, Reverse };

	template <typename T, VarOp Op>
	class ArrayVarNode : public ScriptNodeBase
	{
		ScriptPinId myName, myIndex, myItem, myOut;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			FlowIn(context, "Run");
			myName = In<StringId>(context, "Array Name");
			if constexpr (Op == VarOp::Insert || Op == VarOp::RemoveAt || Op == VarOp::SetAt) myIndex = In<int>(context, "Index", 0);
			if constexpr (Op == VarOp::Add || Op == VarOp::AddUnique || Op == VarOp::Insert || Op == VarOp::RemoveItem || Op == VarOp::SetAt) myItem = In<T>(context, "Item");
			myOut = FlowOut(context, "");
		}

		ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId) const override
		{
			ArrayValue<T>* array = GetVariable<ArrayValue<T>>(context, Read<StringId>(context, myName));
			if (array)
			{
				const int count = (int)array->Get().items.size();
				switch (Op)
				{
				case VarOp::Add:
					array->Edit().items.push_back(ToStored<T>(Read<T>(context, myItem)));
					break;
				case VarOp::AddUnique:
				{
					const T item = Read<T>(context, myItem);
					if (FindIndex<T>(array->Get().items, item) < 0)
						array->Edit().items.push_back(ToStored<T>(item));
					break;
				}
				case VarOp::Insert:
				{
					const int index = Read<int>(context, myIndex);
					if (index >= 0 && index <= count)
						array->Edit().items.insert(array->Edit().items.begin() + index, ToStored<T>(Read<T>(context, myItem)));
					break;
				}
				case VarOp::RemoveAt:
				{
					const int index = Read<int>(context, myIndex);
					if (index >= 0 && index < count)
						array->Edit().items.erase(array->Edit().items.begin() + index);
					break;
				}
				case VarOp::RemoveItem:
				{
					const int index = FindIndex<T>(array->Get().items, Read<T>(context, myItem));
					if (index >= 0)
						array->Edit().items.erase(array->Edit().items.begin() + index);
					break;
				}
				case VarOp::Clear:
					array->Edit().items.clear();
					break;
				case VarOp::SetAt:
				{
					const int index = Read<int>(context, myIndex);
					if (index >= 0 && index < count)
						array->Edit().items[index] = ToStored<T>(Read<T>(context, myItem));
					break;
				}
				case VarOp::Shuffle:
					std::shuffle(array->Edit().items.begin(), array->Edit().items.end(), Rng());
					break;
				case VarOp::Sort:
					SortItems<T>(array->Edit().items);
					break;
				case VarOp::Reverse:
					std::reverse(array->Edit().items.begin(), array->Edit().items.end());
					break;
				}
			}
			context.TriggerOutputPin(myOut);
			return ScriptNodeResult::Finished;
		}
	};

	// ---------------------------------------------------------------- maps

	template <typename T>
	int FindKey(const MapValue<T>& map, StringId key)
	{
		const auto& entries = map.Get().entries;
		for (int i = 0; i < (int)entries.size(); ++i)
			if (entries[i].first == key)
				return i;
		return -1;
	}

	template <typename T>
	void SetEntry(MapValue<T>& map, StringId key, const Stored<T>& value)
	{
		const int index = FindKey<T>(map, key);
		if (index >= 0) map.Edit().entries[index].second = value;
		else map.Edit().entries.emplace_back(key, value);
	}

	template <typename T>
	void RemoveEntry(MapValue<T>& map, StringId key)
	{
		const int index = FindKey<T>(map, key);
		if (index >= 0) map.Edit().entries.erase(map.Edit().entries.begin() + index);
	}

	template <typename T>
	class MapGetNode : public ScriptNodeBase
	{
		ScriptPinId myMap, myKey, myDefault, myFound;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myMap = In<MapValue<T>>(context, "Map");
			myKey = In<StringId>(context, "Key");
			myDefault = In<T>(context, "Default");
			Out<T>(context, "Value");
			myFound = Out<bool>(context, "Found");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId pin) const override
		{
			const MapValue<T> map = ReadMap<T>(context, myMap);
			const int index = FindKey<T>(map, Read<StringId>(context, myKey));
			if (pin == myFound) return Make<bool>(index >= 0);
			return Make<T>(index >= 0 ? FromStored<T>(map.Get().entries[index].second) : Read<T>(context, myDefault));
		}
	};

	template <typename T>
	class MapContainsNode : public ScriptNodeBase
	{
		ScriptPinId myMap, myKey;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myMap = In<MapValue<T>>(context, "Map");
			myKey = In<StringId>(context, "Key");
			Out<bool>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			return Make<bool>(FindKey<T>(ReadMap<T>(context, myMap), Read<StringId>(context, myKey)) >= 0);
		}
	};

	template <typename T>
	class MapLengthNode : public ScriptNodeBase
	{
		ScriptPinId myMap;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myMap = In<MapValue<T>>(context, "Map");
			Out<int>(context, "Length");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			return Make<int>((int)ReadMap<T>(context, myMap).Get().entries.size());
		}
	};

	template <typename T>
	class MapKeysNode : public ScriptNodeBase
	{
		ScriptPinId myMap;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myMap = In<MapValue<T>>(context, "Map");
			Out<ArrayValue<StringId>>(context, "Keys");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			ArrayValue<StringId> keys = ArrayValue<StringId>::Create();
			{ const MapValue<T> source = ReadMap<T>(context, myMap); for (const auto& entry : source.Get().entries) keys.Edit().items.push_back(entry.first); }
			return Make<ArrayValue<StringId>>(keys);
		}
	};

	template <typename T>
	class MapValuesNode : public ScriptNodeBase
	{
		ScriptPinId myMap;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myMap = In<MapValue<T>>(context, "Map");
			Out<ArrayValue<T>>(context, "Values");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			ArrayValue<T> values = ArrayValue<T>::Create();
			{ const MapValue<T> source = ReadMap<T>(context, myMap); for (const auto& entry : source.Get().entries) values.Edit().items.push_back(entry.second); }
			return Make<ArrayValue<T>>(values);
		}
	};

	template <typename T, bool Remove>
	class MapEditNode : public ScriptNodeBase
	{
		ScriptPinId myMap, myKey, myValue;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myMap = In<MapValue<T>>(context, "Map");
			myKey = In<StringId>(context, "Key");
			if constexpr (!Remove) myValue = In<T>(context, "Value");
			Out<MapValue<T>>(context, "Result");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			MapValue<T> map = ReadMap<T>(context, myMap);
			const StringId key = Read<StringId>(context, myKey);
			if constexpr (Remove) RemoveEntry<T>(map, key);
			else SetEntry<T>(map, key, ToStored<T>(Read<T>(context, myValue)));
			return Make<MapValue<T>>(map);
		}
	};

	enum class MapVarOp { Set, Remove, Clear };

	template <typename T, MapVarOp Op>
	class MapVarNode : public ScriptNodeBase
	{
		ScriptPinId myName, myKey, myValue, myOut;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			FlowIn(context, "Run");
			myName = In<StringId>(context, "Map Name");
			if constexpr (Op != MapVarOp::Clear) myKey = In<StringId>(context, "Key");
			if constexpr (Op == MapVarOp::Set) myValue = In<T>(context, "Value");
			myOut = FlowOut(context, "");
		}
		ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId) const override
		{
			if (MapValue<T>* map = GetVariable<MapValue<T>>(context, Read<StringId>(context, myName)))
			{
				if constexpr (Op == MapVarOp::Set) SetEntry<T>(*map, Read<StringId>(context, myKey), ToStored<T>(Read<T>(context, myValue)));
				else if constexpr (Op == MapVarOp::Remove) RemoveEntry<T>(*map, Read<StringId>(context, myKey));
				else map->Edit().entries.clear();
			}
			context.TriggerOutputPin(myOut);
			return ScriptNodeResult::Finished;
		}
	};

	// ---------------------------------------------------------------- reading and writing them as variables

	template <typename P>
	class ReadCollectionNode : public ScriptNodeBase
	{
		ScriptPinId myName;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			myName = In<StringId>(context, "Name");
			Out<P>(context, "Value");
		}
		Property ReadPin(ScriptExecutionContext& context, ScriptPinId) const override
		{
			const StringId name = Read<StringId>(context, myName);
			if (auto* dynamic = context.GetUpdateContext().dynamicProperties)
				if (auto it = dynamic->find(name); it != dynamic->end() && it->second.GetType() == GetPropertyType<P>())
					return it->second;
			if (auto* fixed = context.GetUpdateContext().staticProperties)
				if (auto it = fixed->find(name); it != fixed->end() && it->second.GetType() == GetPropertyType<P>())
					return it->second;
			return Make<P>(P());
		}
	};

	template <typename P>
	class WriteCollectionNode : public ScriptNodeBase
	{
		ScriptPinId myName, myValue, myOut;
	public:
		void Init(const ScriptCreationContext& context) override
		{
			FlowIn(context, "Run");
			myOut = FlowOut(context, "");
			myName = In<StringId>(context, "Name");
			myValue = In<P>(context, "Value");
		}
		ScriptNodeResult Execute(ScriptExecutionContext& context, ScriptPinId) const override
		{
			if (auto* dynamic = context.GetUpdateContext().dynamicProperties)
				(*dynamic)[Read<StringId>(context, myName)] = Make<P>(Read<P>(context, myValue));
			context.TriggerOutputPin(myOut);
			return ScriptNodeResult::Finished;
		}
	};

	// ---------------------------------------------------------------- registration

	template <typename T>
	void RegisterCollectionNodes(const char* typeName)
	{
		using R = ScriptNodeTypeRegistry;
		static std::vector<std::string> names;   // RegisterType keeps the pointers
		names.reserve(names.size() + 64);
		const auto full = [&](const char* group, const char* name)
		{
			names.push_back(std::string("Array/") + group + "/array " + (std::string(group) == "Variable" ? "variable " : "") + name + " (" + typeName + ")");
			return names.back().c_str();
		};
		const auto mapFull = [&](const char* group, const char* name)
		{
			names.push_back(std::string("Map/") + group + "/map " + (std::string(group) == "Variable" ? "variable " : "") + name + " (" + typeName + ")");
			return names.back().c_str();
		};

		R::RegisterType<MakeArrayNode<T>>(full("Make", "make array"), "Builds an array from up to four items");
		R::RegisterType<EmptyArrayNode<T>>(full("Make", "empty array"), "An array with nothing in it");
		R::RegisterType<ArrayQueryNode<T, int, LengthOf<T>>>(full("Read", "length"), "How many items the array has");
		R::RegisterType<ArrayQueryNode<T, bool, IsEmptyOf<T>>>(full("Read", "is empty"), "True when the array has no items");
		R::RegisterType<GetNode<T>>(full("Read", "get"), "The item at Index, counting from 0. A default value when Index is out of range");
		R::RegisterType<GetLastNode<T>>(full("Read", "get last"), "The last item");
		R::RegisterType<RandomItemNode<T>>(full("Read", "random item"), "A random item of the array");
		R::RegisterType<ContainsNode<T>>(full("Read", "contains"), "True when the array has Item");
		R::RegisterType<FindNode<T>>(full("Read", "find"), "The index of the first Item, or -1");
		R::RegisterType<IsValidIndexNode<T>>(full("Read", "is valid index"), "True when Index is inside the array");
		R::RegisterType<ArrayEditNode<T, ArrayEdit::Add>>(full("Copy", "add"), "A copy of the array with Item added at the end");
		R::RegisterType<ArrayEditNode<T, ArrayEdit::Insert>>(full("Copy", "insert"), "A copy of the array with Item inserted at Index");
		R::RegisterType<ArrayEditNode<T, ArrayEdit::Set>>(full("Copy", "set item"), "A copy of the array with the item at Index replaced");
		R::RegisterType<ArrayEditNode<T, ArrayEdit::RemoveIndex>>(full("Copy", "remove index"), "A copy of the array without the item at Index");
		R::RegisterType<AppendArrayNode<T>>(full("Copy", "append"), "A followed by B");
		R::RegisterType<ReverseArrayNode<T>>(full("Copy", "reverse"), "A copy of the array in the opposite order");
		R::RegisterType<ForEachNode<T>>(full("Loop", "for each"), "Runs Loop Body for every item of the array, then Completed. Break stops it early");

		R::RegisterType<ArrayVarNode<T, VarOp::Add>>(full("Variable", "add"), "Adds Item to the end of the array variable");
		R::RegisterType<ArrayVarNode<T, VarOp::AddUnique>>(full("Variable", "add unique"), "Adds Item unless the array variable already has it");
		R::RegisterType<ArrayVarNode<T, VarOp::Insert>>(full("Variable", "insert"), "Inserts Item into the array variable at Index");
		R::RegisterType<ArrayVarNode<T, VarOp::RemoveAt>>(full("Variable", "remove at"), "Removes the item at Index from the array variable");
		R::RegisterType<ArrayVarNode<T, VarOp::RemoveItem>>(full("Variable", "remove item"), "Removes the first Item from the array variable");
		R::RegisterType<ArrayVarNode<T, VarOp::Clear>>(full("Variable", "clear"), "Empties the array variable");
		R::RegisterType<ArrayVarNode<T, VarOp::SetAt>>(full("Variable", "set at"), "Replaces the item at Index in the array variable");
		R::RegisterType<ArrayVarNode<T, VarOp::Shuffle>>(full("Variable", "shuffle"), "Puts the array variable in a random order");
		R::RegisterType<ArrayVarNode<T, VarOp::Reverse>>(full("Variable", "reverse"), "Reverses the array variable");
		R::RegisterType<ReadCollectionNode<ArrayValue<T>>>(full("Variable", "read array"), "Reads an array variable");
		R::RegisterType<WriteCollectionNode<ArrayValue<T>>>(full("Variable", "write array"), "Writes an array variable");

		R::RegisterType<MapGetNode<T>>(mapFull("Read", "get"), "The value stored under Key, or Default when there is none");
		R::RegisterType<MapContainsNode<T>>(mapFull("Read", "contains key"), "True when the map has Key");
		R::RegisterType<MapLengthNode<T>>(mapFull("Read", "length"), "How many entries the map has");
		R::RegisterType<MapKeysNode<T>>(mapFull("Read", "keys"), "All the keys, in the order they were added");
		R::RegisterType<MapValuesNode<T>>(mapFull("Read", "values"), "All the values, in the order they were added");
		R::RegisterType<MapEditNode<T, false>>(mapFull("Copy", "set"), "A copy of the map with Key set to Value");
		R::RegisterType<MapEditNode<T, true>>(mapFull("Copy", "remove"), "A copy of the map without Key");
		R::RegisterType<MapVarNode<T, MapVarOp::Set>>(mapFull("Variable", "set"), "Sets Key to Value in the map variable");
		R::RegisterType<MapVarNode<T, MapVarOp::Remove>>(mapFull("Variable", "remove"), "Removes Key from the map variable");
		R::RegisterType<MapVarNode<T, MapVarOp::Clear>>(mapFull("Variable", "clear"), "Empties the map variable");
		R::RegisterType<ReadCollectionNode<MapValue<T>>>(mapFull("Variable", "read map"), "Reads a map variable");
		R::RegisterType<WriteCollectionNode<MapValue<T>>>(mapFull("Variable", "write map"), "Writes a map variable");
	}

	template <typename T>
	void RegisterSortNode(const char* typeName)
	{
		static std::vector<std::string> names;
		names.reserve(names.size() + 4);
		names.push_back(std::string("Array/Variable/array variable sort (") + typeName + ")");
		ScriptNodeTypeRegistry::RegisterType<ArrayVarNode<T, VarOp::Sort>>(names.back().c_str(), "Sorts the array variable, smallest first");
	}

	template <typename T>
	void RegisterNumberNodes(const char* typeName)
	{
		using R = ScriptNodeTypeRegistry;
		static std::vector<std::string> names;
		names.reserve(names.size() + 8);
		const auto full = [&](const char* name)
		{
			names.push_back(std::string("Array/Math/array ") + name + " (" + typeName + ")");
			return names.back().c_str();
		};
		R::RegisterType<ReduceNode<T, Reduction::Sum>>(full("sum"), "All the items added together");
		R::RegisterType<ReduceNode<T, Reduction::Min>>(full("min of array"), "The smallest item");
		R::RegisterType<ReduceNode<T, Reduction::Max>>(full("max of array"), "The largest item");
		R::RegisterType<ReduceNode<T, Reduction::Average>>(full("average"), "The sum divided by the number of items");
	}
}

void Ag::RegisterArrayNodes()
{
	RegisterCollectionNodes<int>("int");
	RegisterCollectionNodes<float>("float");
	RegisterCollectionNodes<bool>("bool");
	RegisterCollectionNodes<StringId>("string");
	RegisterCollectionNodes<Vector3f>("float3");

	RegisterSortNode<int>("int");
	RegisterSortNode<float>("float");
	RegisterSortNode<StringId>("string");

	RegisterNumberNodes<int>("int");
	RegisterNumberNodes<float>("float");
}
