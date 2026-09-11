#include "CppUnitTest.h"
#include <tge/script/CopyOnWriteWrapper.h>
#include <string>
#include <iostream>
#include <vector> 

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Tga;

namespace CopyOnWriteTests
{
    struct MyData
    {
        int val;
        std::string text;

        MyData(int v = 0, const std::string& t = "") : val(v), text(t) {}
        MyData(const MyData& other) : val(other.val), text(other.text) {}
        MyData(MyData&& other) noexcept : val(other.val), text(std::move(other.text)) {}
        MyData& operator=(const MyData& other) { if (this != &other) { val = other.val; text = other.text; } return *this; }
        MyData& operator=(MyData&& other) noexcept { if (this != &other) { val = other.val; text = std::move(other.text); } return *this; }
    };

    class BaseClassA
    {
    public:
        int baseAValue;
        BaseClassA(int val = 0) : baseAValue(val) {}
        virtual ~BaseClassA() = default;
        virtual std::string GetTypeA() const { return "BaseA"; }
    };

    class BaseClassB
    {
    public:
        double baseBValue;
        BaseClassB(double val = 0.0) : baseBValue(val) {}
        virtual ~BaseClassB() = default;
        virtual std::string GetTypeB() const { return "BaseB"; }
    };

    class SingleDerived : public BaseClassA
    {
    public:
        std::string singleDerivedData;
        SingleDerived(int bVal = 0, const std::string& sData = "") : BaseClassA(bVal), singleDerivedData(sData) {}
        std::string GetTypeA() const override { return "SingleDerived"; }
        SingleDerived(const SingleDerived& other) = default;
        SingleDerived(SingleDerived&& other) noexcept = default;
        SingleDerived& operator=(const SingleDerived& other) = default;
        SingleDerived& operator=(SingleDerived&& other) noexcept = default;
    };

    class MultiDerived : public BaseClassA, public BaseClassB
    {
    public:
        char multiChar;
        MultiDerived(int bAVal = 0, double bBVal = 0.0, char mC = ' ')
            : BaseClassA(bAVal), BaseClassB(bBVal), multiChar(mC) {}

        std::string GetTypeA() const override { return "MultiDerived_A"; }
        std::string GetTypeB() const override { return "MultiDerived_B"; }

        MultiDerived(const MultiDerived& other) = default;
        MultiDerived(MultiDerived&& other) noexcept = default;
        MultiDerived& operator=(const MultiDerived& other) = default;
        MultiDerived& operator=(MultiDerived&& other) noexcept = default;
    };

