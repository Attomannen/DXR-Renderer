#pragma once

#include <array>
#include <list>
#include <vector>

namespace Tga
{
    namespace impl
    {
        class StaticPoolBucketBase
        {
        public:
            static constexpr int BucketSize = 1024;

            StaticPoolBucketBase(void* datas, int elementSize)
                : myDatas(datas), myElementSize(elementSize)
            {
                myCounts.fill(0);
            }

            const void* Get(int index) const;
            void* Edit(int index);
            void Increment(int index);
            void Decrement(int index);
            int GetCount(int index) const;
            int ComputeIndexFromPointer(const void* p) const;
            virtual void OnZero(int index) = 0;
            virtual std::pair<StaticPoolBucketBase*, void*> CloneSlotBase(int index) = 0;
            virtual ~StaticPoolBucketBase() {}

            void* myDatas;
            int myElementSize;
            std::array<int, BucketSize> myCounts;
        };


        /// StaticObjectPool<T>
        /// Manages a global collection of memory buckets for objects of a specific type T,
        /// implementing a singleton pattern for each type. It handles slot allocation and deallocation.
        template <typename T>
        class StaticObjectPool
        {
        public:
            /// Returns the singleton instance of the StaticObjectPool for type T.
            static StaticObjectPool& GetInstance();

            // Not intended for normal usage. Resets all buffers, intended for use with automated tests
            static void ResetInstance();

            /// StaticObjectPool<T>::Bucket
            /// A concrete bucket implementation for type T, inheriting from StaticPoolBucketBase.
            /// It provides the actual `std::array` storage for T objects and implements
            /// the type-specific `OnZero` and `CloneSlot` behaviors.
            class Bucket final : public StaticPoolBucketBase
            {
            public:
                Bucket()
                    : StaticPoolBucketBase(myDatas.data(), sizeof(T)) {}
                
                Bucket(const Bucket&) = delete;
                Bucket& operator=(const Bucket&) = delete;

                void OnZero(int index) override;
                std::pair<Bucket*, T*> CloneSlot(T* object);
                std::pair<StaticPoolBucketBase*, void*> CloneSlotBase(int index) override;

                void Increment(T* object);
                void Decrement(T* object);
                int GetCount(T* object) const;

                std::array<T, StaticPoolBucketBase::BucketSize> myDatas; ///< Raw storage for T objects.
            };

            /// Retrieves a free slot for an object. Reuses from `myFreeSlots` or allocates a new one.
            std::pair<Bucket*, T*> GetFreeSlot();
            std::pair<Bucket*, T*> GetDefault();

            /// Returns a slot to the pool's free list. Typically called by `Bucket::OnZero()`.
            void ReturnSlotInternal(Bucket* bucket, T* object);

        private:
            /// Private constructor for the singleton. Initializes the first bucket,
            /// reserving slot 0 with a default-constructed object and INT_MAX count.
            StaticObjectPool();
            ~StaticObjectPool();

            StaticObjectPool(const StaticObjectPool&) = delete;
            StaticObjectPool& operator=(const StaticObjectPool&) = delete;


            std::list<Bucket> myStorage;
            std::vector<std::pair<Bucket*, T*>> myFreeSlots;
            int myLastBucketCount;

            static bool& IsDestroyed();
        };

        /// Implementations of BucketBase methods are below.
        /// These are placed outside the class definition for clarity.
        inline const void* StaticPoolBucketBase::Get(int index) const
        {
            return static_cast<const void*>(
                static_cast<const char*>(myDatas) + index * myElementSize);
        }

        inline void* StaticPoolBucketBase::Edit(int index)
        {
            return static_cast<void*>(
                static_cast<char*>(myDatas) + index * myElementSize);
        }

        inline void StaticPoolBucketBase::Increment(int index)
        {
            if (myCounts[index] != INT_MAX)
            {
                ++myCounts[index];
            }
        }

