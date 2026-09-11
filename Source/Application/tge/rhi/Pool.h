#pragma once
#include <vector>
#include <cstdint>
#include <memory>
#include "tge/rhi/Handles.h"

// Generation-checked slot pool. Slot 0 is reserved as the null slot, so a live
// handle always has index >= 1 and matches Handle::IsValid(). Backend-agnostic
// (originally lived under rhi::dx11 as the DX11 backend's own helper; hoisted
// here so the DX12 backend can share it without importing from dx11's
// namespace -- Dx11Pools.h now just aliases this).
namespace Tga::rhi
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
			Slot& s = mySlots[idx];
			s.value = static_cast<T&&>(value);
			s.alive = true;
			return HandleT{ idx, s.generation };
		}

		T* Get(HandleT h)
		{
			if (!h.IsValid() || h.index >= mySlots.size()) return nullptr;
			Slot& s = mySlots[h.index];
			if (!s.alive || s.generation != h.generation) return nullptr;
			// NOTE: plain `&s.value` is wrong here -- for T = ComPtr<...>, unary &
			// is overloaded (WRL's out-parameter idiom) and does NOT return the
			// object's real address. std::addressof bypasses any such overload.
			return std::addressof(s.value);
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
		std::vector<Slot> mySlots;
		std::vector<uint32_t> myFree;
	};
}
