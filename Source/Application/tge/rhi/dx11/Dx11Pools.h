#pragma once
#include <vector>
#include <cstdint>
#include "tge/rhi/Handles.h"

// Generation-checked slot pool. Slot 0 is reserved as the null slot, so a live
// handle always has index >= 1 and matches Handle::IsValid().
namespace Tga::rhi::dx11
{
	template <class T, class HandleT>
	class Pool
	{
	public:
		Pool() { mySlots.emplace_back(); /* reserve slot 0 = null */ }

		HandleT Alloc(T&& value)
		{
			uint32_t idx;
			if (!myFree.empty())
			{
				idx = myFree.back();
				myFree.pop_back();
			}
			else
			{
				idx = (uint32_t)mySlots.size();
				mySlots.emplace_back();
			}
			mySlots[idx].value = std::move(value);
			mySlots[idx].alive = true;
			return HandleT{ idx, mySlots[idx].generation };
		}

		T* Get(HandleT h)
		{
			if (!h.IsValid() || h.index >= mySlots.size()) return nullptr;
			Slot& s = mySlots[h.index];
			if (!s.alive || s.generation != h.generation) return nullptr;
			return &s.value;
		}

		const T* Get(HandleT h) const
		{
			return const_cast<Pool*>(this)->Get(h);
		}

		void Free(HandleT h)
		{
			if (!h.IsValid() || h.index >= mySlots.size()) return;
			Slot& s = mySlots[h.index];
			if (!s.alive || s.generation != h.generation) return;
			s.value = T{};
			s.alive = false;
			s.generation++;
			if (s.generation == 0) s.generation = 1;
			myFree.push_back(h.index);
		}

		template <class Fn>
		void ForEachAlive(Fn&& fn)
		{
			for (size_t i = 1; i < mySlots.size(); ++i)
				if (mySlots[i].alive) fn(mySlots[i].value);
		}

	private:
		struct Slot { T value{}; uint32_t generation = 1; bool alive = false; };
		std::deque<Slot> mySlots;
		std::vector<uint32_t> myFree;
	};
}