        inline void StaticPoolBucketBase::Decrement(int index)
        {
            if (myCounts[index] != INT_MAX)
            {
                if (--myCounts[index] == 0)
                {
                    OnZero(index);
                }
            }
        }

        inline int StaticPoolBucketBase::GetCount(int index) const
        {
            return myCounts[index];
        }

        inline int StaticPoolBucketBase::ComputeIndexFromPointer(const void* p) const
        {
            return static_cast<int>(
                (static_cast<const char*>(p) - static_cast<const char*>(myDatas)) / myElementSize);
        }


        template <typename T>
        inline StaticObjectPool<T>::StaticObjectPool()
        {
            myStorage.emplace_back();

            // Add a default value at the first slot, so that instances can be cheaply constructed
            myStorage.front().myDatas[0] = T();
            myStorage.front().myCounts[0] = INT_MAX;
            myLastBucketCount = 1;
        }

        template <typename T>
        inline StaticObjectPool<T>::~StaticObjectPool()
        {
            IsDestroyed() = true;
        }

        template <typename T>
        inline StaticObjectPool<T>& StaticObjectPool<T>::GetInstance()
        {
            static StaticObjectPool instance;
            return instance;
        }

        template <typename T>
        inline bool& StaticObjectPool<T>::IsDestroyed()
        {
            static bool isDestroyed = false;
            return isDestroyed;
        }
        template <typename T>
        inline void StaticObjectPool<T>::ResetInstance()
        {

            StaticObjectPool<T>& instance = GetInstance();
            instance.~StaticObjectPool<T>();
            new (&instance) StaticObjectPool<T>();

        }


        template <typename T>
        inline void StaticObjectPool<T>::Bucket::OnZero(int index)
        {
            myDatas[index].~T();
            StaticObjectPool::GetInstance().ReturnSlotInternal(this, &myDatas[index]);
        }

        template <typename T>
        inline void StaticObjectPool<T>::Bucket::Increment(T* object)
        {
            int index = (int)(object - myDatas.data());
            if (myCounts[index] != INT_MAX)
            {
                ++myCounts[index];
            }
        }

        template <typename T>
        inline void StaticObjectPool<T>::Bucket::Decrement(T* object)
        {
            // Static destructor has already run. Nothing to do
            if (StaticObjectPool::IsDestroyed())
                return;

            int index = (int)(object - myDatas.data());
            if (myCounts[index] != INT_MAX)
            {
                if (--myCounts[index] == 0)
                {
                    OnZero(index);
                }
            }
        }

        template <typename T>
        inline int StaticObjectPool<T>::Bucket::GetCount(T* object) const
        {
            return myCounts[object - myDatas.data()];
        }

        template <typename T>
        inline std::pair<StaticPoolBucketBase*, void*> StaticObjectPool<T>::Bucket::CloneSlotBase(int index)
        {
            auto p = CloneSlot(myDatas.data() + index);
            return { p.first, p.second };
        }

        template <typename T>
        inline std::pair<typename StaticObjectPool<T>::Bucket*, T*> StaticObjectPool<T>::Bucket::CloneSlot(T* object)
        {
            auto& pool = StaticObjectPool<T>::GetInstance();
            auto slot = pool.GetFreeSlot();
            new (slot.second) T(*object);
            slot.first->myCounts[slot.second - slot.first->myDatas.data()] = 1;
            Decrement(object);
            return { slot.first, slot.second };
        }

        template <typename T>
        inline std::pair<typename StaticObjectPool<T>::Bucket*, T*> StaticObjectPool<T>::GetFreeSlot()
        {
            if (!myFreeSlots.empty())
            {
                auto slot = myFreeSlots.back();
                myFreeSlots.pop_back();
                return slot;
            }
            if (myLastBucketCount == StaticPoolBucketBase::BucketSize)
            {
                myStorage.emplace_back();
                myLastBucketCount = 0;
            }
            return { &myStorage.back(), myStorage.back().myDatas.data() + myLastBucketCount++ };
        }