    TEST_CLASS(CopyOnWriteWrapperTests)
    {
    public:
        TEST_METHOD_INITIALIZE(Initialize)
        {
            impl::StaticObjectPool<MyData>::ResetInstance();
        }

        TEST_METHOD(CreateInitializesObject)
        {
            CopyOnWriteWrapper<MyData> wrapper = CopyOnWriteWrapper<MyData>::Create(10, "Hello");
            Assert::AreEqual(10, wrapper.Get().val);
            Assert::AreEqual("Hello", wrapper.Get().text.c_str());
        }

        TEST_METHOD(DefaultConstructorCreatesDefaultObject)
        {
            CopyOnWriteWrapper<MyData> wrapper;
            Assert::AreEqual(0, wrapper.Get().val);
            Assert::AreEqual("", wrapper.Get().text.c_str());
        }

        TEST_METHOD(CopyConstructorSharesObject)
        {
            CopyOnWriteWrapper<MyData> wrapper1 = CopyOnWriteWrapper<MyData>::Create(100, "Original");
            const void* originalPtr = static_cast<const void*>(&wrapper1.Get());

            CopyOnWriteWrapper<MyData> wrapper2 = wrapper1;
            Assert::AreEqual(originalPtr, static_cast<const void*>(&wrapper2.Get()));
        }

        TEST_METHOD(AssignmentOperatorTransfersSharedOwnership)
        {
            CopyOnWriteWrapper<MyData> wrapper1 = CopyOnWriteWrapper<MyData>::Create(1, "A");
            CopyOnWriteWrapper<MyData> wrapper2 = CopyOnWriteWrapper<MyData>::Create(2, "B");

            const void* objPtr2 = static_cast<const void*>(&wrapper2.Get());

            wrapper1 = wrapper2;
            Assert::AreEqual(objPtr2, static_cast<const void*>(&wrapper1.Get()));
        }

        TEST_METHOD(MoveConstructorTransfersOwnership)
        {
            CopyOnWriteWrapper<MyData> original = CopyOnWriteWrapper<MyData>::Create(50, "Moved");
            const void* originalObjectPtr = static_cast<const void*>(&original.Get());

            CopyOnWriteWrapper<MyData> moved = std::move(original);

            Assert::AreEqual(50, moved.Get().val);
            Assert::AreEqual(originalObjectPtr, static_cast<const void*>(&moved.Get()));
        }

        TEST_METHOD(MoveAssignmentOperatorTransfersOwnership)
        {
            CopyOnWriteWrapper<MyData> wrapper1 = CopyOnWriteWrapper<MyData>::Create(10, "Wrapper1");
            CopyOnWriteWrapper<MyData> wrapper2 = CopyOnWriteWrapper<MyData>::Create(20, "Wrapper2");

            const void* objPtr2 = static_cast<const void*>(&wrapper2.Get());

            wrapper1 = std::move(wrapper2);

            Assert::AreEqual(objPtr2, static_cast<const void*>(&wrapper1.Get()));
        }

        TEST_METHOD(EditClonesWhenObjectIsShared)
        {
            CopyOnWriteWrapper<MyData> wrapper1 = CopyOnWriteWrapper<MyData>::Create(10, "Initial");
            CopyOnWriteWrapper<MyData> wrapper2 = wrapper1;

            const void* originalObjectPtr = static_cast<const void*>(&wrapper1.Get());

            wrapper1.Edit().val = 20;

            Assert::AreNotEqual(originalObjectPtr, static_cast<const void*>(&wrapper1.Get()));
            Assert::AreEqual(originalObjectPtr, static_cast<const void*>(&wrapper2.Get()));

            Assert::AreEqual(20, wrapper1.Get().val);
            Assert::AreEqual(10, wrapper2.Get().val);
        }

        TEST_METHOD(EditDoesNotCloneWhenObjectIsNotShared)
        {
            CopyOnWriteWrapper<MyData> wrapper = CopyOnWriteWrapper<MyData>::Create(10, "Initial");
            const void* originalPtr = static_cast<const void*>(&wrapper.Get());

            wrapper.Edit().val = 20;

            Assert::AreEqual(originalPtr, static_cast<const void*>(&wrapper.Get()));
            Assert::AreEqual(20, wrapper.Get().val);
        }

        TEST_METHOD(ConstWrapperPreventsModification)
        {
            CopyOnWriteWrapper<MyData> wrapper = CopyOnWriteWrapper<MyData>::Create(10, "Test");
            const CopyOnWriteWrapper<MyData>& constWrapper = wrapper;

            Assert::AreEqual(10, constWrapper.Get().val);
        }

        TEST_METHOD(StaticObjectPoolRecyclesObjects)
        {
            {
                CopyOnWriteWrapper<MyData> w1 = CopyOnWriteWrapper<MyData>::Create(100, "One");
                CopyOnWriteWrapper<MyData> w2 = CopyOnWriteWrapper<MyData>::Create(200, "Two");
                CopyOnWriteWrapper<MyData> w3 = CopyOnWriteWrapper<MyData>::Create(300, "Three");
            }

            {
                CopyOnWriteWrapper<MyData> w4 = CopyOnWriteWrapper<MyData>::Create(400, "Four");
                CopyOnWriteWrapper<MyData> w5 = CopyOnWriteWrapper<MyData>::Create(500, "Five");

                Assert::AreEqual(400, w4.Get().val);
                Assert::AreEqual(500, w5.Get().val);
            }
        }

        TEST_METHOD(StaticObjectPoolHandlesMultiBucketCreationAndDestruction)
        {
            const int numObjectsToCreate = Tga::impl::StaticPoolBucketBase::BucketSize + 50;

            std::vector<CopyOnWriteWrapper<MyData>> wrappers;
            wrappers.reserve(numObjectsToCreate);

            for (int i = 0; i < numObjectsToCreate; ++i)
            {
                wrappers.push_back(CopyOnWriteWrapper<MyData>::Create(i, "Capacity Test " + std::to_string(i)));
            }

            // Only check the first element of the first bucket to ensure basic initialization works.
            Assert::AreEqual(0, wrappers[0].Get().val);
            Assert::AreEqual("Capacity Test 0", wrappers[0].Get().text.c_str());

            // Check the last element of the first bucket to ensure it's still correct at the boundary.
            Assert::AreEqual(Tga::impl::StaticPoolBucketBase::BucketSize - 1, wrappers[Tga::impl::StaticPoolBucketBase::BucketSize - 1].Get().val);
            Assert::AreEqual(("Capacity Test " + std::to_string(Tga::impl::StaticPoolBucketBase::BucketSize - 1)).c_str(), wrappers[Tga::impl::StaticPoolBucketBase::BucketSize - 1].Get().text.c_str());

            wrappers.clear();
        }
    };