        template <typename T>
        inline std::pair<typename StaticObjectPool<T>::Bucket*, T*> StaticObjectPool<T>::GetDefault()
        {
            return { &myStorage.front(), &myStorage.front().myDatas[0] };
        }

        template <typename T>
        inline void StaticObjectPool<T>::ReturnSlotInternal(Bucket* bucket, T* object)
        {
            myFreeSlots.push_back({ bucket, object });
        }
    }

    /// CopyOnWriteWrapper<T>
    /// A wrapper for objects of type T implementing copy-on-write semantics.
    /// Provides const access (`Get()`) and non-const access (`Edit()`) which
    /// triggers a clone if the object is shared, ensuring unique ownership for modification.
    template <typename T>
    class CopyOnWriteWrapper
    {
    public:
        /// Creates a new wrapper, constructs a new T object in the pool, and sets its initial count to 1.
        template <typename... Args>
        static CopyOnWriteWrapper<T> Create(Args&&... args);

        /// Provides const access to the wrapped object.
        const T& Get() const;

        /// Provides non-const access. Clones the object if shared before returning a modifiable reference.
        T& Edit();

        CopyOnWriteWrapper();
        CopyOnWriteWrapper(const CopyOnWriteWrapper& other);
        CopyOnWriteWrapper(CopyOnWriteWrapper&& other);
        CopyOnWriteWrapper& operator=(const CopyOnWriteWrapper& other);
        CopyOnWriteWrapper& operator=(CopyOnWriteWrapper&& other);
        ~CopyOnWriteWrapper();

    private:
        /// Private constructor: Used by the static `Create` method.
        CopyOnWriteWrapper(impl::StaticObjectPool<T>::Bucket* b, T* ptr);

        impl::StaticObjectPool<T>::Bucket* myBucket; ///< Pointer to the bucket holding the object.
        T* myObjectPtr;              ///< Direct pointer to the wrapped object.
    };

    /// CopyOnWriteWrapperPolymorphic<Base>
    /// A copy-on-write wrapper for polymorphic object hierarchies. Templated on a `Base` class,
    /// it can hold objects of any `Derived` type. `Edit()` uses pointer arithmetic to correctly
    /// handle adjusted `Base` pointers during cloning.
    template <typename Base>
    class CopyOnWriteWrapperPolymorphic
    {
    public:
        /// Creates a new wrapper for a `Derived` object (inheriting from `Base`),
        /// constructing it in the appropriate pool and setting its initial count to 1.
        template <typename Derived, typename... Args,
            typename = std::enable_if_t<std::is_base_of<Base, Derived>::value>>
            static CopyOnWriteWrapperPolymorphic<Base> Create(Args&&... args);

        /// Provides const access to the wrapped Base object.
        const Base& Get() const;

        /// Provides non-const access. Clones the object if shared.
        /// Handles polymorphic adjustment by calculating and reapplying pointer offsets.
        Base& Edit();

        CopyOnWriteWrapperPolymorphic();
        CopyOnWriteWrapperPolymorphic(const CopyOnWriteWrapperPolymorphic& other);
        CopyOnWriteWrapperPolymorphic(CopyOnWriteWrapperPolymorphic&& other) noexcept;
        CopyOnWriteWrapperPolymorphic& operator=(const CopyOnWriteWrapperPolymorphic& other);
        CopyOnWriteWrapperPolymorphic& operator=(CopyOnWriteWrapperPolymorphic&& other) noexcept;
        ~CopyOnWriteWrapperPolymorphic();

    private:
        /// Private constructor: Used by the static `Create` method.
        CopyOnWriteWrapperPolymorphic(impl::StaticPoolBucketBase* b, Base* ptr);

        impl::StaticPoolBucketBase* myBucket; ///< Pointer to the bucket holding the object.
        Base* myBasePtr;            ///< Adjusted pointer to the Base subobject.
    };