    TEST_CLASS(CopyOnWriteWrapperPolymorphicTests)
    {
    public:
        TEST_METHOD_INITIALIZE(Initialize)
        {
            impl::StaticObjectPool<BaseClassA>::ResetInstance();
            impl::StaticObjectPool<BaseClassB>::ResetInstance();
            impl::StaticObjectPool<SingleDerived>::ResetInstance();
            impl::StaticObjectPool<MultiDerived>::ResetInstance();
        }

        TEST_METHOD(PolymorphicCreateInitializesCorrectly_SingleInheritance)
        {
            CopyOnWriteWrapperPolymorphic<BaseClassA> wrapper = CopyOnWriteWrapperPolymorphic<BaseClassA>::Create<SingleDerived>(10, "Hello");

            Assert::AreEqual(10, wrapper.Get().baseAValue);
            Assert::AreEqual(std::string("SingleDerived").c_str(), wrapper.Get().GetTypeA().c_str());

            const SingleDerived* derivedPtr = dynamic_cast<const SingleDerived*>(&wrapper.Get());
            Assert::IsNotNull(derivedPtr);
            Assert::AreEqual("Hello", derivedPtr->singleDerivedData.c_str());
        }

        TEST_METHOD(PolymorphicCreateInitializesCorrectly_MultiInheritance_BaseA)
        {
            CopyOnWriteWrapperPolymorphic<BaseClassA> wrapperA = CopyOnWriteWrapperPolymorphic<BaseClassA>::Create<MultiDerived>(100, 200.0, 'M');

            Assert::AreEqual(100, wrapperA.Get().baseAValue);
            Assert::AreEqual(std::string("MultiDerived_A").c_str(), wrapperA.Get().GetTypeA().c_str());

            const MultiDerived* multiDerivedPtrA = dynamic_cast<const MultiDerived*>(&wrapperA.Get());
            Assert::IsNotNull(multiDerivedPtrA);
            Assert::AreEqual(200.0, multiDerivedPtrA->baseBValue);
            Assert::AreEqual('M', multiDerivedPtrA->multiChar);
        }

        TEST_METHOD(PolymorphicCreateInitializesCorrectly_MultiInheritance_BaseB)
        {
            CopyOnWriteWrapperPolymorphic<BaseClassB> wrapperB = CopyOnWriteWrapperPolymorphic<BaseClassB>::Create<MultiDerived>(100, 200.0, 'N');

            Assert::AreEqual(200.0, wrapperB.Get().baseBValue);
            Assert::AreEqual(std::string("MultiDerived_B").c_str(), wrapperB.Get().GetTypeB().c_str());

            const MultiDerived* multiDerivedPtrB = dynamic_cast<const MultiDerived*>(&wrapperB.Get());
            Assert::IsNotNull(multiDerivedPtrB);
            Assert::AreEqual(100, multiDerivedPtrB->baseAValue);
            Assert::AreEqual('N', multiDerivedPtrB->multiChar);
        }

        TEST_METHOD(PolymorphicCopyConstructorSharesObject)
        {
            CopyOnWriteWrapperPolymorphic<BaseClassA> wrapper1 = CopyOnWriteWrapperPolymorphic<BaseClassA>::Create<SingleDerived>(100, "ABC");
            const void* originalPtr = static_cast<const void*>(&wrapper1.Get());

            CopyOnWriteWrapperPolymorphic<BaseClassA> wrapper2 = wrapper1;
            Assert::AreEqual(originalPtr, static_cast<const void*>(&wrapper2.Get()));
        }

        TEST_METHOD(PolymorphicAssignmentOperatorTransfersSharedOwnership)
        {
            CopyOnWriteWrapperPolymorphic<BaseClassA> wrapper1 = CopyOnWriteWrapperPolymorphic<BaseClassA>::Create<SingleDerived>(1, "A");
            CopyOnWriteWrapperPolymorphic<BaseClassA> wrapper2 = CopyOnWriteWrapperPolymorphic<BaseClassA>::Create<SingleDerived>(2, "B");

            const void* objPtr2 = static_cast<const void*>(&wrapper2.Get());

            wrapper1 = wrapper2;

            Assert::AreEqual(objPtr2, static_cast<const void*>(&wrapper1.Get()));
            Assert::AreEqual(std::string("SingleDerived").c_str(), wrapper1.Get().GetTypeA().c_str());
        }

        TEST_METHOD(PolymorphicMoveConstructorTransfersOwnership)
        {
            CopyOnWriteWrapperPolymorphic<BaseClassA> original = CopyOnWriteWrapperPolymorphic<BaseClassA>::Create<SingleDerived>(50, "Moved");
            const void* originalObjectPtr = static_cast<const void*>(&original.Get());

            CopyOnWriteWrapperPolymorphic<BaseClassA> moved = std::move(original);

            Assert::AreEqual(50, moved.Get().baseAValue);
            Assert::AreEqual(originalObjectPtr, static_cast<const void*>(&moved.Get()));
        }

        TEST_METHOD(PolymorphicMoveAssignmentOperatorTransfersOwnership)
        {
            CopyOnWriteWrapperPolymorphic<BaseClassA> wrapper1 = CopyOnWriteWrapperPolymorphic<BaseClassA>::Create<SingleDerived>(10, "Wrapper1");
            CopyOnWriteWrapperPolymorphic<BaseClassA> wrapper2 = CopyOnWriteWrapperPolymorphic<BaseClassA>::Create<SingleDerived>(20, "Wrapper2");

            const void* objPtr2 = static_cast<const void*>(&wrapper2.Get());

            wrapper1 = std::move(wrapper2);

            Assert::AreEqual(objPtr2, static_cast<const void*>(&wrapper1.Get()));
            Assert::AreEqual(std::string("SingleDerived").c_str(), wrapper1.Get().GetTypeA().c_str());
        }

        TEST_METHOD(PolymorphicEditClonesWhenObjectIsShared_SingleInheritance)
        {
            CopyOnWriteWrapperPolymorphic<BaseClassA> wrapper1 = CopyOnWriteWrapperPolymorphic<BaseClassA>::Create<SingleDerived>(10, "Initial");
            CopyOnWriteWrapperPolymorphic<BaseClassA> wrapper2 = wrapper1;

            const void* originalBasePtr = static_cast<const void*>(&wrapper1.Get());

            wrapper1.Edit().baseAValue = 20;

            Assert::AreNotEqual(originalBasePtr, static_cast<const void*>(&wrapper1.Get()));
            Assert::AreEqual(originalBasePtr, static_cast<const void*>(&wrapper2.Get()));

            Assert::AreEqual(20, wrapper1.Get().baseAValue);
            Assert::AreEqual(10, wrapper2.Get().baseAValue);
            Assert::AreEqual(std::string("SingleDerived").c_str(), wrapper1.Get().GetTypeA().c_str());

            const SingleDerived* originalDerivedPtr = dynamic_cast<const SingleDerived*>(&wrapper2.Get());
            SingleDerived* editedDerivedPtr = dynamic_cast<SingleDerived*>(&wrapper1.Edit());
            Assert::IsNotNull(originalDerivedPtr);
            Assert::IsNotNull(editedDerivedPtr);
            Assert::AreEqual(originalDerivedPtr->singleDerivedData.c_str(), editedDerivedPtr->singleDerivedData.c_str());
        }

        TEST_METHOD(PolymorphicEditClonesWhenObjectIsShared_MultiInheritance)
        {
            CopyOnWriteWrapperPolymorphic<BaseClassA> wrapper1 = CopyOnWriteWrapperPolymorphic<BaseClassA>::Create<MultiDerived>(10, 10.1, 'X');
            CopyOnWriteWrapperPolymorphic<BaseClassA> wrapper2 = wrapper1;

            const void* originalBaseAPtr = static_cast<const void*>(&wrapper1.Get());

            wrapper1.Edit().baseAValue = 20;

            Assert::AreNotEqual(originalBaseAPtr, static_cast<const void*>(&wrapper1.Get()));
            Assert::AreEqual(originalBaseAPtr, static_cast<const void*>(&wrapper2.Get()));

            Assert::AreEqual(20, wrapper1.Get().baseAValue);
            Assert::AreEqual(10, wrapper2.Get().baseAValue);
            Assert::AreEqual(std::string("MultiDerived_A").c_str(), wrapper1.Get().GetTypeA().c_str());

            const MultiDerived* originalDerivedPtr = dynamic_cast<const MultiDerived*>(&wrapper2.Get());
            MultiDerived* editedDerivedPtr = dynamic_cast<MultiDerived*>(&wrapper1.Edit());
            Assert::IsNotNull(originalDerivedPtr);
            Assert::IsNotNull(editedDerivedPtr);

            Assert::AreEqual(originalDerivedPtr->baseBValue, editedDerivedPtr->baseBValue);
            Assert::AreEqual(originalDerivedPtr->multiChar, editedDerivedPtr->multiChar);
            Assert::AreEqual(std::string("MultiDerived_B").c_str(), editedDerivedPtr->GetTypeB().c_str());
        }

        TEST_METHOD(PolymorphicEditDoesNotCloneWhenObjectIsNotShared)
        {
            CopyOnWriteWrapperPolymorphic<BaseClassA> wrapper = CopyOnWriteWrapperPolymorphic<BaseClassA>::Create<SingleDerived>(10, "NotCloned");
            const void* originalPtr = static_cast<const void*>(&wrapper.Get());

            wrapper.Edit().baseAValue = 20;

            Assert::AreEqual(originalPtr, static_cast<const void*>(&wrapper.Get()));
            Assert::AreEqual(20, wrapper.Get().baseAValue);
        }
    };
}