    template <typename T>
    template <typename... Args>
    inline CopyOnWriteWrapper<T> CopyOnWriteWrapper<T>::Create(Args&&... args)
    {
        auto& pool = impl::StaticObjectPool<T>::GetInstance();
        auto slot = pool.GetFreeSlot();
        *slot.second = { std::forward<Args>(args)... };
        slot.first->myCounts[slot.second - slot.first->myDatas.data()] = 1;
        return CopyOnWriteWrapper(slot.first, slot.second);
    }

    template <typename T>
    inline const T& CopyOnWriteWrapper<T>::Get() const
    {
        return *myObjectPtr;
    }

    template <typename T>
    inline T& CopyOnWriteWrapper<T>::Edit()
    {
        if (myBucket->GetCount(myObjectPtr) > 1)
        {
            auto pair = myBucket->CloneSlot(myObjectPtr);
            myBucket = pair.first;
            myObjectPtr = pair.second;
        }
        return *myObjectPtr;
    }

    template <typename T>
    inline CopyOnWriteWrapper<T>::CopyOnWriteWrapper()
    {
        auto& pool = impl::StaticObjectPool<T>::GetInstance();
        auto slot = pool.GetDefault();
        myBucket = slot.first;
        myObjectPtr = slot.second;
    }

    template <typename T>
    inline CopyOnWriteWrapper<T>::CopyOnWriteWrapper(const CopyOnWriteWrapper& other)
        : myBucket(other.myBucket), myObjectPtr(other.myObjectPtr)
    {
        myBucket->Increment(myObjectPtr);
    }

    template <typename T>
    inline CopyOnWriteWrapper<T>::CopyOnWriteWrapper(CopyOnWriteWrapper&& other)
        : myBucket(other.myBucket), myObjectPtr(other.myObjectPtr)
    {
        other.myBucket = nullptr;
    }

    template <typename T>
    inline CopyOnWriteWrapper<T>& CopyOnWriteWrapper<T>::operator=(const CopyOnWriteWrapper& other)
    {
        if (this != &other)
        {
            if (myBucket)
            {
                myBucket->Decrement(myObjectPtr);
            }
            myBucket = other.myBucket;
            myObjectPtr = other.myObjectPtr;
         
            myBucket->Increment(myObjectPtr);         
        }
        return *this;
    }

    template <typename T>
    inline CopyOnWriteWrapper<T>& CopyOnWriteWrapper<T>::operator=(CopyOnWriteWrapper&& other)
    {
        if (this != &other)
        {
            if (myBucket)
            {
                myBucket->Decrement(myObjectPtr);
            }
            myBucket = other.myBucket;
            myObjectPtr = other.myObjectPtr;
            other.myBucket = nullptr;
        }
        return *this;
    }

    template <typename T>
    inline CopyOnWriteWrapper<T>::~CopyOnWriteWrapper()
    {
        if (myBucket)
        {
            myBucket->Decrement(myObjectPtr);
        }
    }

    template <typename T>
    inline CopyOnWriteWrapper<T>::CopyOnWriteWrapper(impl::StaticObjectPool<T>::Bucket* b, T* ptr) : myBucket(b), myObjectPtr(ptr)
    {}


    template <typename Base>
    template <typename Derived, typename... Args, typename>
    inline CopyOnWriteWrapperPolymorphic<Base> CopyOnWriteWrapperPolymorphic<Base>::Create(Args&&... args)
    {
        auto& pool = impl::StaticObjectPool<Derived>::GetInstance();
        auto slot = pool.GetFreeSlot();
        *slot.second = { std::forward<Args>(args)... };
        slot.first->myCounts[slot.second - slot.first->myDatas.data()] = 1;
        Base* base = static_cast<Base*>(slot.second);
        return CopyOnWriteWrapperPolymorphic(slot.first, base);
    }

    template <typename Base>
    inline const Base& CopyOnWriteWrapperPolymorphic<Base>::Get() const
    {
        return *myBasePtr;
    }

    template <typename Base>
    inline Base& CopyOnWriteWrapperPolymorphic<Base>::Edit()
    {
        int idx = myBucket->ComputeIndexFromPointer(static_cast<void*>(myBasePtr));

        if (myBucket->GetCount(idx) > 1)
        {
            const char* unadjustedOld = static_cast<const char*>(myBucket->Get(idx));
            ptrdiff_t offsetDelta = reinterpret_cast<const char*>(myBasePtr) -
                reinterpret_cast<const char*>(unadjustedOld);

            auto cloneResult = myBucket->CloneSlotBase(idx);
            myBucket = cloneResult.first;

            void* newUnadjusted = myBucket->Edit(myBucket->ComputeIndexFromPointer(cloneResult.second));
            myBasePtr = reinterpret_cast<Base*>(reinterpret_cast<char*>(newUnadjusted) + offsetDelta);
        }
        return *myBasePtr;
    }

    template <typename T>
    inline CopyOnWriteWrapperPolymorphic<T>::CopyOnWriteWrapperPolymorphic()
    {
        // CopyOnWriteWrapperPolymorphic requires a default instance to be available for the polymorphic type

        auto& pool = impl::StaticObjectPool<T>::GetInstance();
        auto slot = pool.GetDefault();
        myBucket = slot.first;
        myBasePtr = slot.second;
    }

    template <typename Base>
    inline CopyOnWriteWrapperPolymorphic<Base>::CopyOnWriteWrapperPolymorphic(const CopyOnWriteWrapperPolymorphic& other)
        : myBucket(other.myBucket), myBasePtr(other.myBasePtr)
    {
        int idx = myBucket->ComputeIndexFromPointer(static_cast<void*>(myBasePtr));
        myBucket->Increment(idx);
    }

    template <typename Base>
    inline CopyOnWriteWrapperPolymorphic<Base>::CopyOnWriteWrapperPolymorphic(CopyOnWriteWrapperPolymorphic&& other) noexcept
        : myBucket(other.myBucket), myBasePtr(other.myBasePtr)
    {
        other.myBucket = nullptr;
        other.myBasePtr = nullptr;
    }

    template <typename Base>
    inline CopyOnWriteWrapperPolymorphic<Base>& CopyOnWriteWrapperPolymorphic<Base>::operator=(const CopyOnWriteWrapperPolymorphic& other)
    {
        if (this != &other)
        {
            if (myBucket)
            {
                int idx = myBucket->ComputeIndexFromPointer(static_cast<void*>(myBasePtr));
                myBucket->Decrement(idx);
            }

            myBucket = other.myBucket;
            myBasePtr = other.myBasePtr;

            int idx = myBucket->ComputeIndexFromPointer(static_cast<void*>(myBasePtr));
            myBucket->Increment(idx);
        }
        return *this;
    }

    template <typename Base>
    inline CopyOnWriteWrapperPolymorphic<Base>& CopyOnWriteWrapperPolymorphic<Base>::operator=(CopyOnWriteWrapperPolymorphic&& other) noexcept
    {
        if (this != &other)
        {
            if (myBucket)
            {
                int idx = myBucket->ComputeIndexFromPointer(static_cast<void*>(myBasePtr));
                myBucket->Decrement(idx);
            }

            myBucket = other.myBucket;
            myBasePtr = other.myBasePtr;

            other.myBucket = nullptr;
            other.myBasePtr = nullptr;
        }
        return *this;
    }

    template <typename Base>
    inline CopyOnWriteWrapperPolymorphic<Base>::~CopyOnWriteWrapperPolymorphic()
    {
        if (myBucket)
        {
            int idx = myBucket->ComputeIndexFromPointer(static_cast<void*>(myBasePtr));
            myBucket->Decrement(idx);
        }
    }

    template <typename Base>
    inline CopyOnWriteWrapperPolymorphic<Base>::CopyOnWriteWrapperPolymorphic(impl::StaticPoolBucketBase* b, Base* ptr) : myBucket(b), myBasePtr(ptr)
    {}

} // namespace Tga